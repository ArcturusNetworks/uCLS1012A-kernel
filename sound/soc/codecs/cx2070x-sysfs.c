/***************************************************************************
 *                                                                         *
 *   cx2070x-sysfs.c Conexant CX2070x speakers-on chip (SPoC)              *
 *                 sysfs control interface driver                          *
 *                                                                         *
 *   Copyright (c) 2011-2021 Arcturus Networks Inc.                        *
 *                 by Oleksandr Zhadan <www.ArcturusNetworks.com>          *
 *                                                                         *
 ***************************************************************************/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon-sysfs.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/sysfs.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <asm/delay.h>

#include "cx2070x.h"

#define to_dev_attr(_attr) container_of(_attr, struct device_attribute, attr)

#define CX2070X_EEPROM_SIZE		0x20000	/* in bytes (1Mbit) */
#define CX2070X_EEPROM_PAGE_SIZE	256
#define CX2070X_MAX_STORE_LOOPS		1000	/* maximum retries for write (store process) */

void cx2070x_i2c_indirect_set(struct i2c_client *client, u32 addr, u8 len)
{
	struct cx2070x_priv *data = i2c_get_clientdata(client);
	u8 l, m, h, s;
	static int initialized = 0;

	if (!initialized) {
		initialized = 1;
		data->l = snd_soc_component_read32(data->codec, CXREG_UPDATE_AL);
		data->m = snd_soc_component_read32(data->codec, CXREG_UPDATE_AM);
		data->h = snd_soc_component_read32(data->codec, CXREG_UPDATE_AH);
		data->s = snd_soc_component_read32(data->codec, CXREG_UPDATE_LEN);
	}

	l = addr & 0xFF;;
	m = (addr >> 8) & 0xFF;
	h = (addr >> 16) & 0xFF;
	s = len;

	if (data->l != l) {
		snd_soc_component_write(data->codec, CXREG_UPDATE_AL, l);
		data->l = l;
	}
	if (data->m != m) {
		snd_soc_component_write(data->codec, CXREG_UPDATE_AM, m);
		data->m = m;
	}
	if (data->h != h) {
		snd_soc_component_write(data->codec, CXREG_UPDATE_AH, h);
		data->h = h;
	}
	if (data->s != s) {
		snd_soc_component_write(data->codec, CXREG_UPDATE_LEN, s);
		data->s = s;
	}
}

EXPORT_SYMBOL(cx2070x_i2c_indirect_set);

void cx2070x_i2c_indirect_data(struct i2c_client *client, u8 * data, u32 size)
{
	int i;
	u16 addr = 0x300;	/* UpdateBuff 0x0300 – 0x03FF */
	struct cx2070x_priv *cx = i2c_get_clientdata(client);

	if (size > 256)
		return;

	for (i = 0; i < size; i++)
		snd_soc_component_write(cx->codec, (addr + i), data[i]);
}

EXPORT_SYMBOL(cx2070x_i2c_indirect_data);

/******************************************* SYSFS entry point ****************/

static ssize_t cx2070x_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct sensor_device_attribute *psa = to_sensor_dev_attr(attr);
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	return sprintf(buf, "0x%x=0x%02x\n", psa->index,
		       (unsigned char)snd_soc_component_read32(data->codec, psa->index));
}

static ssize_t cx2070x_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct sensor_device_attribute *psa = to_sensor_dev_attr(attr);
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 0);
	int loop_cnt = CX2070X_MAX_STORE_LOOPS;

	snd_soc_component_write(data->codec, psa->index, val & 0xff);

	if (psa->index == 0x400) {	/* UpdateCtl 0x0400 */
		unsigned char ret;
		do {
			ret = snd_soc_component_read32(data->codec, psa->index);
			udelay(100);
		} while (ret & 0x80 && loop_cnt--);
	}
	if (loop_cnt == 0) {
		count = -1;	/* error return */
	}
	return count;
}

static ssize_t cx2070x_regnum_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	return sprintf(buf, "0x%x\n", data->reg);
}

static ssize_t cx2070x_regnum_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	if (strcmp(buf, "\n")) {
		data->reg = simple_strtoul(buf, NULL, 16);
	}
	return count;
}

static ssize_t cx2070x_regval_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	data->val = (snd_soc_component_read32(data->codec, data->reg) & 0xff);

	return sprintf(buf, "0x%x=0x%x\n", data->reg, data->val);
}

static ssize_t cx2070x_regval_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	if (strcmp(buf, "\n")) {
		data->val = simple_strtoul(buf, NULL, 16);
		snd_soc_component_write(data->codec, data->reg, data->val & 0xff);
	}
	return count;
}

static ssize_t cx2070x_reg_read(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	data->val = (snd_soc_component_read32(data->codec, data->reg) & 0xff);

	return sprintf(buf, "%d\n", data->val);
}

#define TOLOWER(x) ((x) | 0x20)

static ssize_t cx2070x_reg_write(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);
	char reg_buf[16];
	char val_buf[16];
	char *l_buf = (char *)buf;
	int i, j;

	for (i = 0; i < count; i++, l_buf++) {
		if (*l_buf != ' ' && *l_buf != '\n' && *l_buf != ',')
			break;
	}
	if (i == count)
		goto done;

	for (j = 0; i < count; i++, l_buf++, j++) {
		if (*l_buf == ' ' || *l_buf == '\n' || *l_buf == ',') {
			reg_buf[j] = 0;
			if (reg_buf[0] == '0' && TOLOWER(reg_buf[1]) == 'x') {
				data->reg = simple_strtoul(reg_buf, NULL, 16);
			} else {
				data->reg = simple_strtoul(reg_buf, NULL, 10);
			}
			break;
		}
		reg_buf[j] = *l_buf;
	}
	if (i == count)
		goto done;

	for (; i < count; i++, l_buf++) {
		if (*l_buf != ' ' && *l_buf != '\n' && *l_buf != ',')
			break;
	}
	if (i == count)
		goto done;

	for (j = 0; i < count; i++, l_buf++, j++) {
		if (*l_buf == ' ' || *l_buf == '\n' || *l_buf == ',') {
			val_buf[j] = 0;
			if (val_buf[0] == '0' && TOLOWER(val_buf[1]) == 'x') {
				data->val = simple_strtoul(val_buf, NULL, 16);
			} else {
				data->val = simple_strtoul(val_buf, NULL, 10);
			}
			break;
		}
		val_buf[j] = *l_buf;
	}

	pr_debug("%s: R0x%x=0x%x\n", __func__, data->reg, data->val);

	snd_soc_component_write(data->codec, data->reg & 0xffff, data->val & 0xff);

done:
	return count;
}

static ssize_t cx2070x_indirect_regs_get(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);
	u8 l, m, h, s;

	l = snd_soc_component_read32(data->codec, CXREG_UPDATE_AL);	/* UpdateAL[7:0] 0x02fc */
	m = snd_soc_component_read32(data->codec, CXREG_UPDATE_AM);	/* UpdateAM[7:0] 0x02fd */
	h = snd_soc_component_read32(data->codec, CXREG_UPDATE_AH);	/* UpdateAH[7:0] 0x02fe */
	s = snd_soc_component_read32(data->codec, CXREG_UPDATE_LEN);	/* UpdateLen[7:0] 0x02ff */

	return sprintf(buf, "0x%02x%02x%02x %d\n", h, m, l, s);
}

static ssize_t cx2070x_indirect_regs_set(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);
	char reg_buf[16];
	char val_buf[16];
	char *l_buf = (char *)buf;
	int i, j;

	data->reg = 0;
	data->val = 0;

	for (i = 0; i < count; i++, l_buf++) {
		if (*l_buf != ' ' && *l_buf != '\n')
			break;
	}
	if (i == count)
		goto done;

	for (j = 0; i < count; i++, l_buf++, j++) {
		if (*l_buf == ' ' || *l_buf == '\n') {
			reg_buf[j] = 0;
			if (reg_buf[0] == '0' && TOLOWER(reg_buf[1]) == 'x') {
				data->reg = simple_strtoul(reg_buf, NULL, 16);
			} else {
				data->reg = simple_strtoul(reg_buf, NULL, 10);
			}
			break;
		}
		reg_buf[j] = *l_buf;
	}
	if (i == count)
		goto done;

	for (; i < count; i++, l_buf++) {
		if (*l_buf != ' ' && *l_buf != '\n')
			break;
	}
	if (i == count)
		goto done;

	for (j = 0; i < count; i++, l_buf++, j++) {
		if (*l_buf == ' ' || *l_buf == '\n') {
			val_buf[j] = 0;
			if (val_buf[0] == '0' && TOLOWER(val_buf[1]) == 'x') {
				data->val = simple_strtoul(val_buf, NULL, 16);
			} else {
				data->val = simple_strtoul(val_buf, NULL, 10);
			}
			break;
		}
		val_buf[j] = *l_buf;
	}
done:
	pr_debug("%s: (%s)[0x%x] (%s)[0x%x]\n", __func__, reg_buf, data->reg,
		 val_buf, data->val);

	cx2070x_i2c_indirect_set(client, data->reg & 0xffffff,
				 data->val & 0xff);

	return count;
}

static ssize_t patchVersion(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);
	u8 a1, a6, a7, a8;

	a1 = snd_soc_component_read32(data->codec, 0x1005);	/* Chip[7:0] 0x1005 */

	a6 = snd_soc_component_read32(data->codec, 0x1584);	/* Patch_HI [7:0] 0x1584 */
	a7 = snd_soc_component_read32(data->codec, 0x1585);	/* Patch_MED [7:0] 0x1585 */
	a8 = snd_soc_component_read32(data->codec, 0x1586);	/* Patch_LO [7:0] 0x1586 */

	return sprintf(buf, "Cx2070%d FW Patch Version: %02x.%02x.%02x\n", a1,
		       a6, a7, a8);
}

static ssize_t fwVersion(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);
	u8 a1, a2, a3, a4, a5, a6, a7, a8;

	a1 = snd_soc_component_read32(data->codec, 0x1005);	/* Chip[7:0] 0x1005 */

	a2 = snd_soc_component_read32(data->codec, 0x1002);	/* FV_HI [7:0] 0x1002 */
	a3 = snd_soc_component_read32(data->codec, 0x1001);	/* FV_LO [7:0] 0x1001 */
	a4 = snd_soc_component_read32(data->codec, 0x1004);	/* VV_HI [7:0] 0x1003 */
	a5 = snd_soc_component_read32(data->codec, 0x1003);	/* VV_LO [7:0] 0x1003 */

	a6 = snd_soc_component_read32(data->codec, 0x1584);	/* Patch_HI [7:0] 0x1584 */
	a7 = snd_soc_component_read32(data->codec, 0x1585);	/* Patch_MED [7:0] 0x1585 */
	a8 = snd_soc_component_read32(data->codec, 0x1586);	/* Patch_LO [7:0] 0x1586 */

	return sprintf(buf,
		       "Cx2070%d FW Version: %02x.%02x.%02x.%02x (%02x.%02x.%02x)\n",
		       a1, a2, a3, a4, a5, a6, a7, a8);
}

void do_cx_dump(struct snd_soc_component *codec);
void do_cx_dump(struct snd_soc_component *codec)
{
	u8 a1, a2, a3, a4, a5, a6, a7, a8;
	int i;

	for (i = 0; i < CX2070X_REG_MAX;) {
		a1 = snd_soc_component_read32(codec, i++);
		a2 = snd_soc_component_read32(codec, i++);
		a3 = snd_soc_component_read32(codec, i++);
		a4 = snd_soc_component_read32(codec, i++);
		a5 = snd_soc_component_read32(codec, i++);
		a6 = snd_soc_component_read32(codec, i++);
		a7 = snd_soc_component_read32(codec, i++);
		a8 = snd_soc_component_read32(codec, i++);
		pr_crit("0x%04x :  %02x %02x %02x %02x %02x %02x %02x %02x\n", (i - 8), a1, a2, a3,
		       a4, a5, a6, a7, a8);
	}
}

static ssize_t cxregdump(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);
	u8 a1, a2, a3, a4, a5, a6, a7, a8;
#if 0
	int i, j;
	unsigned char ret;
	int loop_cnt = CX2070X_MAX_STORE_LOOPS;
#endif

	do_cx_dump(data->codec);

#if 0				/* show contains on EEPROM */
	printk("\nEEPROM: 16K Bytes\n");
	for (j = 0; j < 16 * 1024; j += 256) {
		cx2070x_i2c_indirect_set(client, j, 255);	/* addr */
		snd_soc_component_write(data->codec, 0x400, 0x82);	/* read EEPROM */
		ret = snd_soc_component_read32(data->codec, 0x400);
		loop_cnt = CX2070X_MAX_STORE_LOOPS;
		do {
			ret = snd_soc_component_read32(data->codec, 0x0400);
			udelay(100);
		} while (ret & 0x80 && loop_cnt--);
		printk("0x%04x:\n", j);
		for (i = 0; i < 256;) {
			a1 = snd_soc_component_read32(data->codec, (0x300 + i++));
			a2 = snd_soc_component_read32(data->codec, (0x300 + i++));
			a3 = snd_soc_component_read32(data->codec, (0x300 + i++));
			a4 = snd_soc_component_read32(data->codec, (0x300 + i++));
			a5 = snd_soc_component_read32(data->codec, (0x300 + i++));
			a6 = snd_soc_component_read32(data->codec, (0x300 + i++));
			a7 = snd_soc_component_read32(data->codec, (0x300 + i++));
			a8 = snd_soc_component_read32(data->codec, (0x300 + i++));
			printk(" %02x %02x %02x %02x %02x %02x %02x %02x\n", a1,
			       a2, a3, a4, a5, a6, a7, a8);
		}
	}
#endif
	a1 = snd_soc_component_read32(data->codec, 0x1005);	/* Chip[7:0] 0x1005 */
	a2 = snd_soc_component_read32(data->codec, 0x1002);	/* FV_HI [7:0] 0x1002 */
	a3 = snd_soc_component_read32(data->codec, 0x1001);	/* FV_LO [7:0] 0x1001 */
	a4 = snd_soc_component_read32(data->codec, 0x1004);	/* VV_HI [7:0] 0x1003 */
	a5 = snd_soc_component_read32(data->codec, 0x1003);	/* VV_LO [7:0] 0x1003 */
	a6 = snd_soc_component_read32(data->codec, 0x1584);	/* Patch_HI [7:0] 0x1584 */
	a7 = snd_soc_component_read32(data->codec, 0x1585);	/* Patch_MED [7:0] 0x1585 */
	a8 = snd_soc_component_read32(data->codec, 0x1586);	/* Patch_LO [7:0] 0x1586 */

	return sprintf(buf,
		       "Cx2070%d FW Version: %02x.%02x.%02x.%02x (%02x.%02x.%02x)\n",
		       a1, a2, a3, a4, a5, a6, a7, a8);
}

static ssize_t cx2070x_aec_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	data->val = (snd_soc_component_read32(data->codec, CXREG_DSP_ENDABLE) & 0x01);

	return sprintf(buf, "%d\n", data->val);
}

static ssize_t cx2070x_aec_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	data->val = simple_strtoul(buf, NULL, 16) & 0xFF;
	if (data->val)
		data->val = snd_soc_component_read32(data->codec, CXREG_DSP_ENDABLE) | 0x01;
	else
		data->val =
		    snd_soc_component_read32(data->codec, CXREG_DSP_ENDABLE) & ~0x01;
	snd_soc_component_write(data->codec, CXREG_DSP_ENDABLE, data->val & 0xff);
	snd_soc_component_write(data->codec, CXREG_DSP_INIT_NEWC,
		      (snd_soc_component_read32(data->codec, CXREG_DSP_INIT_NEWC) | 0x01));

	return count;
}

static ssize_t cx2070x_nr_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	data->val = !!(snd_soc_component_read32(data->codec, CXREG_DSP_ENDABLE) & 0x04);

	return sprintf(buf, "%d\n", data->val);
}

static ssize_t cx2070x_nr_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	data->val = simple_strtoul(buf, NULL, 16) & 0xFF;
	if (data->val)
		data->val = snd_soc_component_read32(data->codec, CXREG_DSP_ENDABLE) | 0x04;
	else
		data->val =
		    snd_soc_component_read32(data->codec, CXREG_DSP_ENDABLE) & ~0x04;
	snd_soc_component_write(data->codec, CXREG_DSP_ENDABLE, data->val & 0xff);
	snd_soc_component_write(data->codec, CXREG_DSP_INIT_NEWC,
		      (snd_soc_component_read32(data->codec, CXREG_DSP_INIT_NEWC) | 0x01));

	return count;
}

#define CX2070X_REG_MUTE                0x1018	/* Volume Mutes 0x1018 */
static ssize_t cx2070x_in_mute_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	data->val = (snd_soc_component_read32(data->codec, CX2070X_REG_MUTE) & 0x78) >> 3;

	return sprintf(buf, "%d\n", data->val);
}

static ssize_t cx2070x_out_mute_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	data->val = snd_soc_component_read32(data->codec, CX2070X_REG_MUTE) & 0x07;

	return sprintf(buf, "%d\n", data->val);
}

/* input mute - 4 bits Mute [6] - Mute [3] */
static ssize_t cx2070x_in_mute_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	data->val = (simple_strtoul(buf, NULL, 16) & 0x0F) << 3;
	data->val |=
	    (snd_soc_component_read32(data->codec, CX2070X_REG_MUTE) & ~(0x0F << 3));

	snd_soc_component_write(data->codec, CX2070X_REG_MUTE, data->val & 0xff);

	return count;
}

static ssize_t cx2070x_out_mute_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	data->val =
	    (snd_soc_component_read32(data->codec, CX2070X_REG_MUTE) & ~0x07) |
	    (simple_strtoul(buf, NULL, 16) & 0x07);
	snd_soc_component_write(data->codec, CX2070X_REG_MUTE, data->val & 0xff);

	return count;
}

/* Side Tone Gain Low 0x1150, High 0x1151 -> 16bit value from 0(no side tone) to 0x7fff(full side tone)*/
static ssize_t cx2070x_sidetone_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);
	u8 l, h;

	l = snd_soc_component_read32(data->codec, 0x1150);	/* Side Tone Gain Low 0x1150 */
	h = snd_soc_component_read32(data->codec, 0x1151);	/* Side Tone Gain High 0x1151 */

	return sprintf(buf, "0x%02x%02x\n", h, l);
}

static ssize_t cx2070x_sidetone_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cx2070x_priv *data = i2c_get_clientdata(client);

	data->val = simple_strtoul(buf, NULL, 0);

	snd_soc_component_write(data->codec, 0x117a,
		      (snd_soc_component_read32(data->codec, 0x117a) | 0x80));
	snd_soc_component_write(data->codec, 0x1151, ((data->val >> 8) & 0x7F));
	snd_soc_component_write(data->codec, 0x1150, (data->val & 0xFF));
	snd_soc_component_write(data->codec, 0x117d,
		      (snd_soc_component_read32(data->codec, 0x117d) | 0x01));

	return count;
}

/* input_select = : suggest to use - should be defined by userland
 *  -1 unknown, 1 for MIC LEFT, 2 for MIC RIGHT, 3 for LINE IN RIGHT, 4 for LINE IN LEFT */
/* output_select = :
 *  -1 unknown, 0 for non, 1 for SPK (LINE OUT LEFT), 2 for LINE OUT RIGHT, 3 for both */
static int input_select = -1;
static int output_select = -1;

static ssize_t cx2070x_input_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", input_select);
}

static ssize_t cx2070x_input_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	input_select = simple_strtoul(buf, NULL, 0);

	return count;
}

static ssize_t cx2070x_output_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", output_select);
}

static ssize_t cx2070x_output_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	output_select = simple_strtoul(buf, NULL, 0);

	return count;
}

#define CX2070X_REG_0x0009		0x0009	/* LPR Reg: bit2=0 - no bootloader */
#define CX2070X_REG_0x02FC		0x02FC	/* SPoC address Low */
#define CX2070X_REG_0x02FD		0x02FD	/* SPoC address Middle */
#define CX2070X_REG_0x02FE		0x02FE	/* SPoC address Hiigh */
#define CX2070X_REG_0x02FF		0x02FF	/* Update Buffer Length */
#define CX2070X_REG_0x0300		0x0300	/* Update Buffer Address */
#define CX2070X_REG_0x0400		0x0400	/* Update Control Reg */

#define CX2070X_REG_0x1000		0x1000	/* SPoC Status Reg */

#define CX2070X_REG_0x1001		0x1001	/* Firmware version Low */
#define CX2070X_REG_0x1002		0x1002	/* Firmware version High */
#define CX2070X_REG_0x1003		0x1003	/* Patch version Low */
#define CX2070X_REG_0x1004		0x1004	/* Patch version High */
#define CX2070X_REG_0x1005		0x1005	/* Chip version */
#define CX2070X_REG_0x1015              0x1015	/* MIC left gain */
#define CX2070X_REG_0x1016              0x1016	/* MIC right gain */
#define CX2070X_REG_LINE_IN_GAIN        0x1014	/* LINE IN 1 gain, 0x101C for line in 2 gain, 0x101D for line in 3 gain */
						/* Line In ADC1 Gain Left 0x1013, Line In ADC1 Gain Right 0x1014 */
#define CX2070X_REG_LINE_OUT_L_GAIN     0x100D	/* DAC1 Left 0x100D */
#define CX2070X_REG_LINE_OUT_R_GAIN     0x100E	/* DAC2 Right 0x100E */
#define CX2070X_REG_0x117d              0x117d	/* DSP init */

#define CX2070X_REG_0x1584		0x1584	/* Patch version High */
#define CX2070X_REG_0x1585		0x1585	/* Patch version Mid */
#define CX2070X_REG_0x1586		0x1586	/* Patch version Low */

static SENSOR_DEVICE_ATTR(reg_regnum, S_IRUGO | S_IWUSR, cx2070x_regnum_show,
			  cx2070x_regnum_store, 0);
static SENSOR_DEVICE_ATTR(reg_regval, S_IRUGO | S_IWUSR, cx2070x_regval_show,
			  cx2070x_regval_store, 0);
static SENSOR_DEVICE_ATTR(reg_rdwr, S_IRUGO | S_IWUSR, cx2070x_reg_read,
			  cx2070x_reg_write, 0);
static SENSOR_DEVICE_ATTR(indirect_set, S_IRUGO | S_IWUSR,
			  cx2070x_indirect_regs_get, cx2070x_indirect_regs_set,
			  0);
static SENSOR_DEVICE_ATTR(indirect_cmd, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x0400);

static SENSOR_DEVICE_ATTR(reg_0x0009, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x0009);
static SENSOR_DEVICE_ATTR(reg_0x02fc, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x02FC);
static SENSOR_DEVICE_ATTR(reg_0x02fd, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x02FD);
static SENSOR_DEVICE_ATTR(reg_0x02fe, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x02FE);
static SENSOR_DEVICE_ATTR(reg_0x02ff, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x02FF);
static SENSOR_DEVICE_ATTR(reg_0x0300, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x0300);
static SENSOR_DEVICE_ATTR(reg_0x0400, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x0400);

static SENSOR_DEVICE_ATTR(reg_0x1000, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x1000);
static SENSOR_DEVICE_ATTR(reg_0x1001, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x1001);
static SENSOR_DEVICE_ATTR(reg_0x1002, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x1002);
static SENSOR_DEVICE_ATTR(reg_0x1003, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x1003);
static SENSOR_DEVICE_ATTR(reg_0x1004, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x1004);
static SENSOR_DEVICE_ATTR(reg_0x1005, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x1005);
static SENSOR_DEVICE_ATTR(reg_0x117d, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x117d);

static SENSOR_DEVICE_ATTR(reg_0x1584, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x1584);
static SENSOR_DEVICE_ATTR(reg_0x1585, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x1585);
static SENSOR_DEVICE_ATTR(reg_0x1586, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x1586);

static SENSOR_DEVICE_ATTR(FW_Version, S_IRUGO, fwVersion, NULL, 0);
static SENSOR_DEVICE_ATTR(CX_regDump, S_IRUGO, cxregdump, NULL, 0);

static SENSOR_DEVICE_ATTR(FWPatch_Version, S_IRUGO, patchVersion, NULL, 0);

static SENSOR_DEVICE_ATTR(aec, S_IRUGO | S_IWUSR, cx2070x_aec_show,
			  cx2070x_aec_store, 0);
static SENSOR_DEVICE_ATTR(noise_reduction, S_IRUGO | S_IWUSR, cx2070x_nr_show,
			  cx2070x_nr_store, 0);

static SENSOR_DEVICE_ATTR(mic_l_gain, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x1015);
static SENSOR_DEVICE_ATTR(mic_r_gain, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_0x1016);
static SENSOR_DEVICE_ATTR(li_gain, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_LINE_IN_GAIN);
static SENSOR_DEVICE_ATTR(lo_l_gain, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_LINE_OUT_L_GAIN);
static SENSOR_DEVICE_ATTR(lo_r_gain, S_IRUGO | S_IWUSR, cx2070x_show,
			  cx2070x_store, CX2070X_REG_LINE_OUT_R_GAIN);

static SENSOR_DEVICE_ATTR(in_mute, S_IRUGO | S_IWUSR, cx2070x_in_mute_show,
			  cx2070x_in_mute_store, CX2070X_REG_MUTE);
static SENSOR_DEVICE_ATTR(out_mute, S_IRUGO | S_IWUSR, cx2070x_out_mute_show,
			  cx2070x_out_mute_store, CX2070X_REG_MUTE);
static SENSOR_DEVICE_ATTR(voice_config, S_IRUGO | S_IWUSR,
			  cx2070x_sidetone_show, cx2070x_sidetone_store, 0);

static SENSOR_DEVICE_ATTR(input_config, S_IRUGO | S_IWUSR, cx2070x_input_show,
			  cx2070x_input_store, 0);
static SENSOR_DEVICE_ATTR(output_config, S_IRUGO | S_IWUSR, cx2070x_output_show,
			  cx2070x_output_store, 0);

static struct attribute *cx2070x_attributes[] = {

/* Universal register read/write */
	&sensor_dev_attr_reg_regnum.dev_attr.attr,
	&sensor_dev_attr_reg_regval.dev_attr.attr,
	&sensor_dev_attr_reg_rdwr.dev_attr.attr,
	&sensor_dev_attr_indirect_set.dev_attr.attr,
	&sensor_dev_attr_indirect_cmd.dev_attr.attr,
	&sensor_dev_attr_reg_0x117d.dev_attr.attr,

/* For EEPROM updates */
	&sensor_dev_attr_reg_0x0009.dev_attr.attr,
	&sensor_dev_attr_reg_0x02fc.dev_attr.attr,
	&sensor_dev_attr_reg_0x02fd.dev_attr.attr,
	&sensor_dev_attr_reg_0x02fe.dev_attr.attr,
	&sensor_dev_attr_reg_0x02ff.dev_attr.attr,
	&sensor_dev_attr_reg_0x0300.dev_attr.attr,
	&sensor_dev_attr_reg_0x0400.dev_attr.attr,

/* For chip identification */
	&sensor_dev_attr_reg_0x1000.dev_attr.attr,
	&sensor_dev_attr_reg_0x1001.dev_attr.attr,
	&sensor_dev_attr_reg_0x1002.dev_attr.attr,
	&sensor_dev_attr_reg_0x1003.dev_attr.attr,
	&sensor_dev_attr_reg_0x1004.dev_attr.attr,
	&sensor_dev_attr_reg_0x1005.dev_attr.attr,

/* For Firmware Patch file identification */
	&sensor_dev_attr_reg_0x1584.dev_attr.attr,
	&sensor_dev_attr_reg_0x1585.dev_attr.attr,
	&sensor_dev_attr_reg_0x1586.dev_attr.attr,

/* Abstracted functionality */
	&sensor_dev_attr_FW_Version.dev_attr.attr,
	&sensor_dev_attr_FWPatch_Version.dev_attr.attr,
	&sensor_dev_attr_aec.dev_attr.attr,
	&sensor_dev_attr_noise_reduction.dev_attr.attr,
	&sensor_dev_attr_CX_regDump.dev_attr.attr,

/* Gain control */
	&sensor_dev_attr_mic_l_gain.dev_attr.attr,
	&sensor_dev_attr_mic_r_gain.dev_attr.attr,
	&sensor_dev_attr_li_gain.dev_attr.attr,
	&sensor_dev_attr_lo_l_gain.dev_attr.attr,
	&sensor_dev_attr_lo_r_gain.dev_attr.attr,
	&sensor_dev_attr_in_mute.dev_attr.attr,
	&sensor_dev_attr_out_mute.dev_attr.attr,
	&sensor_dev_attr_voice_config.dev_attr.attr,
/* input, output selection */
	&sensor_dev_attr_input_config.dev_attr.attr,
	&sensor_dev_attr_output_config.dev_attr.attr,
	NULL
};

static struct attribute_group cx2070x_defattr_group = {
	.attrs = cx2070x_attributes,
};

static ssize_t cx2070x_indirect_data_set(struct file *filp,
					 struct kobject *kobj,
					 struct bin_attribute *attr, char *buf,
					 loff_t off, size_t count)
{
	struct i2c_client *client = kobj_to_i2c_client(kobj);

	dev_dbg(&client->dev, "%s (p=%p, off=%lli, c=%zi)\n", __func__,
		buf, off, count);

	if (count > 0 && count <= CX2070X_EEPROM_PAGE_SIZE) {
		/* Write out to the device */
		cx2070x_i2c_indirect_data(client, buf, count);
		return count;
	}

	return -ENOSPC;
}

static struct bin_attribute cx2070x_indirect_data_attr = {
	.attr = {
		 .name = "indirect_data",
		 .mode = S_IRUGO | S_IWUSR,
		 },
	.size = CX2070X_EEPROM_PAGE_SIZE,
	.write = cx2070x_indirect_data_set,
};

int cx2070x_sysfs_alloc(struct cx2070x_priv *cx2070x)
{
	struct i2c_client *client = cx2070x->cx_i2c;
	int err = 0;

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &cx2070x_defattr_group);
	if (err)
		return err;

	err = sysfs_create_bin_file(&client->dev.kobj,
				    &cx2070x_indirect_data_attr);
	if (err) {
		sysfs_remove_group(&client->dev.kobj, &cx2070x_defattr_group);
		return err;
	}

	return err;
}

EXPORT_SYMBOL(cx2070x_sysfs_alloc);

int cx2070x_sysfs_free(struct cx2070x_priv *cx2070x)
{
	struct i2c_client *client = cx2070x->cx_i2c;

	sysfs_remove_bin_file(&client->dev.kobj, &cx2070x_indirect_data_attr);
	sysfs_remove_group(&client->dev.kobj, &cx2070x_defattr_group);

	return 0;
}

EXPORT_SYMBOL(cx2070x_sysfs_free);

MODULE_DESCRIPTION("CX2070X SYSFS control interface driver");
MODULE_AUTHOR("Oleksandr Zhadan <www.ArcturusNetworks.com>");
MODULE_LICENSE("Dual BSD/GPL");
