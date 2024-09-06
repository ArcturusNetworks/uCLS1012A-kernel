
#ifdef CONFIG_SND_SOC_CX2070X_FW_PATCH
/*
 *  Writes a number of bytes from a buffer to the specified memory address.
 *
 * PARAMETERS
  *  addr     - Specifies the memory address.
 *  data_len - Specifies the number of bytes to be written to the memory 
 *             address.
 *  data     - Pointer to a buffer from an struct of cx2070x_rom_data is 
 *             to be written.
 *  type     - Specifies the requested memory type, the value must be from
 *             the following table.
 *                        MEM_TYPE_CPX     = 4
 *                        MEM_TYPE_SPX     = 2
 *                        MEM_TYPE_EEPROM  = 3
 *
 * RETURN
 *  
 *    If the operation completes successfully, the return value is 0.
 *    Otherwise, return -1. 
 */
static int cx_write_dsp_memory(struct cx2070x_priv *cx2070x, u32 addr,
			       u32 data_len, u8 * data, int type)
{
	int ret = 0;
	u8 address[4];
	u8 ctl_data[4];
	u8 offset = 0;
	u8 cr = 0;
	int is_continue = 0;
	int i = 0;
	const u32 addr_len = 2;
	u16 *addr_byte;
	u8 *data_end = data + data_len;
	u32 to_process = 0;
	unsigned int val;

	while (data_len) {
		to_process =
		    data_len <=
		    CX2070X_MAX_MEM_BUF ? data_len : CX2070X_MAX_MEM_BUF;
		data_len -= to_process;
		data_end = data + to_process;

		*((u32 *) & address) = cpu_to_be32(addr);
		offset = 0;

		if (!is_continue) {
			/*  Update the memory target address and buffer length. */
			ctl_data[0] = address[3];
			ctl_data[1] = address[2];
			ctl_data[2] = address[1];
			ctl_data[3] = (u8) to_process - 1;
			ret =
			    regmap_bulk_write(cx2070x->regmap, CXREG_UPDATE_AL,
					      ctl_data, 4);
			if (ret < 0) {
				dev_err(cx2070x->dev,
					"Failed to configure buffer: %d\n",
					ret);
				goto leave;
			}
		}

		/*  Update buffer. */
		ret =
		    regmap_bulk_write(cx2070x->regmap, CXREG_UPDATE_BUFF, data,
				      to_process);
		if (ret < 0) {
			dev_err(cx2070x->dev, "Failed to write buffer: %d\n",
				ret);
			goto leave;
		}

		data = data_end;

		/* Commit the changes and start to transfer buffer to memory. */
		if (type == MEM_TYPE_CPX)
			cr = 0x81;
		else if (type == MEM_TYPE_EEPROM)
			cr = 0x83;
		else if (type == MEM_TYPE_SPX) {
			cr = 0x85;
			if (is_continue)
				cr |= 0x08;
		}

		/* start to transfer */
		regmap_write(cx2070x->regmap, CXREG_UPDATE_CTR, cr);

		for (i = 0; i < CX2070X_MEMORY_UPDATE_TIMEOUT; i++) {
			/* loop until the writing is done */
			ret =
			    regmap_read(cx2070x->regmap, CXREG_UPDATE_CTR,
					&val);
			if (ret < 0) {
				dev_err(cx2070x->dev,
					"Failed to read buffer status: %d\n",
					ret);
				goto leave;
			}
			ctl_data[0] = (u8) val;

			if (!(ctl_data[0] & 0x80)) {
				/*done */
				ret = 0;
				goto leave;
			} else {
				/*pending */
				if (type == MEM_TYPE_EEPROM)
					msleep(5);
				else
					udelay(1);
				continue;
			}
		}

		if (i == CX2070X_MEMORY_UPDATE_TIMEOUT) {
			dev_err(cx2070x->dev, "Timeout while update memory\n");
			ret = -EBUSY;
			break;
		}
		is_continue = 1;
	};
leave:
	return ret;
}

static int cx2070x_update_fw(struct cx2070x_priv *cx2070x,
			     const struct firmware *fw)
{
	int ret = 0;
	struct cx2070x_rom *rom = (struct cx2070x_rom *)fw->data;
	struct cx2070x_rom_data *rom_data;
	struct cx2070x_rom_data *rom_data_end;
	u32 data_len = 0;
	u8 ready;
	u32 cur_addr = 0;
	u32 timeout = 0;
	unsigned int val;

	if (rom == NULL) {
		dev_err(cx2070x->dev, "Firmware patch is not available\n");
		goto err_exit;
	}

	/*determine if the data is patch or not. */
	if (rom->desc[0xd] != 'P') {
		dev_err(cx2070x->dev, "Firmware content is not correct\n");
		goto err_exit;
	}

	dev_info(cx2070x->dev, "Updating firmware patch: %23s\n", rom->desc);

	/*
	 * In order to prevent popping sound from speaker 
	 * while update firmware patch, we need to turn off
	 * jack sense and all streams before fimrware update 
	 */
	ret = regmap_write(cx2070x->regmap, CXREG_OUTPUT_CONTROL, 0);
	ret = regmap_write(cx2070x->regmap, CXREG_DSP_INIT_NEWC, 1);
	if (ret < 0) {
		dev_err(cx2070x->dev, "Failed to turn streams off %d\n", ret);
		goto err_exit;
	}

	/*download loader */
	rom_data =
	    (struct cx2070x_rom_data *)((char *)rom +
					be32_to_cpu(rom->loader_addr));
	rom_data_end =
	    (struct cx2070x_rom_data *)((char *)rom_data +
					be32_to_cpu(rom->loader_len));
	for (; rom_data != rom_data_end;) {
		cur_addr = be32_to_cpu(rom_data->addr);
		/*subtracts the address bytes */
		data_len = be32_to_cpu(rom_data->len) - sizeof(u32);
		ret =
		    regmap_bulk_write(cx2070x->regmap, cur_addr,
				      &rom_data->data[0], data_len);
		if (ret < 0) {
			dev_err(cx2070x->dev,
				"Failed to download loader code %d\n", ret);
			goto err_exit;
		}

		rom_data =
		    (struct cx2070x_rom_data *)((char *)rom_data +
						be32_to_cpu(rom_data->len)
						+ sizeof(u32));
	}

	/* check if the device is ready. */
	for (timeout = 0; timeout < CX2070X_LOADER_TIMEOUT; timeout++) {
		ret = regmap_read(cx2070x->regmap, CXREG_ABCODE, &val);
		if (ret == 0 && val == 0x1)
			break;
		msleep(1);
	}

	if (timeout == CX2070X_LOADER_TIMEOUT) {
		dev_err(cx2070x->dev, "timeout while download loader %d\n",
			ret);
		if (ret == 0)
			ret = -EBUSY;
		goto err_exit;
	}

	/*Update CPX code */
	rom_data =
	    (struct cx2070x_rom_data *)((char *)rom +
					be32_to_cpu(rom->cpx_addr));
	rom_data_end =
	    (struct cx2070x_rom_data *)((char *)rom_data +
					be32_to_cpu(rom->cpx_len));
	for (; rom_data != rom_data_end;) {
		cur_addr = be32_to_cpu(rom_data->addr);
		/*subtracts the address bytes */
		data_len = be32_to_cpu(rom_data->len) - sizeof(u32);

		ret =
		    cx_write_dsp_memory(cx2070x, cur_addr, data_len,
					&rom_data->data[0], MEM_TYPE_CPX);
		if (ret < 0) {
			dev_err(cx2070x->dev, "failed to update CPX code\n");
			goto err_exit;
		}
		rom_data =
		    (struct cx2070x_rom_data *)((char *)rom_data +
						be32_to_cpu(rom_data->len) +
						sizeof(u32));
	}

	/* Update SPX code */
	rom_data =
	    (struct cx2070x_rom_data *)((char *)rom +
					be32_to_cpu(rom->spx_addr));
	rom_data_end =
	    (struct cx2070x_rom_data *)((char *)rom_data +
					be32_to_cpu(rom->spx_len));
	for (; rom_data != rom_data_end;) {
		cur_addr = be32_to_cpu(rom_data->addr);
		/*only the last 3 bytes are valid address. */
		cur_addr &= 0x00ffffff;
		/*subtracts the address bytes */
		data_len = be32_to_cpu(rom_data->len) - sizeof(u32);
		ret =
		    cx_write_dsp_memory(cx2070x, cur_addr, data_len,
					&rom_data->data[0], MEM_TYPE_SPX);

		if (ret < 0) {
			dev_err(cx2070x->dev, "failed to update SPX code\n");
			goto err_exit;
		}
		rom_data =
		    (struct cx2070x_rom_data *)((char *)rom_data +
						be32_to_cpu(rom_data->len) +
						sizeof(u32));
	}

	/* Software reset */
	regmap_write(cx2070x->regmap, CXREG_ABCODE, 0x00);

	/* waiting until the device is ready */
	for (timeout = 0; timeout < CX2070X_SW_RESET_TIMEOUT; timeout++) {
		ret = regmap_read(cx2070x->regmap, CXREG_ABCODE, &val);
		if (ret == 0 && val == 0x1)
			break;
		msleep(1);
	}
	if (timeout == CX2070X_SW_RESET_TIMEOUT) {
		dev_err(cx2070x->dev, "timeout while download loader\n");
		if (ret == 0)
			ret = -EBUSY;
		goto err_exit;

	}
err_exit:
	return ret;
}

static void cx2070x_firmware_cont(const struct firmware *fw, void *context)
{
	struct cx2070x_priv *cx2070x = (struct cx2070x_priv *)context;
	char *buf = NULL;
	const u8 *dsp_code = NULL;
	int ret;
	int n;

	if (fw == NULL) {
		dev_err(cx2070x->dev, "Firmware is not available!\n");
		return;
	}

	regcache_cache_bypass(cx2070x->regmap, true);
	ret = cx2070x_update_fw(cx2070x, fw);

	if (ret < 0) {
		dev_err(cx2070x->dev,
			"Failed to download firmware. Error = %d\n", ret);
		goto LEAVE;
	}

	dev_dbg(cx2070x->dev, "download firmware patch successfully.\n");
	regcache_cache_bypass(cx2070x->regmap, false);

	ret =
	    snd_soc_register_codec(cx2070x->dev, cx2070x->codec_drv,
				   cx2070x->dai_drv, cx2070x->num_dai);

	if (ret < 0)
		dev_err(cx2070x->dev, "Failed to register codec: %d\n", ret);
	else
		dev_dbg(cx2070x->dev, "%s: Register codec.\n", __func__);
LEAVE:
	release_firmware(fw);

	return;
}

static int cx2070x_update_cache_from_firmware(void *context, unsigned int reg,
					      const void *data, size_t len)
{
	struct snd_soc_codec *codec = (struct snd_soc_codec *)context;
	unsigned int end_reg;
	u8 *val = (u8 *) data;
	if (reg >= 0xf50 && reg <= 0x1200) {
		end_reg = reg + len;
		if (end_reg > (MAX_REGISTER_NUMBER + 1))
			end_reg = (MAX_REGISTER_NUMBER + 1);
		for (; reg != end_reg; reg++) {
			codec->cache_bypass = 0;
			snd_soc_write(codec, reg, *val++);
			codec->cache_bypass = 1;
		}
	}
	return 0;
}

#endif
