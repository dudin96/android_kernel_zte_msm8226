/*
 * Synaptics DSX touchscreen driver
 *
 * Copyright (C) 2012 Synaptics Incorporated
 *
 * Copyright (C) 2012 Alexandra Chin <alexandra.chin@tw.synaptics.com>
 * Copyright (C) 2012 Scott Lin <scott.lin@tw.synaptics.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/firmware.h>
#include <linux/string.h>
#include <linux/input/synaptics_dsx.h>
#include "synaptics_dsx_i2c.h"

#ifdef SENSOR_ID_SUPPORT
	#define SENPR_ID_PIN1 9		/* sensor id pin 1st */
	#define SENPR_ID_PIN2 11	/* sensor id pin 2nd */
	#define SENPR_ID_PIN_NULL (-1)
	#define SENSOR_ID_PIN_MASK
	#define SENSOR_ID_PULLUP_MASK

	#define SENSOR_VENDOR_1 ".vendor1"
	#define SENSOR_VENDOR_2 ".vendor2"
	#define SENSOR_VENDOR_3 ".vendor3"
#endif

#define DEBUG_FW_UPDATE
#define SHOW_PROGRESS
#define FW_IMAGE_NAME "PR1116007_00000002.img"
#define MAX_FIRMWARE_ID_LEN 10
#define FORCE_UPDATE false
#define INSIDE_FIRMWARE_UPDATE

#define FW_IMAGE_OFFSET 0x100

#define BOOTLOADER_ID_OFFSET 0
#define FLASH_PROPERTIES_OFFSET 2
#define BLOCK_SIZE_OFFSET 3
#define FW_BLOCK_COUNT_OFFSET 5

#define REG_MAP (1 << 0)
#define UNLOCKED (1 << 1)
#define HAS_CONFIG_ID (1 << 2)
#define HAS_PERM_CONFIG (1 << 3)
#define HAS_BL_CONFIG (1 << 4)
#define HAS_DISP_CONFIG (1 << 5)
#define HAS_CTRL1 (1 << 6)

#define BLOCK_NUMBER_OFFSET 0
#define BLOCK_DATA_OFFSET 2

#define NAME_BUFFER_SIZE 128

enum flash_config_area {
	UI_CONFIG_AREA		= 0x00,
	PERM_CONFIG_AREA	= 0x01,
	BL_CONFIG_AREA		= 0x02,
	DISP_CONFIG_AREA	= 0x03
};

enum flash_command {
	CMD_WRITE_FW_BLOCK		= 0x2,
	CMD_ERASE_ALL			= 0x3,
	CMD_READ_CONFIG_BLOCK	= 0x5,
	CMD_WRITE_CONFIG_BLOCK	= 0x6,
	CMD_ERASE_CONFIG		= 0x7,
	CMD_READ_SENSOR_ID		= 0x8,
	CMD_ERASE_BL_CONFIG		= 0x9,
	CMD_ERASE_DISP_CONFIG	= 0xA,
	CMD_ENABLE_FLASH_PROG	= 0xF
};

enum flash_area {
	NONE,
	UI_FIRMWARE,
	CONFIG_AREA
};

enum image_file_option {
	OPTION_BUILD_INFO		= 0,
	OPTION_CONTAIN_BOOTLOADER	= 1,
};

#define SLEEP_MODE_NORMAL (0x00)
#define SLEEP_MODE_SENSOR_SLEEP (0x01)
#define SLEEP_MODE_RESERVED0 (0x02)
#define SLEEP_MODE_RESERVED1 (0x03)

#define ENABLE_WAIT_MS (1 * 1000)
#define WRITE_WAIT_MS (3 * 1000)
#define ERASE_WAIT_MS (5 * 1000)
#define RESET_WAIT_MS (500)

#define POLLING_MODE 0

#define SLEEP_TIME_US 50

static ssize_t fwu_sysfs_show_image(struct file *data_file,
		struct kobject *kobj, struct bin_attribute *attributes,
		char *buf, loff_t pos, size_t count);

static ssize_t fwu_sysfs_store_image(struct file *data_file,
		struct kobject *kobj, struct bin_attribute *attributes,
		char *buf, loff_t pos, size_t count);

static ssize_t fwu_sysfs_do_reflash_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t fwu_sysfs_write_config_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t fwu_sysfs_read_config_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t fwu_sysfs_config_area_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t fwu_sysfs_image_size_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t fwu_sysfs_block_size_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t fwu_sysfs_firmware_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t fwu_sysfs_configuration_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t fwu_sysfs_perm_config_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t fwu_sysfs_bl_config_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t fwu_sysfs_disp_config_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static int fwu_wait_for_idle(int timeout_ms);

struct image_header_data {
	union {
		struct {
			/* 0x00-0x0F */
			unsigned char file_checksum[4];
			unsigned char reserved_04;
			unsigned char reserved_05;
			unsigned char options_firmware_id:1;
			unsigned char options_contain_bootloader:1;
			unsigned char options_reserved:6;
			unsigned char bootloader_version;
			unsigned char firmware_size[4];
			unsigned char config_size[4];
			/* 0x10-0x1F */
			unsigned char product_id[SYNAPTICS_RMI4_PRODUCT_ID_SIZE];
			unsigned char reserved_1a;
			unsigned char reserved_1b;
			unsigned char reserved_1c;
			unsigned char reserved_1d;
			unsigned char product_info[SYNAPTICS_RMI4_PRODUCT_INFO_SIZE];
			/* 0x20-0x2F */
			unsigned char reserved_20_2f[0x10];
			/* 0x30-0x3F */
			unsigned char ds_firmware_id[0x10];
			/* 0x40-0x4F */
			unsigned char ds_customize_info[10];
			unsigned char reserved_4a_4f[6];
			/* 0x50-0x53*/
			unsigned char firmware_id[4];
		} __packed;
		unsigned char data[0x54];
	};
};

struct image_header {
	unsigned int checksum;
	unsigned int image_size;
	unsigned int config_size;
	unsigned char options;
	unsigned char bootloader_version;
	unsigned char product_id[SYNAPTICS_RMI4_PRODUCT_ID_SIZE + 1];
	unsigned char product_info[SYNAPTICS_RMI4_PRODUCT_INFO_SIZE];
	unsigned int firmware_id;
	bool is_contain_build_info;
};

struct pdt_properties {
	union {
		struct {
			unsigned char reserved_1:6;
			unsigned char has_bsr:1;
			unsigned char reserved_2:1;
		} __packed;
		unsigned char data[1];
	};
};

struct f01_device_status {
	union {
		struct {
			unsigned char status_code:4;
			unsigned char reserved:2;
			unsigned char flash_prog:1;
			unsigned char unconfigured:1;
		} __packed;
		unsigned char data[1];
	};
};

struct f01_device_control {
	union {
		struct {
			unsigned char sleep_mode:2;
			unsigned char nosleep:1;
			unsigned char reserved:2;
			unsigned char charger_connected:1;
			unsigned char report_rate:1;
			unsigned char configured:1;
		} __packed;
		unsigned char data[1];
	};
};

struct f34_flash_control {
	union {
		struct {
			unsigned char command:4;
			unsigned char status:3;
			unsigned char program_enabled:1;
		} __packed;
		unsigned char data[1];
	};
};

struct f34_flash_properties {
	union {
		struct {
			unsigned char regmap:1;
			unsigned char unlocked:1;
			unsigned char has_configid:1;
			unsigned char has_perm_config:1;
			unsigned char has_bl_config:1;
			unsigned char has_display_config:1;
			unsigned char has_blob_config:1;
			unsigned char reserved:1;
		} __packed;
		unsigned char data[1];
	};
};

struct synaptics_rmi4_fwu_handle {
	bool initialized;
	bool force_update;
	char product_id[SYNAPTICS_RMI4_PRODUCT_ID_SIZE + 1];
	unsigned int image_size;
	unsigned int data_pos;
	unsigned char intr_mask;
	unsigned char bootloader_id[2];
	unsigned char productinfo1;
	unsigned char productinfo2;
	unsigned char *ext_data_source;
	unsigned char *read_config_buf;
	const unsigned char *firmware_data;
	const unsigned char *config_data;
	unsigned short block_size;
	unsigned short fw_block_count;
	unsigned short config_block_count;
	unsigned short perm_config_block_count;
	unsigned short bl_config_block_count;
	unsigned short disp_config_block_count;
	unsigned short config_size;
	unsigned short config_area;
	unsigned short addr_f34_flash_control;
	unsigned short addr_f01_interrupt_register;
	struct synaptics_rmi4_fn_desc f01_fd;
	struct synaptics_rmi4_fn_desc f34_fd;
	struct synaptics_rmi4_exp_fn_ptr *fn_ptr;
	struct synaptics_rmi4_data *rmi4_data;
	struct f34_flash_control flash_control;
	struct f34_flash_properties flash_properties;
	struct workqueue_struct *fwu_workqueue;
	struct delayed_work fwu_work;
#ifdef SENSOR_ID_SUPPORT
	unsigned short sensor_id_pin_mask;
	unsigned short sensor_id_pullup_mask;
#endif
	char *firmware_name;
};

static struct bin_attribute dev_attr_data = {
	.attr = {
		.name = "data",
		.mode = 0644,    //(S_IRUGO | S_IWUGO),
	},
	.size = 0,
	.read = fwu_sysfs_show_image,
	.write = fwu_sysfs_store_image,
};

static struct device_attribute attrs[] = {
	__ATTR(doreflash, 0644/*S_IWUGO*/,
			synaptics_rmi4_show_error,
			fwu_sysfs_do_reflash_store),
	__ATTR(writeconfig, 0644/*S_IWUGO*/,
			synaptics_rmi4_show_error,
			fwu_sysfs_write_config_store),
	__ATTR(readconfig, 0644/*S_IWUGO*/,
			synaptics_rmi4_show_error,
			fwu_sysfs_read_config_store),
	__ATTR(configarea, 0644/*S_IWUGO*/,
			synaptics_rmi4_show_error,
			fwu_sysfs_config_area_store),
	__ATTR(imagesize, 0644/*S_IWUGO*/,
			synaptics_rmi4_show_error,
			fwu_sysfs_image_size_store),
	__ATTR(blocksize, 0644/*S_IRUGO*/,
			fwu_sysfs_block_size_show,
			synaptics_rmi4_store_error),
	__ATTR(fwblockcount, 0644/*S_IRUGO*/,
			fwu_sysfs_firmware_block_count_show,
			synaptics_rmi4_store_error),
	__ATTR(configblockcount, 0644/*S_IRUGO*/,
			fwu_sysfs_configuration_block_count_show,
			synaptics_rmi4_store_error),
	__ATTR(permconfigblockcount, 0644/*S_IRUGO*/,
			fwu_sysfs_perm_config_block_count_show,
			synaptics_rmi4_store_error),
	__ATTR(blconfigblockcount, 0644/*S_IRUGO*/,
			fwu_sysfs_bl_config_block_count_show,
			synaptics_rmi4_store_error),
	__ATTR(dispconfigblockcount,0644 /*S_IRUGO*/,
			fwu_sysfs_disp_config_block_count_show,
			synaptics_rmi4_store_error),
};

static struct synaptics_rmi4_fwu_handle *fwu;

DECLARE_COMPLETION(fwu_remove_complete);

static unsigned int extract_uint(const unsigned char *ptr)
{
	return (unsigned int)ptr[0] +
			(unsigned int)ptr[1] * 0x100 +
			(unsigned int)ptr[2] * 0x10000 +
			(unsigned int)ptr[3] * 0x1000000;
}

static unsigned int extract_uint_be(const unsigned char *ptr)
{
	return (unsigned int)ptr[3] +
			(unsigned int)ptr[2] * 0x100 +
			(unsigned int)ptr[1] * 0x10000 +
			(unsigned int)ptr[0] * 0x1000000;
}

static void parse_header(struct image_header *header,
		const unsigned char *fw_image)
{
	struct image_header_data *data = (struct image_header_data *)fw_image;
	header->checksum = extract_uint(data->file_checksum);
	header->bootloader_version = data->bootloader_version;
	header->image_size = extract_uint(data->firmware_size);
	header->config_size = extract_uint(data->config_size);
	memcpy(header->product_id, data->product_id,
			sizeof(data->product_id));
	header->product_id[sizeof(data->product_id)] = 0;
	memcpy(header->product_info, data->product_info,
			sizeof(data->product_info));

	header->is_contain_build_info =
		(data->options_firmware_id == (1 << OPTION_BUILD_INFO));
	if (header->is_contain_build_info) {
		header->firmware_id = extract_uint(data->firmware_id);
		dev_info(&fwu->rmi4_data->i2c_client->dev,
				"%s Firwmare build id %d\n",
				__func__,
				header->firmware_id);
	}

#ifdef DEBUG_FW_UPDATE
	dev_info(&fwu->rmi4_data->i2c_client->dev,
		"Firwmare size %d, config size %d\n",
		header->image_size,
		header->config_size);
#endif
	return;
}

static int fwu_read_f01_device_status(struct f01_device_status *status)
{
	int retval;

	retval = fwu->fn_ptr->read(fwu->rmi4_data,
			fwu->f01_fd.data_base_addr,
			status->data,
			sizeof(status->data));
	if (retval < 0) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Failed to read F01 device status\n",
				__func__);
		return retval;
	}

	return 0;
}

static int fwu_read_f34_queries(void)
{
	int retval;
	unsigned char count = 4;
	unsigned char buf[10];
	struct i2c_client *i2c_client = fwu->rmi4_data->i2c_client;

	retval = fwu->fn_ptr->read(fwu->rmi4_data,
			fwu->f34_fd.query_base_addr + BOOTLOADER_ID_OFFSET,
			fwu->bootloader_id,
			sizeof(fwu->bootloader_id));
	if (retval < 0) {
		dev_err(&i2c_client->dev,
				"%s: Failed to read bootloader ID\n",
				__func__);
		return retval;
	}

	retval = fwu->fn_ptr->read(fwu->rmi4_data,
			fwu->f34_fd.query_base_addr + FLASH_PROPERTIES_OFFSET,
			fwu->flash_properties.data,
			sizeof(fwu->flash_properties.data));
	if (retval < 0) {
		dev_err(&i2c_client->dev,
				"%s: Failed to read flash properties\n",
				__func__);
		return retval;
	}

	dev_info(&i2c_client->dev, "%s perm:%d, bl:%d, display:%d\n",
				__func__,
				fwu->flash_properties.has_perm_config,
				fwu->flash_properties.has_bl_config,
				fwu->flash_properties.has_display_config);

	if (fwu->flash_properties.has_perm_config)
		count += 2;

	if (fwu->flash_properties.has_bl_config)
		count += 2;

	if (fwu->flash_properties.has_display_config)
		count += 2;

	retval = fwu->fn_ptr->read(fwu->rmi4_data,
			fwu->f34_fd.query_base_addr + BLOCK_SIZE_OFFSET,
			buf,
			2);
	if (retval < 0) {
		dev_err(&i2c_client->dev,
				"%s: Failed to read block size info\n",
				__func__);
		return retval;
	}

	batohs(&fwu->block_size, &(buf[0]));

	retval = fwu->fn_ptr->read(fwu->rmi4_data,
			fwu->f34_fd.query_base_addr + FW_BLOCK_COUNT_OFFSET,
			buf,
			count);
	if (retval < 0) {
		dev_err(&i2c_client->dev,
				"%s: Failed to read block count info\n",
				__func__);
		return retval;
	}

	batohs(&fwu->fw_block_count, &(buf[0]));
	batohs(&fwu->config_block_count, &(buf[2]));

	count = 4;

	if (fwu->flash_properties.has_perm_config) {
		batohs(&fwu->perm_config_block_count, &(buf[count]));
		count += 2;
	}

	if (fwu->flash_properties.has_bl_config) {
		batohs(&fwu->bl_config_block_count, &(buf[count]));
		count += 2;
	}

	if (fwu->flash_properties.has_display_config)
		batohs(&fwu->disp_config_block_count, &(buf[count]));

	fwu->addr_f34_flash_control = fwu->f34_fd.data_base_addr +
					BLOCK_DATA_OFFSET +
					fwu->block_size;
	return 0;
}

static int fwu_read_interrupt_status(void)
{
	int retval;
	unsigned char interrupt_status;
	retval = fwu->fn_ptr->read(fwu->rmi4_data,
			fwu->addr_f01_interrupt_register,
			&interrupt_status,
			sizeof(interrupt_status));
	if (retval < 0) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Failed to read flash status\n",
				__func__);
		return retval;
	}
	return interrupt_status;
}

static int fwu_read_f34_flash_status(void)
{
	int retval;
	retval = fwu->fn_ptr->read(fwu->rmi4_data,
			fwu->addr_f34_flash_control,
			fwu->flash_control.data,
			sizeof(fwu->flash_control.data));
	if (retval < 0) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Failed to read flash status\n",
				__func__);
		return retval;
	}
	return 0;
}

unsigned long firmware_imgsize;
unsigned char firmware_imgver;
unsigned long config_imgsize;
//unsigned long filesize;
unsigned int synaptics_bootload_id;

unsigned char *firmware_imgdata = NULL;
unsigned char *config_imgdata = NULL;

static int synaptics_check_config_id(struct synaptics_rmi4_data *rmi4_data, const unsigned char *pfwfile )
{
	//struct synaptics_rmi_data *ts = i2c_get_clientdata(client);
	u8 chipid, vid, fwvid;
	int fwver;


	if ( !rmi4_data  || ! pfwfile )
		return -1;
	
	//if ( mode == true )	// force update
		//return 1;

	// get fw vid from fw file
	firmware_imgsize = extract_uint(&(pfwfile[8]));
	config_imgdata = (unsigned char*) ( &pfwfile[0] + 0x100 + firmware_imgsize );
	fwvid = *(config_imgdata+1);
	fwver = ((*(config_imgdata+2))<<8)|(*(config_imgdata+3));
	
	chipid = rmi4_data->config_id.chip_type;
	vid = rmi4_data->config_id.sensor;

	pr_info("current chip type id = 0x%x(%c)\n", chipid, chipid );
	pr_info("current sensor partner id = 0x%x(%c)\n", vid, vid );
	pr_info("fw file sensor partner id = 0x%x(%c)\n", fwvid, fwvid );
	pr_info("fw file ver = 0x%x\n", fwver );

	if( fwvid != vid){
		pr_info("module id dismatch!\n");
		return -1;
	}else{
		pr_info("module id matched!\n");
		return 0;
	}

}

static int fwu_reset_device(void)
{
	int retval;

#ifdef DEBUG_FW_UPDATE
	dev_info(&fwu->rmi4_data->i2c_client->dev,
			"%s: Reset device\n",
			__func__);
#endif

	retval = fwu->rmi4_data->reset_device(fwu->rmi4_data);
	if (retval < 0) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Failed to reset core driver after reflash\n",
				__func__);
		return retval;
	}
	return 0;
}

static int fwu_write_f34_command(unsigned char cmd)
{
	int retval;

	fwu->flash_control.data[0] = cmd;
	retval = fwu->fn_ptr->write(fwu->rmi4_data,
			fwu->addr_f34_flash_control,
			fwu->flash_control.data,
			sizeof(fwu->flash_control.data));
	if (retval < 0) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Failed to write command 0x%02x\n",
				__func__, fwu->flash_control.data[0]);
		return retval;
	}
	return 0;
}

static int fwu_wait_for_idle(int timeout_ms)
{
	int count = 0;
	int timeout_count = ((timeout_ms * 1000) / SLEEP_TIME_US) + 1;
	do {
		#if POLLING_MODE
		fwu_read_f34_flash_status();
		#endif
		if (fwu->flash_control.command == 0x00)
			return 0;

		usleep_range(SLEEP_TIME_US, SLEEP_TIME_US + 100);
	} while (count++ < timeout_count);

	fwu_read_f34_flash_status();
	if (fwu->flash_control.command == 0x00)
		return 0;

	dev_err(&fwu->rmi4_data->i2c_client->dev,
			"%s: Timed out waiting for idle status\n",
			__func__);

	return -ETIMEDOUT;
}

static enum flash_area fwu_go_nogo(struct image_header *header)
{
	int retval = 0;
	int index = 0;
	int deviceFirmwareID;
	int imageConfigID;
	int deviceConfigID;
	unsigned long imageFirmwareID;
	unsigned char firmware_id[4];
	unsigned char config_id[4];
	char *strptr;
	char *imagePR = kzalloc(sizeof(MAX_FIRMWARE_ID_LEN), GFP_KERNEL);
	enum flash_area flash_area = NONE;
	struct i2c_client *i2c_client = fwu->rmi4_data->i2c_client;
	struct f01_device_status f01_device_status;

	if (fwu->force_update) {
		flash_area = UI_FIRMWARE;
		goto exit;
	}

	retval = fwu_read_f01_device_status(&f01_device_status);
	if (retval < 0) {
		flash_area = NONE;
		goto exit;
	}

	/* Force update firmware when device is in bootloader mode */
	if (f01_device_status.flash_prog) {
		dev_info(&i2c_client->dev,
			"%s: In flash prog mode\n",
			__func__);
		flash_area = UI_FIRMWARE;
		goto exit;
	}

	/* device firmware id */
	retval = fwu->fn_ptr->read(fwu->rmi4_data,
				fwu->f01_fd.query_base_addr + 18,
				firmware_id,
				sizeof(firmware_id));
	if (retval < 0) {
		dev_err(&i2c_client->dev,
			"%s: Failed to read firmware ID (code %d).\n",
			__func__,
			retval);
		goto exit;
	}
	firmware_id[3] = 0;
	deviceFirmwareID = extract_uint(firmware_id);

	/* .img firmware id */
	if (header->is_contain_build_info) {
		dev_err(&i2c_client->dev,
			"%s: Image option contains build info.\n",
		__func__);
		imageFirmwareID = header->firmware_id;
	} else {
		strptr = strstr(fwu->firmware_name, "PR");
		if (!strptr) {
			dev_err(&i2c_client->dev,
				"%s: No valid PR number (PRxxxxxxx)" \
				"found in image file name...\n", __func__);
			goto exit;
		}

		strptr += 2;
		while (strptr[index] >= '0' && strptr[index] <= '9') {
			imagePR[index] = strptr[index];
			index++;
		}
		imagePR[index] = 0;

		retval = sstrtoul(imagePR, 10, &imageFirmwareID);
		if (retval ==  -EINVAL) {
			dev_err(&i2c_client->dev,
				"%s: Invalid image firmware id...\n",
				__func__);
			goto exit;
		}
	}

	dev_dbg(&i2c_client->dev,
			"%s: Device firmware id %d, .img firmware id %d\n",
			__func__,
			deviceFirmwareID,
			(unsigned int)imageFirmwareID);
	if (imageFirmwareID > deviceFirmwareID) {
		flash_area = UI_FIRMWARE;
		goto exit;
	} else if (imageFirmwareID < deviceFirmwareID) {
		flash_area = NONE;
		dev_info(&i2c_client->dev,
			"%s: Img fw is older than device fw. Skip fw update.\n",
			__func__);
		goto exit;
	}

	/* device config id */
	retval = fwu->fn_ptr->read(fwu->rmi4_data,
				fwu->f34_fd.ctrl_base_addr,
				config_id,
				sizeof(config_id));
	if (retval < 0) {
		dev_err(&i2c_client->dev,
			"%s: Failed to read config ID (code %d).\n",
			__func__,
			retval);
		flash_area = NONE;
		goto exit;
	}
	deviceConfigID =  extract_uint_be(config_id);

	dev_dbg(&i2c_client->dev,
		"%s: Device config ID 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
		__func__,
		config_id[0], config_id[1], config_id[2], config_id[3]);

	/* .img config id */
	dev_dbg(&i2c_client->dev,
			"%s .img config ID 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
			__func__,
			fwu->config_data[0],
			fwu->config_data[1],
			fwu->config_data[2],
			fwu->config_data[3]);
	imageConfigID =  extract_uint_be(fwu->config_data);

	dev_dbg(&i2c_client->dev,
		"%s: Device config ID %d, .img config ID %d\n",
		__func__, deviceConfigID, imageConfigID);

	if (imageConfigID > deviceConfigID) {
		flash_area = CONFIG_AREA;
		goto exit;
	}

exit:
	kfree(imagePR);
	if (flash_area == NONE){
		dev_info(&i2c_client->dev,
			"%s: Nothing needs to be updated\n", __func__);
		//flash_area = UI_FIRMWARE;
		}
	else
		dev_info(&i2c_client->dev,
			"%s: Update %s block\n",
			__func__,
			flash_area == UI_FIRMWARE ? "UI FW" : "CONFIG");
	return flash_area;
}

static int fwu_scan_pdt(void)
{
	int retval;
	unsigned char ii;
	unsigned char intr_count = 0;
	unsigned char intr_off;
	unsigned char intr_src;
	unsigned short addr;
	bool f01found = false;
	bool f34found = false;
	struct synaptics_rmi4_fn_desc rmi_fd;

#ifdef DEBUG_FW_UPDATE
	dev_info(&fwu->rmi4_data->i2c_client->dev, "Scan PDT\n");
#endif

	for (addr = PDT_START; addr > PDT_END; addr -= PDT_ENTRY_SIZE) {
		retval = fwu->fn_ptr->read(fwu->rmi4_data,
					addr,
					(unsigned char *)&rmi_fd,
					sizeof(rmi_fd));
		if (retval < 0)
			return retval;

		if (rmi_fd.fn_number) {
			dev_dbg(&fwu->rmi4_data->i2c_client->dev,
					"%s: Found F%02x\n",
					__func__, rmi_fd.fn_number);
			switch (rmi_fd.fn_number) {
			case SYNAPTICS_RMI4_F01:
				f01found = true;
				fwu->f01_fd = rmi_fd;
				fwu->addr_f01_interrupt_register =
					fwu->f01_fd.data_base_addr + 1;
				break;
			case SYNAPTICS_RMI4_F34:
				f34found = true;
				fwu->f34_fd = rmi_fd;
				fwu->intr_mask = 0;
				intr_src = rmi_fd.intr_src_count;
				intr_off = intr_count % 8;
				for (ii = intr_off;
						ii < ((intr_src & MASK_3BIT) +
						intr_off);
						ii++)
					fwu->intr_mask |= 1 << ii;
				break;
			}
		} else
		break;

		intr_count += (rmi_fd.intr_src_count & MASK_3BIT);
	}

	if (!f01found || !f34found) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Failed to find both F01 and F34\n",
				__func__);
		return -EINVAL;
	}

	fwu_read_interrupt_status();
	return 0;
}

static int fwu_write_blocks(unsigned char *block_ptr, unsigned short block_cnt,
		unsigned char command)
{
	int retval;
	unsigned char block_offset[] = {0, 0};
	unsigned short block_num;
	struct i2c_client *i2c_client = fwu->rmi4_data->i2c_client;
#ifdef SHOW_PROGRESS
	unsigned int progress = (command == CMD_WRITE_CONFIG_BLOCK) ?
				10 : 100;
#endif

#ifdef DEBUG_FW_UPDATE
	dev_info(&i2c_client->dev,
			"%s: Start to update %s blocks\n",
			__func__,
			command == CMD_WRITE_CONFIG_BLOCK ?
			"config" : "firmware");
#endif
	retval = fwu->fn_ptr->write(fwu->rmi4_data,
			fwu->f34_fd.data_base_addr + BLOCK_NUMBER_OFFSET,
			block_offset,
			sizeof(block_offset));
	if (retval < 0) {
		dev_err(&i2c_client->dev,
				"%s: Failed to write to block number registers\n",
				__func__);
		return retval;
	}

	for (block_num = 0; block_num < block_cnt; block_num++) {
#ifdef SHOW_PROGRESS
		if (block_num % progress == 0)
			dev_info(&i2c_client->dev,
					"%s: update %s %3d / %3d\n",
					__func__,
					command == CMD_WRITE_CONFIG_BLOCK ?
					"config" : "firmware",
					block_num, block_cnt);
#endif
		retval = fwu->fn_ptr->write(fwu->rmi4_data,
			fwu->f34_fd.data_base_addr + BLOCK_DATA_OFFSET,
			block_ptr,
			fwu->block_size);
		if (retval < 0) {
			dev_err(&i2c_client->dev,
				"%s: Failed to write block data (block %d)\n",
				__func__, block_num);
			return retval;
		}

		retval = fwu_write_f34_command(command);
		if (retval < 0) {
			dev_err(&i2c_client->dev,
					"%s: Failed to write command for block %d\n",
					__func__, block_num);
			return retval;
		}

		retval = fwu_wait_for_idle(WRITE_WAIT_MS);
		if (retval < 0) {
			dev_err(&i2c_client->dev,
					"%s: Failed to wait for idle status (block %d)\n",
					__func__, block_num);
			return retval;
		}

		if (fwu->flash_control.status != 0x00) {
			dev_err(&i2c_client->dev,
					"%s: Flash block %d failed, status 0x%02X\n",
					__func__, block_num, retval);
			return -1;
		}

		block_ptr += fwu->block_size;
	}
#ifdef SHOW_PROGRESS
	dev_info(&i2c_client->dev,
			"%s: update %s %3d / %3d\n",
			__func__,
			command == CMD_WRITE_CONFIG_BLOCK ?
			"config" : "firmware",
			block_cnt, block_cnt);
#endif
	return 0;
}

static int fwu_write_firmware(void)
{
	return fwu_write_blocks((unsigned char *)fwu->firmware_data,
		fwu->fw_block_count, CMD_WRITE_FW_BLOCK);
}

static int fwu_write_configuration(void)
{
	return fwu_write_blocks((unsigned char *)fwu->config_data,
		fwu->config_block_count, CMD_WRITE_CONFIG_BLOCK);
}

static int fwu_write_bootloader_id(void)
{
	int retval;

#ifdef DEBUG_FW_UPDATE
	dev_info(&fwu->rmi4_data->i2c_client->dev,
			"Write bootloader ID 0x%02X 0x%02X\n",
			fwu->bootloader_id[0],
			fwu->bootloader_id[1]);
#endif
	retval = fwu->fn_ptr->write(fwu->rmi4_data,
			fwu->f34_fd.data_base_addr + BLOCK_DATA_OFFSET,
			fwu->bootloader_id,
			sizeof(fwu->bootloader_id));
	if (retval < 0) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Failed to write bootloader ID\n",
				__func__);
		return retval;
	}

	return 0;
}

static int fwu_enter_flash_prog(void)
{
	int retval;
	struct f01_device_status f01_device_status;
	struct f01_device_control f01_device_control;

#ifdef DEBUG_FW_UPDATE
	dev_info(&fwu->rmi4_data->i2c_client->dev, "Enter bootloader mode\n");
#endif
	retval = fwu_read_f01_device_status(&f01_device_status);
	if (retval < 0)
		return retval;

	if (f01_device_status.flash_prog) {
		dev_info(&fwu->rmi4_data->i2c_client->dev,
				"%s: Already in flash prog mode\n",
				__func__);
		return 0;
	}

	retval = fwu_write_bootloader_id();
	if (retval < 0)
		return retval;

	retval = fwu_write_f34_command(CMD_ENABLE_FLASH_PROG);
	if (retval < 0)
		return retval;

	retval = fwu_wait_for_idle(ENABLE_WAIT_MS);
	if (retval < 0)
		return retval;

	retval = fwu_scan_pdt();
	if (retval < 0)
		return retval;

	retval = fwu_read_f01_device_status(&f01_device_status);
	if (retval < 0)
		return retval;

	if (!f01_device_status.flash_prog) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Not in flash prog mode\n",
				__func__);
		return -EINVAL;
	}

	retval = fwu_read_f34_queries();
	if (retval < 0)
		return retval;

	retval = fwu->fn_ptr->read(fwu->rmi4_data,
			fwu->f01_fd.ctrl_base_addr,
			f01_device_control.data,
			sizeof(f01_device_control.data));
	if (retval < 0) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Failed to read F01 device control\n",
				__func__);
		return retval;
	}

	f01_device_control.nosleep = true;
	f01_device_control.sleep_mode = SLEEP_MODE_NORMAL;

	retval = fwu->fn_ptr->write(fwu->rmi4_data,
			fwu->f01_fd.ctrl_base_addr,
			f01_device_control.data,
			sizeof(f01_device_control.data));
	if (retval < 0) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Failed to write F01 device control\n",
				__func__);
		return retval;
	}

	return retval;
}

static int fwu_do_reflash(void)
{
	int retval;

	retval = fwu_enter_flash_prog();
	if (retval < 0)
		return retval;

	dev_dbg(&fwu->rmi4_data->i2c_client->dev,
			"%s: Entered flash prog mode\n",
			__func__);

	retval = fwu_write_bootloader_id();
	if (retval < 0)
		return retval;

	dev_dbg(&fwu->rmi4_data->i2c_client->dev,
			"%s: Bootloader ID written\n",
			__func__);

	retval = fwu_write_f34_command(CMD_ERASE_ALL);
	if (retval < 0)
		return retval;

	dev_dbg(&fwu->rmi4_data->i2c_client->dev,
			"%s: Erase all command written\n",
			__func__);

	retval = fwu_wait_for_idle(ERASE_WAIT_MS);
	if (retval < 0)
		return retval;

	if (fwu->flash_control.status != 0x00) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Erase all command failed, status 0x%02X\n",
				__func__, retval);
		return -1;
	}

	if (fwu->firmware_data) {
		retval = fwu_write_firmware();
		if (retval < 0)
			return retval;
		pr_notice("%s: Firmware programmed\n", __func__);
	}

	if (fwu->config_data) {
		retval = fwu_write_configuration();
		if (retval < 0)
			return retval;
		pr_notice("%s: Configuration programmed\n", __func__);
	}

	return retval;
}

static int fwu_do_write_config(void)
{
	int retval;

	retval = fwu_enter_flash_prog();
	if (retval < 0)
		return retval;

	dev_dbg(&fwu->rmi4_data->i2c_client->dev,
			"%s: Entered flash prog mode\n",
			__func__);

	if (fwu->config_area == PERM_CONFIG_AREA) {
		fwu->config_block_count = fwu->perm_config_block_count;
		goto write_config;
	}

	retval = fwu_write_bootloader_id();
	if (retval < 0)
		return retval;

	dev_dbg(&fwu->rmi4_data->i2c_client->dev,
			"%s: Bootloader ID written\n",
			__func__);

	switch (fwu->config_area) {
	case UI_CONFIG_AREA:
		retval = fwu_write_f34_command(CMD_ERASE_CONFIG);
		break;
	case BL_CONFIG_AREA:
		retval = fwu_write_f34_command(CMD_ERASE_BL_CONFIG);
		fwu->config_block_count = fwu->bl_config_block_count;
		break;
	case DISP_CONFIG_AREA:
		retval = fwu_write_f34_command(CMD_ERASE_DISP_CONFIG);
		fwu->config_block_count = fwu->disp_config_block_count;
		break;
	}
	if (retval < 0)
		return retval;

	dev_dbg(&fwu->rmi4_data->i2c_client->dev,
			"%s: Erase command written\n",
			__func__);

	retval = fwu_wait_for_idle(ERASE_WAIT_MS);
	if (retval < 0)
		return retval;

	dev_dbg(&fwu->rmi4_data->i2c_client->dev,
			"%s: Idle status detected\n",
			__func__);

write_config:
	retval = fwu_write_configuration();
	if (retval < 0)
		return retval;

	pr_notice("%s: Config written\n", __func__);

	return retval;
}

static int fwu_start_write_config(void)
{
	int retval;
	struct image_header header;

	switch (fwu->config_area) {
	case UI_CONFIG_AREA:
		break;
	case PERM_CONFIG_AREA:
		if (!fwu->flash_properties.has_perm_config)
			return -EINVAL;
		break;
	case BL_CONFIG_AREA:
		if (!fwu->flash_properties.has_bl_config)
			return -EINVAL;
		break;
	case DISP_CONFIG_AREA:
		if (!fwu->flash_properties.has_display_config)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (fwu->ext_data_source)
		fwu->config_data = fwu->ext_data_source;
	else
		return -EINVAL;

	if (fwu->config_area == UI_CONFIG_AREA) {
		parse_header(&header, fwu->ext_data_source);

		if (header.config_size) {
			fwu->config_data = fwu->ext_data_source +
					FW_IMAGE_OFFSET +
					header.image_size;
		} else {
			return -EINVAL;
		}
	}

	pr_notice("%s: Start of write config process\n", __func__);

	retval = fwu_do_write_config();
	if (retval < 0) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Failed to write config\n",
				__func__);
	}

	fwu->rmi4_data->reset_device(fwu->rmi4_data);

	pr_notice("%s: End of write config process\n", __func__);

	return retval;
}

static int fwu_do_read_config(void)
{
	int retval;
	unsigned char block_offset[] = {0, 0};
	unsigned short block_num;
	unsigned short block_count;
	unsigned short index = 0;

	retval = fwu_enter_flash_prog();
	if (retval < 0)
		goto exit;

	dev_dbg(&fwu->rmi4_data->i2c_client->dev,
			"%s: Entered flash prog mode\n",
			__func__);

	switch (fwu->config_area) {
	case UI_CONFIG_AREA:
		block_count = fwu->config_block_count;
		break;
	case PERM_CONFIG_AREA:
		if (!fwu->flash_properties.has_perm_config) {
			retval = -EINVAL;
			goto exit;
		}
		block_count = fwu->perm_config_block_count;
		break;
	case BL_CONFIG_AREA:
		if (!fwu->flash_properties.has_bl_config) {
			retval = -EINVAL;
			goto exit;
		}
		block_count = fwu->bl_config_block_count;
		break;
	case DISP_CONFIG_AREA:
		if (!fwu->flash_properties.has_display_config) {
			retval = -EINVAL;
			goto exit;
		}
		block_count = fwu->disp_config_block_count;
		break;
	default:
		retval = -EINVAL;
		goto exit;
	}

	fwu->config_size = fwu->block_size * block_count;

	kfree(fwu->read_config_buf);
	fwu->read_config_buf = kzalloc(fwu->config_size, GFP_KERNEL);

	block_offset[1] |= (fwu->config_area << 5);

	retval = fwu->fn_ptr->write(fwu->rmi4_data,
			fwu->f34_fd.data_base_addr + BLOCK_NUMBER_OFFSET,
			block_offset,
			sizeof(block_offset));
	if (retval < 0) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Failed to write to block number registers\n",
				__func__);
		goto exit;
	}

	for (block_num = 0; block_num < block_count; block_num++) {
		retval = fwu_write_f34_command(CMD_READ_CONFIG_BLOCK);
		if (retval < 0) {
			dev_err(&fwu->rmi4_data->i2c_client->dev,
					"%s: Failed to write read config command\n",
					__func__);
			goto exit;
		}

		retval = fwu_wait_for_idle(WRITE_WAIT_MS);
		if (retval < 0) {
			dev_err(&fwu->rmi4_data->i2c_client->dev,
					"%s: Failed to wait for idle status\n",
					__func__);
			goto exit;
		}

		retval = fwu->fn_ptr->read(fwu->rmi4_data,
				fwu->f34_fd.data_base_addr + BLOCK_DATA_OFFSET,
				&fwu->read_config_buf[index],
				fwu->block_size);
		if (retval < 0) {
			dev_err(&fwu->rmi4_data->i2c_client->dev,
					"%s: Failed to read block data (block %d)\n",
					__func__, block_num);
			goto exit;
		}

		index += fwu->block_size;
	}

exit:
	fwu->rmi4_data->reset_device(fwu->rmi4_data);

	return retval;
}

#ifdef SENSOR_ID_SUPPORT
static void synaptics_rmi4_fwu_config_sensor_id(void)
{
	int i;
	char sensor_id_pin[2] = {SENPR_ID_PIN1, SENPR_ID_PIN2};

	for (i = 0; i < sizeof(sensor_id_pin); i++) {
		if (sensor_id_pin[i] != SENPR_ID_PIN_NULL) {
			fwu->sensor_id_pin_mask |= (0x01 << sensor_id_pin[i]);
			fwu->sensor_id_pullup_mask |=
				(0x01 << sensor_id_pin[i]);
		} else {
			break;
		}
	}
	dev_info(&fwu->rmi4_data->i2c_client->dev,
			"Sensor ID pin mask %#04X, pullup mask %#04X\n",
			fwu->sensor_id_pin_mask,
			fwu->sensor_id_pullup_mask);
}

static int fwu_read_sensor_id(void)
{
	int retval;
	unsigned char sensor_id_pin_mask[2];
	unsigned char sensor_id_pullup_mask[2];
	unsigned char sensor_id_data[2];
	unsigned short sensor_id;

	retval = fwu_enter_flash_prog();
	if (retval < 0)
		return retval;

	dev_dbg(&fwu->rmi4_data->i2c_client->dev,
			"%s: Entered flash prog mode\n",
			__func__);

	/* set sensor id mask */
	sensor_id_pin_mask[0] = fwu->sensor_id_pin_mask & 0xFF;
	sensor_id_pin_mask[1] = (fwu->sensor_id_pin_mask & 0xFF00) >> 8;

	retval = fwu->fn_ptr->write(fwu->rmi4_data,
		fwu->f34_fd.data_base_addr + BLOCK_DATA_OFFSET,
		sensor_id_pin_mask,
		2);
	if (retval < 0) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
			"%s: Failed to write sensor id pin mask)\n",
			__func__);
		return retval;
	}

	/* set sensor id pullup mask */
	sensor_id_pullup_mask[0] = fwu->sensor_id_pullup_mask & 0xFF;
	sensor_id_pullup_mask[1] = (fwu->sensor_id_pullup_mask & 0xFF00) >> 8;

	retval = fwu->fn_ptr->write(fwu->rmi4_data,
		fwu->f34_fd.data_base_addr + BLOCK_DATA_OFFSET + 2,
		sensor_id_pullup_mask,
		2);
	if (retval < 0) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
			"%s: Failed to write sensor id pullup mask\n",
			__func__);
		return retval;
	}

	retval = fwu_write_f34_command(CMD_READ_SENSOR_ID);
	if (retval < 0)
		return retval;

	dev_dbg(&fwu->rmi4_data->i2c_client->dev,
			"%s: Erase command written\n",
			__func__);

	retval = fwu_wait_for_idle(ERASE_WAIT_MS);
	if (retval < 0)
		return retval;

	dev_dbg(&fwu->rmi4_data->i2c_client->dev,
			"%s: Idle status detected\n",
			__func__);

	/* read sensor id */
	retval = fwu->fn_ptr->read(fwu->rmi4_data,
		fwu->f34_fd.data_base_addr + BLOCK_DATA_OFFSET + 4,
		sensor_id_data,
		2);
	if (retval < 0) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
			"%s: Failed to read sensor id data\n",
			__func__);
		return retval;
	}

	sensor_id = sensor_id_data[0] |
			((unsigned short)sensor_id_data[1] << 8);

	dev_info(&fwu->rmi4_data->i2c_client->dev,
			"%s: Read sensor id data %#02X %#02X (%d)\n",
			__func__,
			sensor_id_data[0],
			sensor_id_data[1],
			sensor_id);

	/* reset device */
	fwu_reset_device();

	return sensor_id;
}
#endif
char *syna_file_name;
char  *syna_fw_name="P864A10_TPK_PR1343768-s3202_zte_34313131.img";
 int fwu_start_reflash(void)
{
	int retval = 0;
	struct image_header header;
	const unsigned char *fw_image;
	const struct firmware *fw_entry = NULL;
	struct f01_device_status f01_device_status;
	enum flash_area flash_area;
#ifdef SENSOR_ID_SUPPORT
	unsigned short sensor_id;
	bool sensor_pin1, sensor_pin2;
	char *file_name_sep;
	char *cur = file_name;
#endif
	//char file_name[] = FW_IMAGE_NAME;

	pr_notice("%s: Start of reflash process\n", __func__);

	if (fwu->ext_data_source)
		fw_image = fwu->ext_data_source;
	else {
		fwu->firmware_name =
			kcalloc(NAME_BUFFER_SIZE, sizeof(char), GFP_KERNEL);
		if (!fwu->firmware_name) {
			dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s Failed to allocate firmware name (%d).\n",
				__func__,
				NAME_BUFFER_SIZE);
			goto exit;
		}

#ifdef SENSOR_ID_SUPPORT
		/* Sensor ID support: if firmware image file name is xxx.img
		  * image file name for sensor vender 1 will be
				xxx.{SENSOR_VENDOR_1}.img
		  * image file name for sensor vender 2 will be
				xxx.{SENSOR_VENDOR_2}.img
		  * image file name for sensor vender 3 will be
				xxx.{SENSOR_VENDOR_3}.img
		  */

		syna_file_name = strsep(&cur, ".");
		snprintf(fwu->firmware_name,
				NAME_BUFFER_SIZE, "%s", file_name_sep);

		sensor_id = fwu_read_sensor_id();
		sensor_pin1 = sensor_id & (0x01 << SENPR_ID_PIN1);
		sensor_pin2 = sensor_id & (0x01 << SENPR_ID_PIN2);

		dev_info(&fwu->rmi4_data->i2c_client->dev,
					"%s: Sensor id %d (pin %d) %d (pin %d)\n",
					__func__,
					sensor_pin1,
					SENPR_ID_PIN1,
					sensor_pin2,
					SENPR_ID_PIN2);

		if ((sensor_pin1 == 0) && (sensor_pin2 == 0))
			strcat(fwu->firmware_name, SENSOR_VENDOR_1);
		else if ((sensor_pin1 == 0) && (sensor_pin2 == 1))
			strcat(fwu->firmware_name, SENSOR_VENDOR_2);
		else if ((sensor_pin1 == 1) && (sensor_pin2 == 0))
			strcat(fwu->firmware_name, SENSOR_VENDOR_3);
		else
			dev_err(&fwu->rmi4_data->i2c_client->dev,
					"%s: Sensor id %d %d is not available\n",
					__func__,
					sensor_pin1,
					sensor_pin2);

		strcat(fwu->firmware_name, ".img");
#else
		printk("syna fwfile name:%s\n",syna_file_name);

		if(!syna_file_name){
			printk("syna fw name is null\n");
			syna_file_name = syna_fw_name;
		}
		if(!strcmp(syna_file_name,""))
		{
			printk("%s file_name is null\n",__func__);
			return -1;
		}


		snprintf(fwu->firmware_name, NAME_BUFFER_SIZE, "%s", syna_file_name);
	
#endif
		dev_info(&fwu->rmi4_data->i2c_client->dev,
				"%s: Requesting firmware image %s\n",
				__func__, fwu->firmware_name);

		retval = request_firmware(&fw_entry, fwu->firmware_name,
				&fwu->rmi4_data->i2c_client->dev);
		if (retval != 0) {
			dev_err(&fwu->rmi4_data->i2c_client->dev,
					"%s: Firmware image %s not available\n",
					__func__, fwu->firmware_name);
			retval = -EINVAL;
			goto exit;
		}

		dev_dbg(&fwu->rmi4_data->i2c_client->dev,
				"%s: Firmware image size = %d\n",
				__func__, fw_entry->size);

		fw_image = fw_entry->data;
	}
	if (!(fwu->force_update)) {
	retval=synaptics_check_config_id(fwu->rmi4_data,fw_image);
		if(retval!=0)
			goto exit;
	}
	parse_header(&header, fw_image);

	if (header.image_size)
		fwu->firmware_data = fw_image + FW_IMAGE_OFFSET;
	if (header.config_size) {
		fwu->config_data = fw_image + FW_IMAGE_OFFSET +
				header.image_size;
	}

	if (fwu->ext_data_source)
		flash_area = UI_FIRMWARE;
	else
		flash_area = fwu_go_nogo(&header);

	switch (flash_area) {
	case NONE:
		dev_info(&fwu->rmi4_data->i2c_client->dev,
		"%s: No need to do reflash.\n",
		__func__);
		goto exit;
	case UI_FIRMWARE:
		retval = fwu_do_reflash();
		break;
	case CONFIG_AREA:
		retval = fwu_do_write_config();
		break;
	default:
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Unknown flash area\n",
				__func__);
		goto exit;
	}

	if (retval < 0) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"%s: Failed to do reflash\n",
				__func__);
	}

	/* reset device */
	fwu_reset_device();

	/* check device status */
	retval = fwu_read_f01_device_status(&f01_device_status);
	if (retval < 0)
		goto exit;

	dev_info(&fwu->rmi4_data->i2c_client->dev, "Device is in %s mode\n",
		f01_device_status.flash_prog == 1 ? "bootloader" : "UI");
	if (f01_device_status.flash_prog)
		dev_info(&fwu->rmi4_data->i2c_client->dev, "Flash status %d\n",
				f01_device_status.status_code);

	if (f01_device_status.flash_prog) {
		dev_info(&fwu->rmi4_data->i2c_client->dev,
				"%s: Device is in flash prog mode 0x%02X\n",
				__func__, f01_device_status.status_code);
		retval = 0;
		goto exit;
	}


	pr_notice("%s: End of reflash process\n", __func__);
exit:
	if (fw_entry)
		release_firmware(fw_entry);

	kfree(fwu->firmware_name);
	return retval;
}

int synaptics_fw_updater(unsigned char *fw_data)
{
	int retval;

	if (!fwu)
		return -ENODEV;

	if (!fwu->initialized)
		return -ENODEV;

	fwu->ext_data_source = fw_data;
	fwu->config_area = UI_CONFIG_AREA;

	retval = fwu_start_reflash();

	return retval;
}
EXPORT_SYMBOL(synaptics_fw_updater);

static ssize_t fwu_sysfs_show_image(struct file *data_file,
		struct kobject *kobj, struct bin_attribute *attributes,
		char *buf, loff_t pos, size_t count)
{
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	if (count < fwu->config_size) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Not enough space (%d bytes) in buffer\n",
				__func__, count);
		return -EINVAL;
	}

	memcpy(buf, fwu->read_config_buf, fwu->config_size);

	return fwu->config_size;
}

static ssize_t fwu_sysfs_store_image(struct file *data_file,
		struct kobject *kobj, struct bin_attribute *attributes,
		char *buf, loff_t pos, size_t count)
{
	memcpy((void *)(&fwu->ext_data_source[fwu->data_pos]),
			(const void *)buf,
			count);

	fwu->data_pos += count;

	return count;
}

static ssize_t fwu_sysfs_do_reflash_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	if (sscanf(buf, "%u", &input) != 1) {
		retval = -EINVAL;
		goto exit;
	}

	if (input != 1) {
		retval = -EINVAL;
		goto exit;
	}

	retval = synaptics_fw_updater(fwu->ext_data_source);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to do reflash\n",
				__func__);
		goto exit;
	}

	retval = count;

exit:
	kfree(fwu->ext_data_source);
	fwu->ext_data_source = NULL;
	return retval;
}

static ssize_t fwu_sysfs_write_config_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	if (sscanf(buf, "%u", &input) != 1) {
		retval = -EINVAL;
		goto exit;
	}

	if (input != 1) {
		retval = -EINVAL;
		goto exit;
	}

	retval = fwu_start_write_config();
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to write config\n",
				__func__);
		goto exit;
	}

	retval = count;

exit:
	kfree(fwu->ext_data_source);
	fwu->ext_data_source = NULL;
	return retval;
}

static ssize_t fwu_sysfs_read_config_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	if (input != 1)
		return -EINVAL;

	retval = fwu_do_read_config();
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to read config\n",
				__func__);
		return retval;
	}

	return count;
}

static ssize_t fwu_sysfs_config_area_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned long config_area;

	retval = sstrtoul(buf, 10, &config_area);
	if (retval)
		return retval;

	fwu->config_area = config_area;

	return count;
}

static ssize_t fwu_sysfs_image_size_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned long size;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	retval = sstrtoul(buf, 10, &size);
	if (retval)
		return retval;

	fwu->image_size = size;
	fwu->data_pos = 0;

	kfree(fwu->ext_data_source);
	fwu->ext_data_source = kzalloc(fwu->image_size, GFP_KERNEL);
	if (!fwu->ext_data_source) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for image data\n",
				__func__);
		return -ENOMEM;
	}

	return count;
}

static ssize_t fwu_sysfs_block_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", fwu->block_size);
}

static ssize_t fwu_sysfs_firmware_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", fwu->fw_block_count);
}

static ssize_t fwu_sysfs_configuration_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", fwu->config_block_count);
}

static ssize_t fwu_sysfs_perm_config_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", fwu->perm_config_block_count);
}

static ssize_t fwu_sysfs_bl_config_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", fwu->bl_config_block_count);
}

static ssize_t fwu_sysfs_disp_config_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", fwu->disp_config_block_count);
}

static void synaptics_rmi4_fwu_attn(struct synaptics_rmi4_data *rmi4_data,
		unsigned char intr_mask)
{
	if (!fwu)
		return;

	if (fwu->intr_mask & intr_mask)
		fwu_read_f34_flash_status();

	return;
}

static void synaptics_rmi4_fwu_work(struct work_struct *work)
{
	//printk("synaptics_rmi4_fwu_work\n");
	//fwu_start_reflash();
}

int syna_get_fw_ver(struct i2c_client *client, char *pfwfilename)
{
	//u32 fwsize = 0;
	int ret=0;
	int fwver=-1;
	char *charcmp="";
	const unsigned char *fw_image;
	const struct firmware *fw_entry = NULL;
	unsigned long firmware_imgsize;
	unsigned char *config_imgdata = NULL;
	//u32 temp1,temp2;
	if ( !client || !pfwfilename )
		return -1;
	if(!strcmp(pfwfilename,charcmp))
	{
		printk("syna_get_fw_ver pfwfilename is null\n");
		return -1;
	}
	printk("syna_get_fw_ver fw name:%s\n",pfwfilename);
	//fwsize = syna_getfwsize(pfwfilename);
  	//pbt_buf = (unsigned char *) kmalloc(fwsize+1,GFP_ATOMIC);
	//if(syna_getfwinfo(pfwfilename, pbt_buf)){
	//	pr_err("get firmware information failed!\n");
	//	ret = -1;
	//	goto malloc_error1;
	//}
    ret = request_firmware(&fw_entry, pfwfilename,
    	&fwu->rmi4_data->i2c_client->dev);
    if (ret != 0) {
    	dev_err(&fwu->rmi4_data->i2c_client->dev,
    		"%s: Firmware image %s not available\n",
    		__func__, fwu->firmware_name);
    	ret = -1;
    	return ret;
    }
	
    dev_err(&fwu->rmi4_data->i2c_client->dev,
    	"%s: Firmware image size = %d\n",
    	__func__, fw_entry->size);
    
    fw_image = fw_entry->data;
	
	printk("syna_get_fw_ver fw_image:0x%x%x\n",(int)fw_image>>16,(int)fw_image);
	firmware_imgsize = extract_uint(fw_image+8);
	printk("syna_get_fw_ver firmware_imgsize:0x%x%x\n",(int)firmware_imgsize>>16,(int)firmware_imgsize);
	config_imgdata = (unsigned char*) ( fw_image + 0x100 + firmware_imgsize );
	printk("syna_get_fw_ver config_imgdata:0x%x%x\n",(int)config_imgdata>>16,(int)config_imgdata);
	//temp1=*(config_imgdata+2);
	//temp2=*(config_imgdata+3);
	//fwver = (temp1<<8)|temp2;
	fwver = ((*(config_imgdata+2))<<8)|(*(config_imgdata+3));
	printk("syna fw ver:%x \n",fwver);
	
	//return -1;
	if (fw_entry)
		release_firmware(fw_entry);
	
	return fwver;

}

static int synaptics_rmi4_fwu_init(struct synaptics_rmi4_data *rmi4_data)
{
	int retval = 0;
	unsigned char attr_count;
	struct pdt_properties pdt_props;
	printk("synaptics_rmi4_fwu_init\n");
	
	if(rmi4_data==NULL){
		printk("%s:rmi4_data is null\n",__func__);
		return -1;
	}	
	fwu = kzalloc(sizeof(*fwu), GFP_KERNEL);
	if (!fwu) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for fwu\n",
				__func__);
		goto exit;
	}

	fwu->fn_ptr = kzalloc(sizeof(*(fwu->fn_ptr)), GFP_KERNEL);
	if (!fwu->fn_ptr) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for fn_ptr\n",
				__func__);
		retval = -ENOMEM;
		goto exit_free_fwu;
	}

	fwu->rmi4_data = rmi4_data;
	fwu->fn_ptr->read = rmi4_data->i2c_read;
	fwu->fn_ptr->write = rmi4_data->i2c_write;
	fwu->fn_ptr->enable = rmi4_data->irq_enable;

	retval = fwu->fn_ptr->read(rmi4_data,
			PDT_PROPS,
			pdt_props.data,
			sizeof(pdt_props.data));
	if (retval < 0) {
		dev_dbg(&rmi4_data->i2c_client->dev,
				"%s: Failed to read PDT properties, assuming 0x00\n",
				__func__);
	} else if (pdt_props.has_bsr) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Reflash for LTS not currently supported\n",
				__func__);
		goto exit_free_mem;
	}

	retval = fwu_scan_pdt();
	if (retval < 0){
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: fwu_scan_pdt failed\n",
				__func__);		
		goto exit_free_mem;
}
	fwu->productinfo1 = rmi4_data->rmi4_mod_info.product_info[0];
	fwu->productinfo2 = rmi4_data->rmi4_mod_info.product_info[1];

	memcpy(fwu->product_id, rmi4_data->rmi4_mod_info.product_id_string,
			SYNAPTICS_RMI4_PRODUCT_ID_SIZE);
	fwu->product_id[SYNAPTICS_RMI4_PRODUCT_ID_SIZE] = 0;

	dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: F01 product info: 0x%04x 0x%04x\n",
			__func__, fwu->productinfo1, fwu->productinfo2);
	dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: F01 product ID: %s\n",
			__func__, fwu->product_id);

	retval = fwu_read_f34_queries();
	if (retval < 0){
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: fwu_read_f34_queries failed\n",
				__func__);		
		goto exit_free_mem;
	}
	fwu->initialized = true;
	fwu->force_update = FORCE_UPDATE;

	retval = sysfs_create_bin_file(&rmi4_data->input_dev->dev.kobj,
			&dev_attr_data);
	printk("huangjinyu name:%s\n",rmi4_data->input_dev->dev.kobj.name);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to create sysfs bin file\n",
				__func__);
		goto exit_free_mem;
	}

	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++) {
		retval = sysfs_create_file(&rmi4_data->input_dev->dev.kobj,
				&attrs[attr_count].attr);
		if (retval < 0) {
			dev_err(&rmi4_data->i2c_client->dev,
					"%s: Failed to create sysfs attributes\n",
					__func__);
			retval = -ENODEV;
			goto exit_remove_attrs;
		}
	}

#ifdef INSIDE_FIRMWARE_UPDATE
	fwu->fwu_workqueue = create_singlethread_workqueue("fwu_workqueue");
	INIT_DELAYED_WORK(&fwu->fwu_work, synaptics_rmi4_fwu_work);
	queue_delayed_work(fwu->fwu_workqueue,
			&fwu->fwu_work,
			msecs_to_jiffies(1000));
#endif

	return 0;

exit_remove_attrs:
for (attr_count--; attr_count >= 0; attr_count--) {
	sysfs_remove_file(&rmi4_data->input_dev->dev.kobj,
			&attrs[attr_count].attr);
}

sysfs_remove_bin_file(&rmi4_data->input_dev->dev.kobj, &dev_attr_data);

exit_free_mem:
	kfree(fwu->fn_ptr);

exit_free_fwu:
	kfree(fwu);
	fwu = NULL;

exit:
	return retval;
}

static void synaptics_rmi4_fwu_remove(struct synaptics_rmi4_data *rmi4_data)
{
	unsigned char attr_count;

	if (!fwu)
		goto exit;

	sysfs_remove_bin_file(&rmi4_data->input_dev->dev.kobj, &dev_attr_data);

	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++) {
		sysfs_remove_file(&rmi4_data->input_dev->dev.kobj,
				&attrs[attr_count].attr);
	}

	kfree(fwu->fn_ptr);
	kfree(fwu);
	fwu = NULL;

exit:
	complete(&fwu_remove_complete);

	return;
}
//static struct i2c_client *syna_i2c_client;
//static bool fw_update_mode;
static char fwname[128];

//upgrade from app.bin
static ssize_t syna_fwupdate_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	//struct i2c_client *client = syna_i2c_client;

	memset(fwname, 0, sizeof(fwname));
	sprintf(fwname, "%s", buf);
	fwname[count-1] = '\0';
	syna_file_name=fwname;

	fwu->force_update = false;
	if(0 == fwu_start_reflash())
		pr_info("%s: update success \n", __func__);
	else
		pr_info("%s: update fail  \n", __func__);

	return count;
}

static DEVICE_ATTR(synafwupdate, S_IRUGO|S_IWUSR, NULL, syna_fwupdate_store);


static ssize_t syna_force_fwupdate_store(struct device *dev,
						struct device_attribute *attr,
							const char *buf, size_t count)
{
	//char fwname[128];
	//struct i2c_client *client = syna_i2c_client;
	memset(fwname, 0, sizeof(fwname));
	sprintf(fwname, "%s", buf);
	fwname[count-1] = '\0';

	syna_file_name=fwname;

	fwu->force_update = true;

	if(0 == fwu_start_reflash())
		pr_info("%s: update success \n", __func__);
	else
		pr_info("%s: update fail  \n", __func__);

	return count;
}

static DEVICE_ATTR(synafwupdate_force, S_IRUGO|S_IWUSR, NULL, syna_force_fwupdate_store);
extern struct kobject *firmware_kobj;

int syna_fwupdate_init(struct i2c_client *client)
{
	int ret;
	struct kobject * fts_fw_kobj=NULL;


	if (!client)
		return 0;

	//syna_i2c_client = client;
	//fw_update_mode = false;

	fts_fw_kobj = kobject_get(firmware_kobj);
	if (fts_fw_kobj == NULL) {
		fts_fw_kobj = kobject_create_and_add("firmware", NULL);
		if (fts_fw_kobj == NULL) {
			pr_err("%s: subsystem_register failed\n", __func__);
			ret = -ENOMEM;
			return ret;
		}
	}

 	ret=sysfs_create_file(fts_fw_kobj, &dev_attr_synafwupdate.attr);
	if (ret) {
		pr_err("%s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	
	ret=sysfs_create_file(fts_fw_kobj, &dev_attr_synafwupdate_force.attr);
	if (ret) {
		pr_err("%s: sysfs_create_file failed\n", __func__);
		return ret;
	}	

	pr_info("%s:synaptics firmware update init succeed!\n", __func__);
	return 0;

}


int syna_fwupdate_deinit(struct i2c_client *client)
{
	struct kobject * fts_fw_kobj=NULL;

	fts_fw_kobj = kobject_get(firmware_kobj);
	if ( !firmware_kobj ){
		printk("%s: error get kobject\n", __func__);
		return -1;
	}
	
	sysfs_remove_file(firmware_kobj, &dev_attr_synafwupdate.attr);
	//	kobject_del(virtual_key_kobj);

	return 0;
}

int  rmi4_fw_update_module_init(void)
{
	printk("rmi4_fw_update_module_init\n");
	synaptics_rmi4_new_function(RMI_FW_UPDATER, true,
			synaptics_rmi4_fwu_init,
			synaptics_rmi4_fwu_remove,
			synaptics_rmi4_fwu_attn);
	return 0;
}


//module_init(rmi4_fw_update_module_init);
//module_exit(rmi4_fw_update_module_exit);

MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics DSX FW Update Module");
MODULE_LICENSE("GPL v2");
