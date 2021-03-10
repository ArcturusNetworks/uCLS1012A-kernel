// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA SoC CX2070X codec driver
 *
 * Copyright:   (C) 2017-2021 Arcturus Networks Inc.
 *                  by Oleksandr Zhadan
 *
 * based on cx2070x.c
 * Copyright:   (C) 2009/2010 Conexant Systems
 *		 by Simon Ho <simon.ho@conexant.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *************************************************************************
 *  2017.08 : Linux 4.x kernel support added
 *  2019.03 : fsl,imx-audio-cx2070x support added
 *  2021.01 : Linux 5.x kernel support added
 *
 *************************************************************************
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/gpio.h>
#include <sound/jack.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/spi/spi.h>
#include <linux/firmware.h>
#include <linux/regmap.h>

#include "cx2070x.h"

#define VOLATILE_FOR_ALL 1 //FIXME: remove the change after CX reset signal is fixed 

extern int cx2070x_sysfs_alloc(struct cx2070x_priv *cx2070x);

static const struct reg_default cx2070x_reg_defaults[] = {
	{CXREG_USB_LOCAL_VOLUME, 0x48},
	{CXREG_CLOCK_DIVIDER, 0x77},
	{CXREG_PORT1_CONTROL, 0xF2},
	{CXREG_PORT1_TX_FRAME, 0x03},
	{CXREG_PORT1_RX_FRAME, 0x03},
	{CXREG_PORT1_TX_SYNC, 0x0F},
	{CXREG_PORT1_RX_SYNC, 0x0F},
	{CXREG_PORT1_CONTROL2, 0x05},
	{CXREG_PORT1_RX_SLOT2, 0x04},
	{CXREG_PORT1_TX_SLOT2, 0x04},
	{CXREG_PORT1_DELAY, 0x01},
	{CXREG_PORT2_FRAME, 0x07},
	{CXREG_PORT2_SYNC, 0x1F},
	{CXREG_PORT2_SAMPLE, 0x01},
	{CXREG_PORT2_RX_SLOT2, 0x04},
	{CXREG_PORT2_TX_SLOT2, 0x04},
	{CXREG_DSPDAC, 0xCE},
	{CXREG_CLASSD_GAIN, 0x0F},
	{CXREG_ADC1L_GAIN, 0xF2},
	{CXREG_OUTPUT_CONTROL, 0x03},
};

enum {
	MEM_TYPE_RAM = 1 /* CTL */ ,
	MEM_TYPE_SPX = 2,
	MEM_TYPE_EEPROM = 3,
	MEM_TYPE_CPX = 4,
	MEM_TYPE_EEPROM_RESET = 0x8003,
};

#define CX2070X_DRIVER_VERSION AUDDRV_VERSION(5, 0, 21, 6)

#define CX1070X_MAX_REGISTER 0X1300
#define AUDIO_NAME	"cx2070x"
#define MAX_REGISTER_NUMBER 0x1250
#define CX2070X_RATES	( \
	SNDRV_PCM_RATE_8000  \
	| SNDRV_PCM_RATE_11025 \
	| SNDRV_PCM_RATE_16000 \
	| SNDRV_PCM_RATE_22050 \
	| SNDRV_PCM_RATE_32000 \
	| SNDRV_PCM_RATE_44100 \
	| SNDRV_PCM_RATE_48000 \
	| SNDRV_PCM_RATE_88200 \
	| SNDRV_PCM_RATE_96000)

#define CX2070X_FORMATS (SNDRV_PCM_FMTBIT_S16_LE \
	| SNDRV_PCM_FMTBIT_S16_BE \
	| SNDRV_PCM_FMTBIT_MU_LAW \
	| SNDRV_PCM_FMTBIT_A_LAW)

#define get_cx2070x_priv(_codec_) ((struct cx2070x_priv *) \
			snd_soc_codec_get_drvdata(codec))

/*
 * ADC/DAC Volume
 *
 * max : 0x00 : 5 dB
 *       ( 1 dB step )
 * min : 0xB6 : -74 dB
 */
static const DECLARE_TLV_DB_SCALE(main_tlv, -7400, 100, 0);

/*
 * Capture Volume
 *
 * max : 0x00 : 12 dB
 *       ( 1.5 dB per step )
 * min : 0x1f : -35 dB
 */
static const DECLARE_TLV_DB_SCALE(line_tlv, -3500, 150, 0);

static const char *const classd_gain_texts[] = {
	"2.8W", "2.6W", "2.5W", "2.4W", "2.3W", "2.2W", "2.1W", "2.0W",
	"1.3W", "1.25W", "1.2W", "1.15W", "1.1W", "1.05W", "1.0W", "0.9W"
};

static const struct soc_enum classd_gain_enum =
SOC_ENUM_SINGLE(CXREG_CLASSD_GAIN, 0, 16, classd_gain_texts);

static const int apply_dsp_change(struct snd_soc_component *codec)
{
	struct cx2070x_priv *cx2070x = snd_soc_component_get_drvdata(codec);
	u16 try_loop = 50;

	mutex_lock(&cx2070x->update_lock);

	snd_soc_component_write(codec, CXREG_DSP_INIT_NEWC,
		      snd_soc_component_read32(codec, CXREG_DSP_INIT_NEWC) | 1);
	for (; try_loop; try_loop--) {
		if (0 == (snd_soc_component_read32(codec, CXREG_DSP_INIT_NEWC) & 1)) {
			mutex_unlock(&cx2070x->update_lock);
			return 0;
		}
		udelay(1);
	}
	mutex_unlock(&cx2070x->update_lock);
	dev_err(codec->dev, "newc timeout\n");

	return -1;
}

static int dsp_put(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct soc_mixer_control *mc =
	    (struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int mask = 1 << mc->shift;

	snd_soc_component_update_bits(codec, reg, mask,
			    ucontrol->value.integer.value[0] ? mask : 0);

	return apply_dsp_change(codec);
}

static const struct snd_kcontrol_new cx2070x_snd_controls[] = {
	SOC_SINGLE_TLV("Line 1 Vol", CXREG_LINE1_GAIN, 0, 32, 0, line_tlv),
	SOC_SINGLE_TLV("Line 2 Vol", CXREG_LINE2_GAIN, 0, 32, 0, line_tlv),
	SOC_SINGLE_TLV("Line 3 Vol", CXREG_LINE3_GAIN, 0, 32, 0, line_tlv),
	SOC_DOUBLE_S8_TLV("Left Speaker Volume", CXREG_DAC1_GAIN, -74, 5,
			  main_tlv),
	SOC_DOUBLE_S8_TLV("Right Speaker Volume", CXREG_DAC2_GAIN, -74, 5,
			  main_tlv),
	SOC_DOUBLE_S8_TLV("Mono Out Volume", CXREG_DAC3_GAIN, -74, 5, main_tlv),
	SOC_DOUBLE_S8_TLV("Left ADC1 Gain", CXREG_ADC1L_GAIN, -74, 5, main_tlv),
	SOC_DOUBLE_S8_TLV("Right ADC1 Gain", CXREG_ADC1R_GAIN, -74, 5,
			  main_tlv),
	SOC_DOUBLE_S8_TLV("Left ADC2 Gain", CXREG_ADC1L_GAIN, -74, 5, main_tlv),
	SOC_DOUBLE_S8_TLV("Right ADC2 Gain", CXREG_ADC1R_GAIN, -74, 5,
			  main_tlv),
	SOC_ENUM("Class-D Gain", classd_gain_enum),
	SOC_SINGLE_EXT("Sidetone Switch", CXREG_DSP_ENDABLE, 7, 7, 0,
		       snd_soc_get_volsw, dsp_put),
	SOC_SINGLE_EXT("Inbound NR Switch", CXREG_DSP_ENDABLE, 7, 5, 0,
		       snd_soc_get_volsw, dsp_put),
	SOC_SINGLE_EXT("Mic AGC Switch", CXREG_DSP_ENDABLE, 7, 4, 0,
		       snd_soc_get_volsw, dsp_put),
	SOC_SINGLE_EXT("Beam forming Switch", CXREG_DSP_ENDABLE, 7, 3, 0,
		       snd_soc_get_volsw, dsp_put),
	SOC_SINGLE_EXT("NR Switch", CXREG_DSP_ENDABLE, 7, 2, 0,
		       snd_soc_get_volsw, dsp_put),
	SOC_SINGLE_EXT("LEC Switch", CXREG_DSP_ENDABLE, 7, 1, 0,
		       snd_soc_get_volsw, dsp_put),
	SOC_SINGLE_EXT("AEC Switch", CXREG_DSP_ENDABLE, 7, 1, 0,
		       snd_soc_get_volsw, dsp_put),
	SOC_SINGLE_EXT("Tone Generator Switch", CXREG_DSP_ENDABLE2, 7, 5, 0,
		       snd_soc_get_volsw, dsp_put),
	SOC_SINGLE_EXT("DRC Switch", CXREG_DSP_ENDABLE2, 7, 2, 0,
		       snd_soc_get_volsw, dsp_put),
	SOC_SINGLE_EXT("EQ Switch", CXREG_DSP_ENDABLE2, 7, 0, 0,
		       snd_soc_get_volsw, dsp_put),
};

static const char *const stream3_mux_txt[] = {
	"Digital 1", "Digital 2", "No input", "USB TX2", "SPDIF"
};

static const struct soc_enum stream3_mux_enum =
SOC_ENUM_SINGLE(CXREG_STREAM3_ROUTE, 3, 5, stream3_mux_txt);

static const struct snd_kcontrol_new stream3_mux =
SOC_DAPM_ENUM("Stream 3 Mux", stream3_mux_enum);

static const char *const stream4_mux_txt[] = {
	"Digital 1", "Digital 2", "USB"
};

static const struct soc_enum stream4_mux_enum =
SOC_ENUM_SINGLE(CXREG_STREAM4_ROUTE, 3, 3, stream4_mux_txt);

static const struct snd_kcontrol_new stream4_mux =
SOC_DAPM_ENUM("Stream 4 Mux", stream4_mux_enum);

static const char *const dsp_input_mux_txt[] = {
	"None", "Stream 1", "Stream 2", "Stream 3", "Stream 4", "Scale Out",
	"Voice Out0", "Voice Out1", "Function Gen", "Mixer1 Out"
};

#define CX2070X_DSP_INPUT_ENUM(_wname, _reg , _mux_enum)  \
	static const struct soc_enum _reg##dsp_input_mux_enum =  \
		SOC_ENUM_SINGLE(_reg, 0, 10, dsp_input_mux_txt);  \
								  \
	static const struct snd_kcontrol_new _mux_enum =	  \
		SOC_DAPM_ENUM(_wname, _reg##dsp_input_mux_enum); \

CX2070X_DSP_INPUT_ENUM("Mix0Input 0 Mux", CXREG_MIX0IN0_SOURCE,
		       mix0in0_input_mux)
    CX2070X_DSP_INPUT_ENUM("Mix0Input 1 Mux", CXREG_MIX0IN1_SOURCE,
		       mix0in1_input_mux)
    CX2070X_DSP_INPUT_ENUM("Mix0Input 2 Mux", CXREG_MIX0IN2_SOURCE,
		       mix0in2_input_mux)
    CX2070X_DSP_INPUT_ENUM("Mix0Input 3 Mux", CXREG_MIX0IN3_SOURCE,
		       mix0in3_input_mux)
    CX2070X_DSP_INPUT_ENUM("Mix1Input 0 Mux", CXREG_MIX1IN0_SOURCE,
		       mix1in0_input_mux)
    CX2070X_DSP_INPUT_ENUM("Mix1Input 1 Mux", CXREG_MIX1IN1_SOURCE,
		       mix1in1_input_mux)
    CX2070X_DSP_INPUT_ENUM("Mix1Input 2 Mux", CXREG_MIX1IN2_SOURCE,
		       mix1in2_input_mux)
    CX2070X_DSP_INPUT_ENUM("Mix1Input 3 Mux", CXREG_MIX1IN3_SOURCE,
		       mix1in3_input_mux)
    CX2070X_DSP_INPUT_ENUM("VoiceIn Mux", CXREG_VOICEIN0_SOURCE, voiice_input_mux)
    CX2070X_DSP_INPUT_ENUM("Stream 5 Mux", CXREG_I2S1OUTIN_SOURCE,
		       stream5_input_mux)
    CX2070X_DSP_INPUT_ENUM("Stream 6 Mux", CXREG_I2S2OUTIN_SOURCE,
		       stream6_input_mux)
    CX2070X_DSP_INPUT_ENUM("Stream 7 Mux", CXREG_DACIN_SOURCE, stream7_input_mux)
    CX2070X_DSP_INPUT_ENUM("Stream 8 Mux", CXREG_DACSUBIN_SOURCE, stream8_input_mux)
    CX2070X_DSP_INPUT_ENUM("Stream 7 Mux", CXREG_USBOUT_SOURCE, stream9_input_mux)

#if 0
static const struct snd_kcontrol_new hp_switch =
SOC_DAPM_SINGLE("Switch", CXREG_OUTPUT_CONTROL, 0, 1, 0);

static const struct snd_kcontrol_new classd_switch =
SOC_DAPM_SINGLE("Switch", CXREG_OUTPUT_CONTROL, 2, 1, 0);

static const struct snd_kcontrol_new lineout_switch =
SOC_DAPM_SINGLE("Switch", CXREG_OUTPUT_CONTROL, 1, 1, 0);
#endif

static const struct snd_kcontrol_new function_gen_switch =
SOC_DAPM_SINGLE("Switch", CXREG_DSP_ENDABLE, 5, 1, 0);

static const struct snd_kcontrol_new strm1_sel_mix[] = {
	SOC_DAPM_SINGLE("Digital Mic Switch", CXREG_DMIC_CONTROL, 6, 1, 1),
	SOC_DAPM_SINGLE("Line 1 Switch", CXREG_INPUT_CONTROL, 0, 1, 0),
	SOC_DAPM_SINGLE("Line 2 Switch", CXREG_INPUT_CONTROL, 1, 1, 0),
	SOC_DAPM_SINGLE("Line 3 Switch", CXREG_INPUT_CONTROL, 2, 1, 0),
};

static const struct snd_kcontrol_new strm2_sel_mix[] = {
	SOC_DAPM_SINGLE("Digital Mic Switch", CXREG_DMIC_CONTROL, 6, 1, 0),
};

static int newc_ev(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol,
		   int nevent)
{
	/* execute the DSP change */
	struct snd_soc_component *codec = snd_soc_dapm_to_component(w->dapm);
	apply_dsp_change(codec);
	return 0;
}

static const struct snd_soc_dapm_widget cx2070x_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("DP1IN", PLAYBACK_STREAM_NAME_1, 0, SND_SOC_NOPM, 0,
			    0),
	SND_SOC_DAPM_AIF_IN("DP2IN", PLAYBACK_STREAM_NAME_2, 0, SND_SOC_NOPM, 0,
			    0),
	SND_SOC_DAPM_AIF_OUT("SPDIFOUT", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("USBOUT", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("HPOUT"),
	SND_SOC_DAPM_OUTPUT("SPKOUT"),
	SND_SOC_DAPM_OUTPUT("MONOOUT"),
	SND_SOC_DAPM_OUTPUT("LINEOUT"),
	SND_SOC_DAPM_AIF_OUT("DP1OUT", CAPTURE_STREAM_NAME_1, 0, SND_SOC_NOPM,
			     0, 0),
	SND_SOC_DAPM_AIF_OUT("DP2OUT", CAPTURE_STREAM_NAME_2, 0, SND_SOC_NOPM,
			     0, 0),
	SND_SOC_DAPM_INPUT("LINEIN1"),
	SND_SOC_DAPM_INPUT("LINEIN2"),
	SND_SOC_DAPM_INPUT("LINEIN3"),
	SND_SOC_DAPM_INPUT("DMICIN"),
	SND_SOC_DAPM_INPUT("MICIN"),
	SND_SOC_DAPM_AIF_IN("USBTX2IN", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SPDIFIN", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("USBIN", NULL, 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_PGA("Digital Mic Enable", CXREG_DMIC_CONTROL, 7, 0, NULL,
			 0),

	SND_SOC_DAPM_MIXER("Stream 1 Mixer", CXREG_DSP_INIT_NEWC, 1, 0,
			   strm1_sel_mix, ARRAY_SIZE(strm1_sel_mix)),
	SND_SOC_DAPM_PGA("Microphone", SND_SOC_NOPM, 0, 0, 0, 0),
	SND_SOC_DAPM_MIXER("Stream 2 Mixer", CXREG_DSP_INIT_NEWC, 2, 0,
			   strm2_sel_mix, ARRAY_SIZE(strm2_sel_mix)),

	SND_SOC_DAPM_MUX("Stream 3 Mux", CXREG_DSP_INIT_NEWC, 3, 0,
			 &stream3_mux),
	SND_SOC_DAPM_MUX("Stream 4 Mux", CXREG_DSP_INIT_NEWC, 4, 0,
			 &stream4_mux),

	SND_SOC_DAPM_SIGGEN("TONE"),
	SND_SOC_DAPM_SWITCH("Function Generator", SND_SOC_NOPM, 0, 0,
			    &function_gen_switch),

	/* playback dsp */
	SND_SOC_DAPM_MUX("Mix0Input 0 Mux", SND_SOC_NOPM, 0, 0,
			 &mix0in0_input_mux),
	SND_SOC_DAPM_MUX("Mix0Input 1 Mux", SND_SOC_NOPM, 0, 0,
			 &mix0in1_input_mux),
	SND_SOC_DAPM_MUX("Mix0Input 2 Mux", SND_SOC_NOPM, 0, 0,
			 &mix0in2_input_mux),
	SND_SOC_DAPM_MUX("Mix0Input 3 Mux", SND_SOC_NOPM, 0, 0,
			 &mix0in3_input_mux),
	SND_SOC_DAPM_MIXER("Mixer 0 Mixer", SND_SOC_NOPM, 0, 0, 0, 0),
	SND_SOC_DAPM_PGA("Playback DSP", SND_SOC_NOPM, 0, 0, 0, 0),
	SND_SOC_DAPM_PGA("Scale Out", SND_SOC_NOPM, 0, 0, 0, 0),
	SND_SOC_DAPM_MUX("Mix1Input 0 Mux", SND_SOC_NOPM, 0, 0,
			 &mix1in0_input_mux),
	SND_SOC_DAPM_MUX("Mix1Input 1 Mux", SND_SOC_NOPM, 0, 0,
			 &mix1in1_input_mux),
	SND_SOC_DAPM_MUX("Mix1Input 2 Mux", SND_SOC_NOPM, 0, 0,
			 &mix1in2_input_mux),
	SND_SOC_DAPM_MUX("Mix1Input 3 Mux", SND_SOC_NOPM, 0, 0,
			 &mix1in3_input_mux),
	SND_SOC_DAPM_MIXER("Mixer 1 Mixer", SND_SOC_NOPM, 0, 0, 0, 0),

	/* voice dsp */
	SND_SOC_DAPM_MUX("VoiceIn Mux", SND_SOC_NOPM, 0, 0, &voiice_input_mux),
	SND_SOC_DAPM_PGA("Voice DSP", SND_SOC_NOPM, 0, 0, 0, 0),
	SND_SOC_DAPM_PGA("Voice Out", SND_SOC_NOPM, 0, 0, 0, 0),

	/* stream 5 */
	SND_SOC_DAPM_MUX("Stream 5 Mux", CXREG_DSP_INIT_NEWC, 5, 0,
			 &stream5_input_mux),

	/* stream 6 */
	SND_SOC_DAPM_MUX("Stream 6 Mux", CXREG_DSP_INIT_NEWC, 6, 0,
			 &stream6_input_mux),

	/* Stream 7 */
	SND_SOC_DAPM_MUX("Stream 7 Mux", CXREG_DSP_INIT_NEWC, 7, 0,
			 &stream7_input_mux),
	/*FIX ME, there is a register to switch output path. */
	SND_SOC_DAPM_PGA("SPDIF Out", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Headphone", CXREG_OUTPUT_CONTROL, 0, 0,
			 NULL, 0),
	SND_SOC_DAPM_PGA("Class D", CXREG_OUTPUT_CONTROL, 2, 0,
			 NULL, 0),
	SND_SOC_DAPM_PGA("Line Out", CXREG_OUTPUT_CONTROL, 1, 0,
			 NULL, 0),

	/* Stream 8 */
	SND_SOC_DAPM_MUX("Stream 8 Mux", CXREG_DSP_INIT, 0, 0,
			 &stream8_input_mux),
	SND_SOC_DAPM_PGA("Mono Out", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Stream 9 */
	SND_SOC_DAPM_MUX("Stream 9 Mux", CXREG_DSP_INIT_NEWC, 1, 0,
			 &stream9_input_mux),
	SND_SOC_DAPM_PGA("USB Out", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_POST("NEWC", newc_ev),
};

#define CX2070X_DSP_MUX_ROUTES(widget)  \
	{widget, "Stream 1", "Stream 1 Mixer"}, \
	{widget, "Stream 2", "Stream 2 Mixer"}, \
	{widget, "Stream 3", "Stream 3 Mux"}, \
	{widget, "Stream 4", "Stream 4 Mux"}, \
	{widget, "Function Gen","Function Generator"}

#define CX2070X_OUTPUT_SOURCE_MUX_ROUTES(_wname) \
	{_wname, "Stream 1", "Stream 1 Mixer"}, \
	{_wname, "Stream 2", "Stream 2 Mixer"}, \
	{_wname, "Stream 3", "Stream 3 Mux"}, \
	{_wname, "Stream 4", "Stream 4 Mux"}, \
	{_wname, "Scale Out", "Scale Out"}, \
	{_wname, "Voice Out0", "Voice Out"}, \
	{_wname, "Function Gen", "Function Generator"}, \
	{_wname, "Mixer1 Out", "Mixer 1 Mixer"} \

static const struct snd_soc_dapm_route cx2070x_routes[] = {
	/* stream 1 */
	{"Digital Mic Enable", NULL, "DMICIN"},
	{"Stream 1 Mixer", "Line 1 Switch", "LINEIN1"},
	{"Stream 1 Mixer", "Line 2 Switch", "LINEIN2"},
	{"Stream 1 Mixer", "Line 3 Switch", "LINEIN3"},
	{"Stream 1 Mixer", "Digital Mic Switch", "Digital Mic Enable"},

	/* stream 2 */
	{"Microphone", NULL, "MICIN"},
	{"Stream 2 Mixer", NULL, "Microphone"},
	{"Stream 2 Mixer", "Digital Mic Switch", "Digital Mic Enable"},

	/* stream 3 */
	{"Stream 3 Mux", "Digital 1", "DP1IN"},
	{"Stream 3 Mux", "Digital 2", "DP2IN"},
	{"Stream 3 Mux", "USB TX2", "USBTX2IN"},
	{"Stream 3 Mux", "SPDIF", "SPDIFIN"},

	/* straem 4 */
	{"Stream 4 Mux", "Digital 1", "DP1IN"},
	{"Stream 4 Mux", "Digital 2", "DP2IN"},
	{"Stream 4 Mux", "USB", "USBIN"},

	/* Function Generator */
	{"Function Generator", "Switch", "TONE"},

	/*Mixer 0 + Playback DSP */
	CX2070X_DSP_MUX_ROUTES("Mix0Input 0 Mux"),
	CX2070X_DSP_MUX_ROUTES("Mix0Input 1 Mux"),
	CX2070X_DSP_MUX_ROUTES("Mix0Input 2 Mux"),
	CX2070X_DSP_MUX_ROUTES("Mix0Input 3 Mux"),
	{"Mixer 0 Mixer", NULL, "Mix0Input 0 Mux"},
	{"Mixer 0 Mixer", NULL, "Mix0Input 1 Mux"},
	{"Mixer 0 Mixer", NULL, "Mix0Input 2 Mux"},
	{"Mixer 0 Mixer", NULL, "Mix0Input 3 Mux"},
	{"Playback DSP", NULL, "Mixer 0 Mixer"},
	{"Scale Out", NULL, "Playback DSP"},

	/*Mixer 1 */
	CX2070X_DSP_MUX_ROUTES("Mix1Input 0 Mux"),
	CX2070X_DSP_MUX_ROUTES("Mix1Input 1 Mux"),
	CX2070X_DSP_MUX_ROUTES("Mix1Input 2 Mux"),
	CX2070X_DSP_MUX_ROUTES("Mix1Input 3 Mux"),
	{"Mixer 1 Mixer", NULL, "Mix1Input 0 Mux"},
	{"Mixer 1 Mixer", NULL, "Mix1Input 1 Mux"},
	{"Mixer 1 Mixer", NULL, "Mix1Input 2 Mux"},
	{"Mixer 1 Mixer", NULL, "Mix1Input 3 Mux"},

	/* Voice Processing */
	CX2070X_DSP_MUX_ROUTES("VoiceIn Mux"),
	{"Voice DSP", NULL, "VoiceIn Mux"},
	{"Voice Out", NULL, "Voice DSP"},

	/* Stream 5 */
	CX2070X_OUTPUT_SOURCE_MUX_ROUTES("Stream 5 Mux"),
	{"DP1OUT", NULL, "Stream 5 Mux"},

	/* Stream 6 */
	CX2070X_OUTPUT_SOURCE_MUX_ROUTES("Stream 6 Mux"),
	{"DP2OUT", NULL, "Stream 6 Mux"},

	/* Stream 7 */
	CX2070X_OUTPUT_SOURCE_MUX_ROUTES("Stream 7 Mux"),
	{"SPDIF Out", NULL, "Stream 7 Mux"},
	{"Headphone", NULL, "Stream 7 Mux"},
	{"Class D", NULL, "Stream 7 Mux"},
	{"Line Out", NULL, "Stream 7 Mux"},

	/* Stream 8 */
	CX2070X_OUTPUT_SOURCE_MUX_ROUTES("Stream 8 Mux"),
	{"Mono Out", NULL, "Stream 8 Mux"},

	/* Stream 9 */
	CX2070X_OUTPUT_SOURCE_MUX_ROUTES("Stream 9 Mux"),
	{"USB Out", NULL, "Stream 9 Mux"},

	/* DAPM Endpoint */
	{"HPOUT", NULL, "Headphone"},
	{"SPKOUT", NULL, "Class D"},
	{"LINEOUT", NULL, "Line Out"},
	{"MONOOUT", NULL, "Mono Out"},
	{"USBOUT", NULL, "USB Out"},
};

static int cx2070x_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *codec = dai->component;
	struct cx2070x_priv *cx2070x = snd_soc_component_get_drvdata(codec);
	struct device *dev = codec->dev;
	u8 val = 0;
	u8 sample_size;
	u32 bit_rate;
	u32 frame_size;
	u32 num_ch = 2;

	/*turn off bit clock output */
	snd_soc_component_update_bits(codec, CXREG_CLOCK_DIVIDER,
			    dai->id ? 0x0f << 4 : 0x0f,
			    dai->id ? 0x0f << 4 : 0xf);

	switch (params_format(params)) {
	case SNDRV_PCM_FMTBIT_A_LAW:
		val |= 0 << 4;
		sample_size = 1;
		break;
	case SNDRV_PCM_FMTBIT_MU_LAW:
		val |= 1 << 4;
		sample_size = 1;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		val |= 2 << 4;
		sample_size = 2;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val |= 3 << 4;
		sample_size = 3;
		break;
	default:
		dev_warn(dev, "Unsupported format %d\n",
			 params_format(params));
		return -EINVAL;
	}

	switch (params_rate(params)) {
	case 8000:
		val |= 0;
		break;
	case 11025:
		val |= 1;
		break;
	case 16000:
		val |= 2;
		break;
	case 22050:
		val |= 3;
		break;
	case 24000:
		val |= 4;
		break;
	case 32000:
		val |= 5;
		break;
	case 44100:
		val |= 6;
		break;
	case 48000:
		val |= 7;
		break;
	case 88200:
		val |= 8;
		break;
	case 96000:
		val |= 9;
		break;
	default:
		dev_warn(dev, "Unsupported sample rate %d\n",
			 params_rate(params));
		return -EINVAL;
	}

	/*update input rate */
	snd_soc_component_update_bits(codec,
			    dai->id ? CXREG_STREAM4_RATE : CXREG_STREAM3_RATE,
			    0x3f, val);

	/*update output rate */
	snd_soc_component_update_bits(codec,
			    dai->id ? CXREG_STREAM6_RATE : CXREG_STREAM5_RATE,
			    0x3f, val);

	/*set bit clock */
	frame_size = (sample_size * 8) * num_ch;
	bit_rate = frame_size * params_rate(params);

	dev_info(dev, "bit rate at %uHz, master = %d\n", bit_rate,
		 cx2070x->master[dai->id]);

	dev_info(dev, "sample size = %d bytes, sample rate = %uHz\n",
		 sample_size, params_rate(params));

	if (dai->id == 0) {
		snd_soc_component_write(codec, CXREG_PORT1_TX_FRAME, frame_size / 8 - 1);
		snd_soc_component_write(codec, CXREG_PORT1_RX_FRAME, frame_size / 8 - 1);
		/*TODO: only I2S mode is implemented. */
		snd_soc_component_write(codec, CXREG_PORT1_TX_SYNC,
			      frame_size / num_ch - 1);
		snd_soc_component_write(codec, CXREG_PORT1_RX_SYNC,
			      frame_size / num_ch - 1);
		val = sample_size - 1;
		val |= val << 2;
		/*TODO : implement PassThru mode */
		/*snd_soc_component_update_bits(codec, CXREG_PORT1_CONTROL2,0x0f,val); */
		snd_soc_component_write(codec, CXREG_PORT1_CONTROL2, val);

	} else {
		snd_soc_component_write(codec, CXREG_PORT2_FRAME, frame_size / 8 - 1);
		/*TODO: only I2S mode is implemented. */
		snd_soc_component_write(codec, CXREG_PORT2_SYNC, frame_size / num_ch - 1);
		val = sample_size - 1;
		/*TODO: implement PassThru mode */
		/*snd_soc_update_bits(codec, CXREG_PORT2_SAMPLE,0x02,val); */
		snd_soc_component_write(codec, CXREG_PORT2_SAMPLE, val);
	}

	bit_rate /= 1000;
	bit_rate *= 1000;

	if (!cx2070x->master[dai->id])
		val = 0xf;
	else {
		switch (bit_rate) {
		case 6144000:
			val = 1;
			break;
		case 3072000:
			val = 2;
			break;
		case 2048000:
			val = 3;
			break;
		case 1536000:
			val = 4;
			break;
		case 1024000:
			val = 5;
			break;
		case 568000:
			val = 6;
			break;
		case 512000:
			val = 7;
			break;
		case 384000:
			val = 7;
			break;
		case 256000:
			val = 9;
			break;
		case 5644000:
			val = 10;
			break;
		case 2822000:
			val = 11;
			break;
		case 1411000:
			val = 12;
			break;
		case 705000:
			val = 13;
			break;
		case 352000:
			val = 13;
			break;
		default:
			dev_warn(dev, "Unsupported bit rate %uHz\n",
				 bit_rate);
			return -EINVAL;
		}
	}

	snd_soc_component_update_bits(codec, CXREG_CLOCK_DIVIDER,
			    dai->id ? 0x0f << 4 : 0x0f,
			    dai->id ? val << 4 : val);

	return 0;
}

static int cx2070x_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_component *codec = dai->component;
	return snd_soc_component_update_bits(codec, CXREG_VOLUME_MUTE, 0x03,
				   mute ? 0x03 : 0);
}

static int cx2070x_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				  unsigned int freq, int dir)
{
	struct snd_soc_component *codec = dai->component;
	struct cx2070x_priv *cx2070x = snd_soc_component_get_drvdata(codec);
	struct device *dev = codec->dev;
	u8 val;

	dev_dbg(dev, "using MCLK at %uHz\n", freq);
	val = snd_soc_component_read32(codec, CXREG_I2S_OPTION);
	val &= ~0x10;
	if (dir == SND_SOC_CLOCK_OUT) {
		switch (freq) {
		case 2048000:
			val |= 0;
			break;
		case 4096000:
			val |= 1;
			break;
		case 5644000:
			val |= 2;
			break;
		case 6144000:
			val |= 3;
			break;
		case 8192000:
			val |= 4;
			break;
		case 11289000:
			val |= 5;
			break;
		case 12288000:
			val |= 6;
			break;
		case 24576000:
			val |= 10;
			break;
		case 22579000:
			val |= 11;
			break;
		default:
			dev_err(dev, "Unsupport MCLK rate %uHz!\n", freq);
			return -EINVAL;
		}
		val |= 0x10;	/*enable MCLK output */
		snd_soc_component_write(codec, CXREG_I2S_OPTION, val);
	} else
		snd_soc_component_write(codec, CXREG_I2S_OPTION, val);

	if (clk_set_rate(cx2070x->mclk, freq)) {
		dev_err(dev, "set clk rate failed\n");
		return -EINVAL;
	}

	cx2070x->sysclk = freq;
	return 0;
}

static int cx2070x_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *codec = dai->component;
	struct cx2070x_priv *cx2070x = snd_soc_component_get_drvdata(codec);
	struct device *dev = codec->dev;
	uint8_t is_pcm = 0;
	uint8_t is_frame_invert = 0;
	uint8_t is_clk_invert = 0;
	uint8_t is_right_j = 0;
	uint8_t is_one_delay = 0;
	uint8_t val;

	if (dai->id > NUM_OF_DAI) {
		dev_err(dev, "Unknown dai configuration,dai->id = %d\n",
			dai->id);
		return -EINVAL;
	}
	cx2070x->master[dai->id] = 0;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		cx2070x->master[dai->id] = 0;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		cx2070x->master[dai->id] = 1;
		break;
	default:
		dev_err(dai->dev, "Unsupported master/slave configuration\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_B:
		/*PCM short frame sync */
		is_pcm = 1;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		/*PCM short frame sync with one cycle delay */
		is_pcm = 1;
		is_one_delay = 1;
		break;
	case SND_SOC_DAIFMT_I2S:
		/*I2S */
		is_one_delay = 1;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		/*I2S right justified */
		is_right_j = 1;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		/*I2S without delay */
		break;
	default:
		dev_err(dev, "Unsupported dai format %d\n",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_NB_IF:
		if (is_pcm) {
			dev_err(dev,
				"Can't support invert frame in PCM mode\n");
			return -EINVAL;
		}
		is_frame_invert = 1;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		is_clk_invert = 1;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		if (is_pcm) {
			dev_err(dev,
				"Can't support invert frame in PCM mode\n");
			return -EINVAL;
		}
		is_frame_invert = 1;
		is_clk_invert = 1;
		break;
	}

	val =
	    (is_one_delay << 7) | (is_right_j << 6) | (is_clk_invert << 3) |
	    (is_clk_invert << 2) | (is_frame_invert << 1) | (is_pcm);
	snd_soc_component_update_bits(codec,
			    dai->id ? CXREG_PORT2_CONTROL : CXREG_PORT1_CONTROL,
			    0xc0, val);
	return 0;
}

static int cx2070x_set_bias_level(struct snd_soc_component *codec,
				  enum snd_soc_bias_level level)
{
	struct cx2070x_priv *cx2070x = snd_soc_component_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(codec) == SND_SOC_BIAS_OFF) {
			regcache_cache_only(cx2070x->regmap, false);
			regcache_sync(cx2070x->regmap);
			/*wake up */
			snd_soc_component_write(codec, CXREG_LOWER_POWER, 0x00);
			msleep(200);
		}
		break;
	case SND_SOC_BIAS_OFF:
		/*deep sleep mode */
		snd_soc_component_write(codec, CXREG_DSP_INIT_NEWC, 1);
		apply_dsp_change(codec);
		snd_soc_component_write(codec, CXREG_LOWER_POWER, 0xe0);
		regcache_cache_only(cx2070x->regmap, true);
		regcache_mark_dirty(cx2070x->regmap);
		break;
	}

	return 0;
}

extern void do_cx_dump(struct snd_soc_component *codec);
static int cx2070x_probe(struct snd_soc_component *codec)
{
	struct cx2070x_priv *cx2070x = snd_soc_component_get_drvdata(codec);
	u8 a1, a2, a3, a4, a5, a6, a7, a8;

	cx2070x->codec = codec;

	a1 = snd_soc_component_read32(codec, CXREG_CHIP_VERSION);
	a2 = snd_soc_component_read32(codec, CXREG_FIRMWARE_VER_HI);
	a3 = snd_soc_component_read32(codec, CXREG_FIRMWARE_VER_LO);
	a4 = snd_soc_component_read32(codec, CXREG_PATCH_VER_HI);
	a5 = snd_soc_component_read32(codec, CXREG_PATCH_VER_LO);
	a6 = snd_soc_component_read32(codec, CXREG_PATCH_HI);
	a7 = snd_soc_component_read32(codec, CXREG_PATCH_MED);
	a8 = snd_soc_component_read32(codec, CXREG_PATCH_LO);

	dev_info(codec->dev,
		 "CX2070%d, Firmware Version %x.%x.%x.%x (%02x.%02x.%02x)\n",
		 a1, a2, a3, a4, a5, a6, a7, a8);

#if 0
	do_cx_dump(codec);
#endif

#ifdef CONFIG_SND_SOC_CX2070X_SYSFS
	cx2070x_sysfs_alloc(cx2070x);
#endif
	return 0;
}

static void cx2070x_remove(struct snd_soc_component *codec)
{
	cx2070x_set_bias_level(codec, SND_SOC_BIAS_OFF);
}

#ifdef CONFIG_PM
static int cx2070x_suspend(struct snd_soc_component *codec)
{
	cx2070x_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int cx2070x_resume(struct snd_soc_component *codec)
{
	cx2070x_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}
#endif

static bool cx2070x_volatile(struct device *dev, unsigned int reg)
{
#if VOLATILE_FOR_ALL
	return 1;
#endif
	switch (reg) {
	case CXREG_ABCODE:
	case CXREG_UPDATE_CTR:
	case CXREG_DSP_INIT_NEWC:
		return 1;
	default:
		return 0;
	}
}

static const struct snd_soc_dai_ops cx2070x_dai_ops = {
	.hw_params = cx2070x_hw_params,
	.digital_mute = cx2070x_mute,
	.set_fmt = cx2070x_set_dai_fmt,
	.set_sysclk = cx2070x_set_dai_sysclk,
};

static struct snd_soc_dai_driver cx2070x_dai[] = {
	{
	 .name = DAI_DP1_NAME,
	 .id	= 1,
	 .playback = {
		      .stream_name = PLAYBACK_STREAM_NAME_1,
		      .channels_min = 1,
		      .channels_max = 2,
		      .rates = CX2070X_RATES,
		      .formats = CX2070X_FORMATS,
		      },
	 .capture = {
		     .stream_name = CAPTURE_STREAM_NAME_1,
		     .channels_min = 1,
		     .channels_max = 2,
		     .rates = CX2070X_RATES,
		     .formats = CX2070X_FORMATS,
		     },
	 .ops = &cx2070x_dai_ops,
	 .symmetric_rates = 1,
	 },
	{
	 .name = DAI_DP2_NAME,
	 .id	= 2,
	 .playback = {
		      .stream_name = PLAYBACK_STREAM_NAME_2,
		      .channels_min = 1,
		      .channels_max = 2,
		      .rates = CX2070X_RATES,
		      .formats = CX2070X_FORMATS,
		      },
	 .capture = {
		     .stream_name = CAPTURE_STREAM_NAME_2,
		     .channels_min = 1,
		     .channels_max = 2,
		     .rates = CX2070X_RATES,
		     .formats = CX2070X_FORMATS,
		     },
	 .ops = &cx2070x_dai_ops,
	 .symmetric_rates = 1,
	 }
};

static struct snd_soc_component_driver cx2070x_driver = {
	.probe = cx2070x_probe,
	.remove = cx2070x_remove,
#ifdef CONFIG_PM
	.suspend = cx2070x_suspend,
	.resume = cx2070x_resume,
#endif
	.set_bias_level = cx2070x_set_bias_level,
	.controls = cx2070x_snd_controls,
	.num_controls = ARRAY_SIZE(cx2070x_snd_controls),
	.dapm_widgets = cx2070x_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cx2070x_dapm_widgets),
	.dapm_routes = cx2070x_routes,
	.num_dapm_routes = ARRAY_SIZE(cx2070x_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config cx2070x_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = CX2070X_REG_MAX,
	.volatile_reg = cx2070x_volatile,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = cx2070x_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cx2070x_reg_defaults),
};

/*
 * Write all the default values from cx2070x_reg_defaults[] array into the
 * cx2070x registers, to make sure we always start with the sane registers
 * values as stated in the datasheet.
 */
static void cx2070x_fill_defaults(struct i2c_client *client)
{
	struct cx2070x_priv *cx2070x = i2c_get_clientdata(client);
	int i, ret, val, index;

	for (i = 0; i < ARRAY_SIZE(cx2070x_reg_defaults); i++) {
		val = cx2070x_reg_defaults[i].def;
		index = cx2070x_reg_defaults[i].reg;
		ret = regmap_write(cx2070x->regmap, index, val);
		if (ret)
			dev_err(&client->dev,
				"%s: error %d setting reg 0x%02x to 0x%04x\n",
				__func__, ret, index, val);
	}
	regmap_read(cx2070x->regmap, CXREG_DSP_INIT_NEWC, &val);
	regmap_write(cx2070x->regmap, CXREG_DSP_INIT_NEWC, val | 1);
	for (i = 0; i < 50; i++) {
		regmap_read(cx2070x->regmap, CXREG_DSP_INIT_NEWC, &val);
		if (0 == (val & 1))
			return;
		udelay(1);
	}
}

static int cx2070x_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct cx2070x_priv *cx2070x;
	int reg, ret = 0;

	cx2070x = devm_kzalloc(&client->dev, sizeof(*cx2070x), GFP_KERNEL);
	if (!cx2070x)
		return -ENOMEM;

	i2c_set_clientdata(client, cx2070x);

	cx2070x->regmap = devm_regmap_init_i2c(client, &cx2070x_regmap);
	if (IS_ERR(cx2070x->regmap)) {
		ret = PTR_ERR(cx2070x->regmap);
		dev_err(&client->dev, "Failed to allocate regmap: %d\n", ret);
		goto disable_regs;
	}

	cx2070x->mclk = devm_clk_get(&client->dev, NULL);
	if (IS_ERR(cx2070x->mclk)) {
		ret = PTR_ERR(cx2070x->mclk);
		/* Defer the probe to see if the clk will be provided later */
		if (ret == -ENOENT)
			ret = -EPROBE_DEFER;

		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev, "Failed to get mclock: %d\n",
				ret);
		goto disable_regs;
	}

	ret = clk_prepare_enable(cx2070x->mclk);
	if (ret) {
		dev_err(&client->dev, "Error enabling clock %d\n", ret);
		goto disable_regs;
	}

	/* Need 8 clocks before I2C accesses */
	udelay(20);

	/* read chip information */
	ret = regmap_read(cx2070x->regmap, CXREG_CHIP_VERSION, &reg);
	if (ret) {
		dev_err(&client->dev, "Error reading chip version %d\n", ret);
		goto disable_clk;
	}

	cx2070x->version = ret;
	cx2070x->num_dai = NUM_OF_DAI;
	cx2070x->cx_i2c = client;
	cx2070x->dev = &client->dev;
	cx2070x->codec_drv = &cx2070x_driver;
	cx2070x->dai_drv = cx2070x_dai;
	mutex_init(&cx2070x->update_lock);

	/* Ensure cx2070x will start with sane register values */
//      cx2070x_fill_defaults(client);

	ret =
		devm_snd_soc_register_component(&client->dev, &cx2070x_driver, cx2070x_dai,
				   NUM_OF_DAI);
	if (ret)
		goto disable_clk;

	dev_info(&client->dev, "codec driver version %d.%d.%d.%d\n",
		 (u8) ((CX2070X_DRIVER_VERSION) >> 24),
		 (u8) ((CX2070X_DRIVER_VERSION) >> 16),
		 (u8) ((CX2070X_DRIVER_VERSION) >> 8),
		 (u8) ((CX2070X_DRIVER_VERSION)));

	return 0;

disable_clk:
	clk_disable_unprepare(cx2070x->mclk);

disable_regs:

	return ret;
}

static int cx2070x_i2c_remove(struct i2c_client *client)
{
	struct cx2070x_priv *cx2070x = i2c_get_clientdata(client);

	clk_disable_unprepare(cx2070x->mclk);
	return 0;
}

static const struct i2c_device_id cx2070x_id[] = {
	{"cx2070x", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, cx2070x_id);

static const struct of_device_id cx2070x_dt_ids[] = {
	{.compatible = "conexant,cx2070x",},
	{}
};

MODULE_DEVICE_TABLE(of, cx2070x_dt_ids);

static struct i2c_driver cx2070x_i2c_driver = {
	.driver = {
		   .name = "cx2070x",
		   .of_match_table = cx2070x_dt_ids,
		   },
	.probe = cx2070x_i2c_probe,
	.remove = cx2070x_i2c_remove,
	.id_table = cx2070x_id,
};

module_i2c_driver(cx2070x_i2c_driver);

MODULE_DESCRIPTION("ASoC cx2070x Codec Driver");
MODULE_AUTHOR("Oleksandr Zhadan <www.arcturusnetworks.com>");
MODULE_LICENSE("GPL");
