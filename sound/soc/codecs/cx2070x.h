/*
* ALSA SoC CX2070X codec driver
*
* Copyright:   (C) 2009/2010 Conexant Systems
* Copyright:   (C) 2017-2021 Arcturus Networks Inc.
*                  by Oleksandr Zhadan
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
*************************************************************************
*  2019.03: fsl,imx-audio-cx2070x support added
*  2017.08: Linux 4.x kernel support added
*  Based on:
*    Modified Date:  12/02/13
*    File Version:   3.8.0.21
*************************************************************************
*/
#ifndef _CX2070X_H_
#define _CX2070X_H_

#define CONFIG_SND_SOC_CX2070X_SYSFS 1
#define CX2070X_SPI_WRITE_FLAG 0x80

// #define SINGLE_DAI_SUPPORT

#ifdef  SINGLE_DAI_SUPPORT
#define NUM_OF_DAI			1
#define DAI_DP1_NAME			"cx2070x"
#define CAPTURE_STREAM_NAME_1		"Capture"
#define PLAYBACK_STREAM_NAME_1		"Playback"
#else
#define NUM_OF_DAI			2
#define DAI_DP1_NAME			"cx2070x"
#define DAI_DP2_NAME			"cx2070x-dp2"
#define CAPTURE_STREAM_NAME_1		"Capture"
#define PLAYBACK_STREAM_NAME_1		"Playback"
#define CAPTURE_STREAM_NAME_2		"Capture-dp2"
#define  PLAYBACK_STREAM_NAME_2		"Playback-dp2"
#endif

#define CX2070X_I2C_DRIVER_NAME		"cx2070x"
#define CX2070X_SPI_DRIVER_NAME		"cx2070x"
#define CX2070X_FIRMWARE_FILENAME	"cnxt/cx2070x.fw"
#define CX2070X_REG_MAX			0x1600
#define AUDDRV_VERSION(major0, major1, minor, build) \
       ((major0) << 24 |(major1) << 16 | (minor) << 8 | (build))

#define CX2070X_LOADER_TIMEOUT		50	/*50 ms */
#define CX2070X_SW_RESET_TIMEOUT	50	/*50 ms */
#define CX2070X_MEMORY_UPDATE_TIMEOUT	30	/*5 times */
#define CX2070X_MAX_MEM_BUF		0x100

#define CXREG_USB_LOCAL_VOLUME	0x004E
#define CXREG_UPDATE_AL		0x02FC
#define CXREG_UPDATE_AM		0x02FD
#define CXREG_UPDATE_AH		0x02FE
#define CXREG_UPDATE_LEN	0x02FF
#define CXREG_UPDATE_BUFF	0x0300
#define CXREG_UPDATE_CTR	0x0400
#define CXREG_CLOCK_DIVIDER	0x0F50
#define CXREG_PORT1_CONTROL	0x0f51
#define CXREG_PORT1_TX_FRAME	0x0f52
#define CXREG_PORT1_RX_FRAME	0x0f53
#define CXREG_PORT1_TX_SYNC	0x0f54
#define CXREG_PORT1_RX_SYNC	0x0f55
#define CXREG_PORT1_CONTROL2	0x0f56
#define CXREG_PORT1_RX_SLOT1	0x0f57
#define CXREG_PORT1_RX_SLOT2	0x0f58
#define CXREG_PORT1_RX_SLOT3	0x0f59
#define CXREG_PORT1_TX_SLOT1	0x0f5A
#define CXREG_PORT1_TX_SLOT2	0x0f5B
#define CXREG_PORT1_TX_SLOT3	0x0f5C
#define CXREG_PORT1_DELAY	0x0F5D
#define CXREG_PORT2_CONTROL	0x0f5e
#define CXREG_PORT2_FRAME	0x0f5f
#define CXREG_PORT2_SYNC	0x0f60
#define CXREG_PORT2_SAMPLE	0x0f61
#define CXREG_PORT2_RX_SLOT1	0x0f62
#define CXREG_PORT2_RX_SLOT2	0x0f63
#define CXREG_PORT2_TX_SLOT1	0x0f65
#define CXREG_PORT2_TX_SLOT2	0x0f66
#define CXREG_ABCODE		0x1000
#define CXREG_FIRMWARE_VER_LO	0x1001
#define CXREG_FIRMWARE_VER_HI	0x1002
#define CXREG_PATCH_VER_LO	0x1003
#define CXREG_PATCH_VER_HI	0x1004
#define CXREG_CHIP_VERSION	0x1005
#define CXREG_RELEASE_TYPE	0x1006
#define CXREG_DAC1_GAIN		0x100d
#define CXREG_DAC2_GAIN		0x100e
#define CXREG_DSPDAC		0x1010
#define CXREG_CLASSD_GAIN	0x1011
#define CXREG_DAC3_GAIN		0x1012
#define CXREG_ADC1L_GAIN	0X1013
#define CXREG_ADC1R_GAIN	0X1014
#define CXREG_ADC2L_GAIN	0X1015
#define CXREG_ADC2R_GAIN	0X1016
#define CXREG_VOLUME_MUTE	0x1018
#define CXREG_OUTPUT_CONTROL	0x1019
#define CXREG_INPUT_CONTROL	0x101a
#define CXREG_LINE1_GAIN	0x101b
#define CXREG_LINE2_GAIN	0x101c
#define CXREG_LINE3_GAIN	0x101d
#define CXREG_STREAM3_RATE	0X116D
#define CXREG_STREAM3_ROUTE	0X116E
#define CXREG_STREAM4_RATE	0X116F
#define CXREG_STREAM4_ROUTE	0X1170
#define CXREG_STREAM5_RATE	0x1171
#define CXREG_STREAM6_RATE	0x1172
#define CXREG_DSP_ENDABLE	0x117a
#define CXREG_DSP_ENDABLE2	0x117b
#define CXREG_DSP_INIT		0x117c
#define CXREG_DSP_INIT_NEWC	0x117d
#define CXREG_LOWER_POWER	0x117e
#define CXREG_DACIN_SOURCE	0x117f
#define CXREG_DACSUBIN_SOURCE	0x1180
#define CXREG_I2S1OUTIN_SOURCE	0X1181
#define CXREG_USBOUT_SOURCE	0x1183
#define CXREG_MIX0IN0_SOURCE	0x1184
#define CXREG_MIX0IN1_SOURCE	0x1185
#define CXREG_MIX0IN2_SOURCE	0x1186
#define CXREG_MIX0IN3_SOURCE	0x1187
#define CXREG_MIX1IN0_SOURCE	0x1188
#define CXREG_MIX1IN1_SOURCE	0x1189
#define CXREG_MIX1IN2_SOURCE	0x118a
#define CXREG_MIX1IN3_SOURCE	0x118b
#define CXREG_VOICEIN0_SOURCE	0x118c
#define CXREG_I2S2OUTIN_SOURCE	0X118e
#define CXREG_DMIC_CONTROL	0x1247
#define CXREG_I2S_OPTION	0x1249
#define CXREG_PATCH_HI          0x1584
#define CXREG_PATCH_MED         0x1585
#define CXREG_PATCH_LO          0x1586

/* codec private data*/
struct cx2070x_priv {
	struct regmap *regmap;
	unsigned int sysclk;
	int is_clk_gated[NUM_OF_DAI];
	int master[NUM_OF_DAI];
	struct device *dev;
	const struct snd_soc_component_driver *codec_drv;
	struct snd_soc_dai_driver *dai_drv;
	int num_dai;
	struct mutex update_lock;
	struct snd_soc_component *codec;
	struct i2c_client *cx_i2c;
	int version;
	struct clk *mclk;
#ifdef CONFIG_SND_SOC_CX2070X_SYSFS
	int reg;
	int val;
	u8 l;
	u8 m;
	u8 h;
	u8 s;
#endif
};

#ifdef CONFIG_SND_SOC_CX2070X_FW_PATCH
struct cx2070x_rom_data {
	u32 len;
	u32 addr;
	u8 data[1];
};

struct cx2070x_rom {
	char desc[24];
	char c_open_bracket;
	char version[5];
	char c_close_bracket;
	char c_eof;
	u32 file_len;
	u32 loader_addr;
	u32 loader_len;
	u32 cpx_addr;
	u32 cpx_len;
	u32 spx_addr;
	u32 spx_len;
	struct cx2070x_rom_data data[1];
};
#endif

#endif /* _CX2070X_H_ */
