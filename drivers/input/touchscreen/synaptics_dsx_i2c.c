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
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/input/synaptics_dsx.h>
#include "synaptics_dsx_i2c.h"
#ifdef KERNEL_ABOVE_2_6_38
#include <linux/input/mt.h>
#endif
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/fb.h>
#include "touchscreen_fw.h"
#include <linux/input/touchscreen_pm.h>
#include "tpd_fw.h"

//#include <linux/sched.h>

//void show_stack(struct task_struct *task, unsigned long *sp);

struct regulator {
	struct device *dev;
	struct list_head list;
	int uA_load;
	int min_uV;
	int max_uV;
	int enabled;
	char *supply_name;
	struct device_attribute dev_attr;
	struct regulator_dev *rdev;
	struct dentry *debugfs;
};

#define DRIVER_NAME "syna-touchscreen"
#define INPUT_PHYS_NAME "syna-touchscreen/input0"

#ifdef KERNEL_ABOVE_2_6_38
//#define TYPE_B_PROTOCOL
#endif

#define NO_0D_WHILE_2D
/*
#define REPORT_2D_Z
*/
#define REPORT_2D_W

#define RPT_TYPE (1 << 0)
#define RPT_X_LSB (1 << 1)
#define RPT_X_MSB (1 << 2)
#define RPT_Y_LSB (1 << 3)
#define RPT_Y_MSB (1 << 4)
#define RPT_Z (1 << 5)
#define RPT_WX (1 << 6)
#define RPT_WY (1 << 7)
#define RPT_DEFAULT (RPT_TYPE | RPT_X_LSB | RPT_X_MSB | RPT_Y_LSB | RPT_Y_MSB)

#define EXP_FN_WORK_DELAY_MS 1000 /* ms */
#define POLLING_PERIOD 1 /* ms */
#define SYN_I2C_RETRY_TIMES 10
#define MAX_ABS_MT_TOUCH_MAJOR 15

#define F01_STD_QUERY_LEN 21
#define F01_BUID_ID_OFFSET 18
#define F11_STD_QUERY_LEN 9
#define F11_STD_CTRL_LEN 10
#define F11_STD_DATA_LEN 12

#define NORMAL_OPERATION (0 << 0)
#define SENSOR_SLEEP (1 << 0)
#define NO_SLEEP_OFF (0 << 2)
#define NO_SLEEP_ON (1 << 2)

enum device_status {
	STATUS_NO_ERROR = 0x00,
	STATUS_RESET_OCCURRED = 0x01,
	STATUS_INVALID_CONFIG = 0x02,
	STATUS_DEVICE_FAILURE = 0x03,
	STATUS_CONFIG_CRC_FAILURE = 0x04,
	STATUS_FIRMWARE_CRC_FAILURE = 0x05,
	STATUS_CRC_IN_PROGRESS = 0x06
};

static int synaptics_rmi4_i2c_read(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data,
		unsigned short length);

static int synaptics_rmi4_i2c_write(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data,
		unsigned short length);

static int synaptics_rmi4_reset_device(struct synaptics_rmi4_data *rmi4_data);

static void synaptics_rmi4_swap_axis(struct synaptics_rmi4_data *rmi4_data);

#ifdef CONFIG_PM
static ssize_t synaptics_rmi4_full_pm_cycle_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_full_pm_cycle_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static int synaptics_rmi4_suspend(struct device *dev);

static int synaptics_rmi4_resume(struct device *dev);
#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
				unsigned long event, void *data);

#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void synaptics_rmi4_early_suspend(struct early_suspend *h);

static void synaptics_rmi4_late_resume(struct early_suspend *h);
#endif
#endif
int synaptics_rmi4_suspend_pm(void);

int synaptics_rmi4_resume_pm(void);

static ssize_t synaptics_rmi4_f01_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t synaptics_rmi4_f01_productinfo_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_f01_buildid_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_f01_flashprog_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_0dbutton_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_0dbutton_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t synaptics_rmi4_flipx_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_flipx_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t synaptics_rmi4_flipy_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_flipy_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t synaptics_rmi4_swap_axes_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

struct synaptics_rmi4_f01_device_status {
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

struct synaptics_rmi4_f1a_query {
	union {
		struct {
			unsigned char max_button_count:3;
			unsigned char reserved:5;
			unsigned char has_general_control:1;
			unsigned char has_interrupt_enable:1;
			unsigned char has_multibutton_select:1;
			unsigned char has_tx_rx_map:1;
			unsigned char has_perbutton_threshold:1;
			unsigned char has_release_threshold:1;
			unsigned char has_strongestbtn_hysteresis:1;
			unsigned char has_filter_strength:1;
		} __packed;
		unsigned char data[2];
	};
};

struct synaptics_rmi4_f1a_control_0 {
	union {
		struct {
			unsigned char multibutton_report:2;
			unsigned char filter_mode:2;
			unsigned char reserved:4;
		} __packed;
		unsigned char data[1];
	};
};

struct synaptics_rmi4_f1a_control_3_4 {
	unsigned char transmitterbutton;
	unsigned char receiverbutton;
};

struct synaptics_rmi4_f1a_control {
	struct synaptics_rmi4_f1a_control_0 general_control;
	unsigned char *button_int_enable;
	unsigned char *multi_button;
	struct synaptics_rmi4_f1a_control_3_4 *electrode_map;
	unsigned char *button_threshold;
	unsigned char button_release_threshold;
	unsigned char strongest_button_hysteresis;
	unsigned char filter_strength;
};

struct synaptics_rmi4_f1a_handle {
	int button_bitmask_size;
	unsigned char button_count;
	unsigned char valid_button_count;
	unsigned char *button_data_buffer;
	unsigned char *button_map;
	struct synaptics_rmi4_f1a_query button_query;
	struct synaptics_rmi4_f1a_control button_control;
};

struct synaptics_rmi4_f54_query {
	union {
		struct {
			/* query 0 */
			unsigned char num_of_rx_electrodes;
			/* query 1 */
			unsigned char num_of_tx_electrodes;
			/* query 2 */
			unsigned char f54_query2_b0__1:2;
			unsigned char has_baseline:1;
			unsigned char has_image8:1;
			unsigned char f54_query2_b4__5:2;
			unsigned char has_image16:1;
			unsigned char f54_query2_b7:1;
			/* queries 3.0 and 3.1 */
			unsigned short clock_rate;
			/* query 4 */
			unsigned char touch_controller_family;
			/* query 5 */
			unsigned char has_pixel_touch_threshold_adjustment:1;
			unsigned char f54_query5_b1__7:7;
			/* query 6 */
			unsigned char has_sensor_assignment:1;
			unsigned char has_interference_metric:1;
			unsigned char has_sense_frequency_control:1;
			unsigned char has_firmware_noise_mitigation:1;
			unsigned char has_ctrl11:1;
			unsigned char has_two_byte_report_rate:1;
			unsigned char has_one_byte_report_rate:1;
			unsigned char has_relaxation_control:1;
		} __packed;
		unsigned char data[8];
	};
};

struct synaptics_rmi4_exp_fn {
	enum exp_fn fn_type;
	bool inserted;
	int (*func_init)(struct synaptics_rmi4_data *rmi4_data);
	void (*func_remove)(struct synaptics_rmi4_data *rmi4_data);
	void (*func_attn)(struct synaptics_rmi4_data *rmi4_data,
			unsigned char intr_mask);
	struct list_head link;
};

static struct device_attribute attrs[] = {
#ifdef CONFIG_PM
	__ATTR(full_pm_cycle, 0644, //(S_IRUGO | S_IWUGO),
			synaptics_rmi4_full_pm_cycle_show,
			synaptics_rmi4_full_pm_cycle_store),
#endif
	__ATTR(reset, S_IWUSR | S_IWGRP, //zhangzhao for cts test 2014-4-25
			NULL,//synaptics_rmi4_show_error,
			synaptics_rmi4_f01_reset_store),
	__ATTR(productinfo, 0644, //wangminrong for cts test
			synaptics_rmi4_f01_productinfo_show,
			synaptics_rmi4_store_error),
	__ATTR(buildid, 0644, //wangminrong for cts test
			synaptics_rmi4_f01_buildid_show,
			synaptics_rmi4_store_error),
	__ATTR(flashprog, 0644, //wangminrong for cts test
			synaptics_rmi4_f01_flashprog_show,
			synaptics_rmi4_store_error),
	__ATTR(0dbutton, 0644, //wangminrong for cts test
			synaptics_rmi4_0dbutton_show,
			synaptics_rmi4_0dbutton_store),
	__ATTR(flipx, 0644, //wangminrong for cts test
			synaptics_rmi4_flipx_show,
			synaptics_rmi4_flipx_store),
	__ATTR(flipy, 0644, //wangminrong for cts test
			synaptics_rmi4_flipy_show,
			synaptics_rmi4_flipy_store),
	__ATTR(swapaxes, S_IWUSR | S_IWGRP, //zhangzhao for cts test 2014-4-25
			NULL,//synaptics_rmi4_show_error,
			synaptics_rmi4_swap_axes_store),
};

struct synaptics_rmi4_exp_fn_data {
	bool initialized;
	bool queue_work;
	struct mutex mutex;
	struct list_head list;
	struct delayed_work work;
	struct workqueue_struct *workqueue;
	struct synaptics_rmi4_data *rmi4_data;
};

static struct synaptics_rmi4_exp_fn_data exp_data;


#ifdef CONFIG_PM
static ssize_t synaptics_rmi4_full_pm_cycle_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			rmi4_data->full_pm_cycle);
}

static ssize_t synaptics_rmi4_full_pm_cycle_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	rmi4_data->full_pm_cycle = input > 0 ? 1 : 0;

	return count;
}
#endif

static ssize_t synaptics_rmi4_f01_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int reset;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &reset) != 1)
		return -EINVAL;

	if (reset != 1)
		return -EINVAL;

	retval = synaptics_rmi4_reset_device(rmi4_data);
	if (retval < 0) {
		dev_err(dev,
				"%s: Failed to issue reset command, error = %d\n",
				__func__, retval);
		return retval;
	}

	return count;
}

static ssize_t synaptics_rmi4_f01_productinfo_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "0x%02x 0x%02x\n",
			(rmi4_data->rmi4_mod_info.product_info[0]),
			(rmi4_data->rmi4_mod_info.product_info[1]));
}

static ssize_t synaptics_rmi4_f01_buildid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned int build_id;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	build_id = (unsigned int)rmi->build_id[0] +
			(unsigned int)rmi->build_id[1] * 0x100 +
			(unsigned int)rmi->build_id[2] * 0x10000;

	return snprintf(buf, PAGE_SIZE, "%u\n",
			build_id);
}

static ssize_t synaptics_rmi4_f01_flashprog_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int retval;
	struct synaptics_rmi4_f01_device_status device_status;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr,
			device_status.data,
			sizeof(device_status.data));
	if (retval < 0) {
		dev_err(dev,
				"%s: Failed to read device status, error = %d\n",
				__func__, retval);
		return retval;
	}

	return snprintf(buf, PAGE_SIZE, "%u\n",
			device_status.flash_prog);
}

static ssize_t synaptics_rmi4_0dbutton_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			rmi4_data->button_0d_enabled);
}

static ssize_t synaptics_rmi4_0dbutton_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;
	unsigned char ii;
	unsigned char intr_enable;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	input = input > 0 ? 1 : 0;

	if (rmi4_data->button_0d_enabled == input)
		return count;

	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F1A) {
				ii = fhandler->intr_reg_num;

				retval = synaptics_rmi4_i2c_read(rmi4_data,
						rmi4_data->f01_ctrl_base_addr +
						1 + ii,
						&intr_enable,
						sizeof(intr_enable));
				if (retval < 0)
					return retval;

				if (input == 1)
					intr_enable |= fhandler->intr_mask;
				else
					intr_enable &= ~fhandler->intr_mask;

				retval = synaptics_rmi4_i2c_write(rmi4_data,
						rmi4_data->f01_ctrl_base_addr +
						1 + ii,
						&intr_enable,
						sizeof(intr_enable));
				if (retval < 0)
					return retval;
			}
		}
	}

	rmi4_data->button_0d_enabled = input;

	return count;
}

static ssize_t synaptics_rmi4_flipx_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			rmi4_data->flip_x);
}

static ssize_t synaptics_rmi4_flipx_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	rmi4_data->flip_x = input > 0 ? 1 : 0;

	return count;
}

static ssize_t synaptics_rmi4_flipy_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			rmi4_data->flip_y);
}

static ssize_t synaptics_rmi4_flipy_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	rmi4_data->flip_y = input > 0 ? 1 : 0;

	return count;
}

static ssize_t synaptics_rmi4_swap_axes_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	if (input != 1)
		return -EINVAL;

	synaptics_rmi4_swap_axis(rmi4_data);

	return count;
}

static void synaptics_rmi4_swap_axis(struct synaptics_rmi4_data *rmi4_data)
{
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_device_info *rmi;
	struct i2c_client *client = rmi4_data->i2c_client;
	struct synaptics_rmi4_fn *sensor_tuning = NULL;

	rmi = &(rmi4_data->rmi4_mod_info);

	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			printk("function number 0x%x\n", fhandler->fn_number);
			if (fhandler->fn_number == SYNAPTICS_RMI4_F55)
				sensor_tuning = fhandler;
			if (sensor_tuning == NULL && 
				fhandler->fn_number == SYNAPTICS_RMI4_F54)
				sensor_tuning = fhandler;
		}
	}

	if (sensor_tuning != NULL) {
		int retval = 0;
		unsigned char val;
		unsigned short swap_ctrl_addr;
		unsigned short offset;

		if (sensor_tuning->fn_number == SYNAPTICS_RMI4_F55)
			swap_ctrl_addr = sensor_tuning->full_addr.ctrl_base + 0;
		else {
			struct synaptics_rmi4_f54_query f54_query;
			retval = synaptics_rmi4_i2c_read(rmi4_data,
				sensor_tuning->full_addr.query_base,
				(unsigned char *)f54_query.data,
				sizeof(f54_query.data));
			if (retval < 0)
				dev_err(&client->dev,
				"%s: Failed to read swap control registers\n",
				__func__);

			/* general ctrl 0 */
			offset = 1;
			/* ctrl 1/4/5/6/8.0/8.1/9*/
			if (f54_query.touch_controller_family == 0x00 ||
				f54_query.touch_controller_family == 0x01)
				offset += 7;
			/* ctrl 2/2.1 */
			offset += 2;
			/* ctrl 3 */
			if (f54_query.has_pixel_touch_threshold_adjustment)
				offset++;	
			/* ctrl 7*/
			if (f54_query.touch_controller_family == 0x01)
					offset += 1;
			/* ctrl 10 */
			if (f54_query.has_interference_metric)
				offset++;	
			/* ctrl 11/11.0 */
			if (f54_query.has_ctrl11)
				offset +=2;
			/* ctrl 12/13 */
			if (f54_query.has_relaxation_control)
				offset +=2;
			if (!f54_query.has_sensor_assignment)
				dev_err(&client->dev,
				"%s: Sensor assignment properties not exist\n",
				__func__);
			swap_ctrl_addr = sensor_tuning->full_addr.ctrl_base + offset;
		}
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				swap_ctrl_addr,
				(unsigned char *)&val,
				sizeof(val));
		if (retval < 0)
			dev_err(&client->dev,
			"%s: Failed to read swap control registers\n",
			__func__);

		val = (val & 0xFE) | (!val & 0x01);

		dev_info(&client->dev,
			"swap value :0x%x, rmi address 0x%02X\n",
			val, swap_ctrl_addr);

		retval = synaptics_rmi4_i2c_write(rmi4_data,
				swap_ctrl_addr,
				(unsigned char *)&val,
				sizeof(val));
		if (retval < 0)
			dev_err(&client->dev,
			"%s: Failed to write swap control registers\n",
			__func__);
	}else
		dev_err(&client->dev,
			"%s: Firmware not support swap function\n",
			__func__);
}

 /**
 * synaptics_rmi4_set_page()
 *
 * Called by synaptics_rmi4_i2c_read() and synaptics_rmi4_i2c_write().
 *
 * This function writes to the page select register to switch to the
 * assigned page.
 */
static int synaptics_rmi4_set_page(struct synaptics_rmi4_data *rmi4_data,
		unsigned int address)
{
	int retval = 0;
	unsigned char retry;
	unsigned char buf[PAGE_SELECT_LEN];
	unsigned char page;
	struct i2c_client *i2c = rmi4_data->i2c_client;

	page = ((address >> 8) & MASK_8BIT);
	if (page != rmi4_data->current_page) {
		buf[0] = MASK_8BIT;
		buf[1] = page;
		for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
			retval = i2c_master_send(i2c, buf, PAGE_SELECT_LEN);
			if (retval != PAGE_SELECT_LEN) {
				dev_err(&i2c->dev,
						"%s: I2C retry %d\n",
						__func__, retry + 1);
				msleep(20);
			} else {
				rmi4_data->current_page = page;
				break;
			}
		}
	} else
		return PAGE_SELECT_LEN;
	return (retval == PAGE_SELECT_LEN) ? retval : -EIO;
}

 /**
 * synaptics_rmi4_i2c_read()
 *
 * Called by various functions in this driver, and also exported to
 * other expansion Function modules such as rmi_dev.
 *
 * This function reads data of an arbitrary length from the sensor,
 * starting from an assigned register address of the sensor, via I2C
 * with a retry mechanism.
 */
static int synaptics_rmi4_i2c_read(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char retry;
	unsigned char buf;
	struct i2c_msg msg[] = {
		{
			.addr = rmi4_data->i2c_client->addr,
			.flags = 0,
			.len = 1,
			.buf = &buf,
		},
		{
			.addr = rmi4_data->i2c_client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = data,
		},
	};

	buf = addr & MASK_8BIT;

	mutex_lock(&(rmi4_data->rmi4_io_ctrl_mutex));

	retval = synaptics_rmi4_set_page(rmi4_data, addr);
	if (retval != PAGE_SELECT_LEN)
		goto exit;

	for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(rmi4_data->i2c_client->adapter, msg, 2) == 2) {
			retval = length;
			break;
		}
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: I2C retry %d\n",
				__func__, retry + 1);
		msleep(20);
	}

	if (retry == SYN_I2C_RETRY_TIMES) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: I2C read over retry limit\n",
				__func__);
		retval = -EIO;
	}

exit:
	mutex_unlock(&(rmi4_data->rmi4_io_ctrl_mutex));

	return retval;
}

 /**
 * synaptics_rmi4_i2c_write()
 *
 * Called by various functions in this driver, and also exported to
 * other expansion Function modules such as rmi_dev.
 *
 * This function writes data of an arbitrary length to the sensor,
 * starting from an assigned register address of the sensor, via I2C with
 * a retry mechanism.
 */
static int synaptics_rmi4_i2c_write(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char retry;
	unsigned char buf[length + 1];
	struct i2c_msg msg[] = {
		{
			.addr = rmi4_data->i2c_client->addr,
			.flags = 0,
			.len = length + 1,
			.buf = buf,
		}
	};

	mutex_lock(&(rmi4_data->rmi4_io_ctrl_mutex));

	retval = synaptics_rmi4_set_page(rmi4_data, addr);
	if (retval != PAGE_SELECT_LEN)
		goto exit;

	buf[0] = addr & MASK_8BIT;
	memcpy(&buf[1], &data[0], length);

	for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(rmi4_data->i2c_client->adapter, msg, 1) == 1) {
			retval = length;
			break;
		}
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: I2C retry %d\n",
				__func__, retry + 1);
		msleep(20);
	}

	if (retry == SYN_I2C_RETRY_TIMES) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: I2C write over retry limit\n",
				__func__);
		retval = -EIO;
	}

exit:
	mutex_unlock(&(rmi4_data->rmi4_io_ctrl_mutex));

	return retval;
}

static void to_origin_position(struct synaptics_rmi4_data *rmi4_data, int *x, int *y)
{
	if(rmi4_data->sensor_max_x == 0 || rmi4_data->sensor_max_chip_y == 0) {
		rmi4_data->sensor_max_x = 0xff;
		rmi4_data->sensor_max_chip_y = 0xff;
		dev_err(&rmi4_data->i2c_client->dev,
			"%s: TP Chip deivce max x or y Error.\n",
			__func__);
	}
	
	*x = (*x) * rmi4_data->sensor_origin_x / rmi4_data->sensor_max_x;
	*y = (*y) * rmi4_data->sensor_origin_y / rmi4_data->sensor_max_chip_y;
}

 /**
 * synaptics_rmi4_f11_abs_report()
 *
 * Called by synaptics_rmi4_report_touch() when valid Function $11
 * finger data has been detected.
 *
 * This function reads the Function $11 data registers, determines the
 * status of each finger supported by the Function, processes any
 * necessary coordinate manipulation, reports the finger data to
 * the input subsystem, and returns the number of fingers detected.
 */
 unsigned long reportdot=0;
static int synaptics_rmi4_f11_abs_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char touch_count = 0; /* number of touch points */
	unsigned char reg_index;
	unsigned char finger;
	unsigned char fingers_supported;
	unsigned char num_of_finger_status_regs;
	unsigned char finger_shift;
	unsigned char finger_status;
	unsigned char data_reg_blk_size;
	unsigned char finger_status_reg[3];
	unsigned char data[F11_STD_DATA_LEN];
	unsigned short data_addr;
	unsigned short data_offset;
	int x;
	int y;
	int wx;
	int wy;
	int z;

	/*
	 * The number of finger status registers is determined by the
	 * maximum number of fingers supported - 2 bits per finger. So
	 * the number of finger status registers to read is:
	 * register_count = ceil(max_num_of_fingers / 4)
	 */
	fingers_supported = fhandler->num_of_data_points;
	num_of_finger_status_regs = (fingers_supported + 3) / 4;
	data_addr = fhandler->full_addr.data_base;
	data_reg_blk_size = fhandler->size_of_data_register_block;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_addr,
			finger_status_reg,
			num_of_finger_status_regs);
	if (retval < 0)
		return 0;

	mutex_lock(&(rmi4_data->rmi4_report_mutex));

	for (finger = 0; finger < fingers_supported; finger++) {
		reg_index = finger / 4;
		finger_shift = (finger % 4) * 2;
		finger_status = (finger_status_reg[reg_index] >> finger_shift)
				& MASK_2BIT;

		/*
		 * Each 2-bit finger status field represents the following:
		 * 00 = finger not present
		 * 01 = finger present and data accurate
		 * 10 = finger present but data may be inaccurate
		 * 11 = reserved
		 */
#ifdef TYPE_B_PROTOCOL
		input_mt_slot(rmi4_data->input_dev, finger);
		input_mt_report_slot_state(rmi4_data->input_dev,
				MT_TOOL_FINGER, finger_status != 0);
#endif

		if (finger_status) {
			data_offset = data_addr +
					num_of_finger_status_regs +
					(finger * data_reg_blk_size);
			retval = synaptics_rmi4_i2c_read(rmi4_data,
					data_offset,
					data,
					data_reg_blk_size);
			if (retval < 0)
				{
				touch_count = 0;
				goto exit;
				}
			
			x = (data[0] << 4) | (data[2] & MASK_4BIT);
			y = (data[1] << 4) | ((data[2] >> 4) & MASK_4BIT);
			wx = (data[3] & MASK_4BIT);
			wy = (data[3] >> 4) & MASK_4BIT;
			z = data[4];

			if (rmi4_data->flip_x)
				x = rmi4_data->sensor_max_x - x;
			if (rmi4_data->flip_y)
				y = rmi4_data->sensor_max_y - y;

			dev_dbg(&rmi4_data->i2c_client->dev,
					"%s: Finger %d:\n"
					"status = 0x%02x\n"
					"x = %d\n"
					"y = %d\n"
					"wx = %d\n"
					"wy = %d\n",
					__func__, finger,
					finger_status,
					x, y, wx, wy);

		//	pr_err("wangminrong x %d\r\n,y %d\r\n",x,y);
			to_origin_position(rmi4_data, &x, &y);
			input_report_key(rmi4_data->input_dev,
					BTN_TOUCH, 1);
#ifdef TYPE_B_PROTOCOL			
			input_report_key(rmi4_data->input_dev,
					BTN_TOOL_FINGER, 1);
#endif
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_X, x);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_Y, y);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_PRESSURE, z);

#ifdef REPORT_2D_W
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MAJOR, max(wx, wy));
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MINOR, min(wx, wy));
#endif
#ifndef TYPE_B_PROTOCOL
			input_mt_sync(rmi4_data->input_dev);
#endif
			touch_count++;
			reportdot++;
			if(0==reportdot%60)
			{
			      printk("[TPD]:finger=%d,status=%d X=%d,Y=%d\n" ,finger,finger_status,x,y);		 			
			 }
			else
			{ 
			     if(reportdot>80000)
					reportdot=0;
			}
		}
	}
	if (touch_count == 0) {
		input_report_key(rmi4_data->input_dev,
				BTN_TOUCH, 0);
#ifdef TYPE_B_PROTOCOL
		input_report_key(rmi4_data->input_dev,
				BTN_TOOL_FINGER, 0);
#endif
#ifndef TYPE_B_PROTOCOL
		input_mt_sync(rmi4_data->input_dev);
#else
	/* sync after groups of events */
	#ifdef KERNEL_ABOVE_3_7
	input_mt_sync_frame(rmi4_data->input_dev);
	#endif
#endif
		}
	input_sync(rmi4_data->input_dev);

exit:
	mutex_unlock(&(rmi4_data->rmi4_report_mutex));

	return touch_count;
}

static void synaptics_rmi4_f1a_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char button;
	unsigned char index;
	unsigned char shift;
	unsigned char status;
	unsigned char *data;
	unsigned short data_addr = fhandler->full_addr.data_base;
	struct synaptics_rmi4_f1a_handle *f1a = fhandler->data;
	static unsigned char do_once = 1;
	static bool current_status[MAX_NUMBER_OF_BUTTONS];
#ifdef NO_0D_WHILE_2D
	static bool before_2d_status[MAX_NUMBER_OF_BUTTONS];
	static bool while_2d_status[MAX_NUMBER_OF_BUTTONS];
#endif

	if (do_once) {
		memset(current_status, 0, sizeof(current_status));
#ifdef NO_0D_WHILE_2D
		memset(before_2d_status, 0, sizeof(before_2d_status));
		memset(while_2d_status, 0, sizeof(while_2d_status));
#endif
		do_once = 0;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_addr,
			f1a->button_data_buffer,
			f1a->button_bitmask_size);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to read button data registers\n",
				__func__);
		return;
	}

	data = f1a->button_data_buffer;

	for (button = 0; button < f1a->valid_button_count; button++) {
		index = button / 8;
		shift = button % 8;
		status = ((data[index] >> shift) & MASK_1BIT);

		if (current_status[button] == status)
			continue;
		else
			current_status[button] = status;

		dev_dbg(&rmi4_data->i2c_client->dev,
				"%s: Button %d (code %d) ->%d\n",
				__func__, button,
				f1a->button_map[button],
				status);
#ifdef NO_0D_WHILE_2D
		if (rmi4_data->fingers_on_2d == false) {
			if (status == 1) {
				before_2d_status[button] = 1;
			} else {
				if (while_2d_status[button] == 1) {
					while_2d_status[button] = 0;
					continue;
				} else {
					before_2d_status[button] = 0;
				}
			}
			input_report_key(rmi4_data->input_dev,
					f1a->button_map[button],
					status);
		} else {
			if (before_2d_status[button] == 1) {
				before_2d_status[button] = 0;
				input_report_key(rmi4_data->input_dev,
						f1a->button_map[button],
						status);
			} else {
				if (status == 1)
					while_2d_status[button] = 1;
				else
					while_2d_status[button] = 0;
			}
		}
#else
		input_report_key(rmi4_data->input_dev,
				f1a->button_map[button],
				status);
#endif
	}

	input_sync(rmi4_data->input_dev);

	return;
}

 /**
 * synaptics_rmi4_report_touch()
 *
 * Called by synaptics_rmi4_sensor_report().
 *
 * This function calls the appropriate finger data reporting function
 * based on the function handler it receives and returns the number of
 * fingers detected.
 */
static void synaptics_rmi4_report_touch(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		unsigned char *touch_count)
{
	unsigned char touch_count_2d;

	dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: Function %02x reporting\n",
			__func__, fhandler->fn_number);

	switch (fhandler->fn_number) {
	case SYNAPTICS_RMI4_F11:
		touch_count_2d = synaptics_rmi4_f11_abs_report(rmi4_data,
				fhandler);

		*touch_count += touch_count_2d;

		if (touch_count_2d)
			rmi4_data->fingers_on_2d = true;
		else
			rmi4_data->fingers_on_2d = false;
		break;

	case SYNAPTICS_RMI4_F1A:
		synaptics_rmi4_f1a_report(rmi4_data, fhandler);
		break;

	default:
		break;
	}

	return;
}

 /**
 * synaptics_rmi4_sensor_report()
 *
 * Called by synaptics_rmi4_irq().
 *
 * This function determines the interrupt source(s) from the sensor
 * and calls synaptics_rmi4_report_touch() with the appropriate
 * function handler for each function with valid data inputs.
 */
static int synaptics_rmi4_sensor_report(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char touch_count = 0;
	unsigned char intr[MAX_INTR_REGISTERS];
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_exp_fn *exp_fhandler;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	/*
	 * Get interrupt status information from F01 Data1 register to
	 * determine the source(s) that are flagging the interrupt.
	 */
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr + 1,
			intr,
			rmi4_data->num_of_intr_regs);
	if (retval < 0)
		return retval;

	/*
	 * Traverse the function handler list and service the source(s)
	 * of the interrupt accordingly.
	 */
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->num_of_data_sources) {
				if (fhandler->intr_mask &
						intr[fhandler->intr_reg_num]) {
					synaptics_rmi4_report_touch(rmi4_data,
							fhandler, &touch_count);
				}
			}
		}
	}

	mutex_lock(&exp_data.mutex);
	if (!list_empty(&exp_data.list)) {
		list_for_each_entry(exp_fhandler, &exp_data.list, link) {
			if (exp_fhandler->inserted &&
					(exp_fhandler->func_attn != NULL))
				exp_fhandler->func_attn(rmi4_data, intr[0]);
		}
	}
	mutex_unlock(&exp_data.mutex);

	return touch_count;
}
 
 static void synaptics_work_func(struct work_struct *work)
 {
	 struct synaptics_rmi4_data *ts = container_of(work, struct synaptics_rmi4_data, work);
	 
	 synaptics_rmi4_sensor_report(ts);
	 enable_irq(ts->irq);
	 
	 return ;
	 
}
 static struct workqueue_struct *synaptics_wq;

 /**
 * synaptics_rmi4_irq()
 *
 * Called by the kernel when an interrupt occurs (when the sensor
 * asserts the attention irq).
 *
 * This function is the ISR thread and handles the acquisition
 * and the reporting of finger data when the presence of fingers
 * is detected.
 */
static irqreturn_t synaptics_rmi4_irq(int irq, void *data)
{
	struct synaptics_rmi4_data *rmi4_data = data;
	
	//synaptics_rmi4_sensor_report(rmi4_data);
	disable_irq_nosync(irq);
	queue_work(synaptics_wq, &rmi4_data->work);

	return IRQ_HANDLED;
}

 /**
 * synaptics_rmi4_irq_enable()
 *
 * Called by synaptics_rmi4_irq_acquire() and power management 
 * functions in this driver
 *
 * This function handles the enabling and disabling of the attention
 * irq including the setting up of the ISR thread.
 */
static int synaptics_rmi4_irq_enable(struct synaptics_rmi4_data *rmi4_data,
		bool enable)
{
	int retval = 0;
	unsigned char intr_status;
	if (enable) {
		/* Clear interrupts first */
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				rmi4_data->f01_data_base_addr + 1,
				&intr_status,
				rmi4_data->num_of_intr_regs);
		if (retval < 0)
			return retval;

		enable_irq(rmi4_data->irq);
	} else
		disable_irq(rmi4_data->irq);

	return retval;
}

 /**
 * synaptics_rmi4_irq_acquire()
 *
 * Called by synaptics_rmi4_probe()  in this driver and also exported 
 * to other expansion Function modules such as rmi_dev.
 *
 * This function handles the enabling and disabling of the attention
 * irq including the setting up of the ISR thread.
 */
static int synaptics_rmi4_irq_acquire(struct synaptics_rmi4_data *rmi4_data,
		bool enable)
{
	int retval = 0;
	const struct synaptics_dsx_platform_data *pdata = rmi4_data->board;
        unsigned char intr_status;

	if (enable) {
		/* Clear interrupts first */
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				rmi4_data->f01_data_base_addr + 1,
				&intr_status,
				rmi4_data->num_of_intr_regs);
		if (retval < 0){
			printk("synaptics_rmi4_i2c_read fail addr:0x%x,length:%d",
				rmi4_data->f01_data_base_addr + 1,rmi4_data->num_of_intr_regs);
			return retval;
		}
		retval = request_threaded_irq(rmi4_data->irq, NULL,
			synaptics_rmi4_irq, pdata->irq_flags,
			DRIVER_NAME, rmi4_data);
		if (retval < 0) {
			dev_err(&rmi4_data->i2c_client->dev,
			"%s: Failed to request_threaded_irq\n",
			__func__);
			return retval;
		}
	} else
		free_irq(rmi4_data->irq, rmi4_data);
	return retval;
}
 
 /**
 * synaptics_rmi4_f11_init()
 *
 * Called by synaptics_rmi4_query_device().
 *
 * This funtion parses information from the Function 11 registers
 * and determines the number of fingers supported, x and y data ranges,
 * offset to the associated interrupt status register, interrupt bit
 * mask, and gathers finger data acquisition capabilities from the query
 * registers.
 */
static int synaptics_rmi4_f11_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;
	unsigned char ii;
	unsigned char intr_offset;
	unsigned char abs_data_size;
	unsigned char abs_data_blk_size;
	unsigned char query[F11_STD_QUERY_LEN];
	unsigned char control[F11_STD_CTRL_LEN];
	static int b_boot = 0;

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base,
			query,
			sizeof(query));
	if (retval < 0)
		return retval;

	/* Maximum number of fingers supported */
	if ((query[1] & MASK_3BIT) <= 4)
		fhandler->num_of_data_points = (query[1] & MASK_3BIT) + 1;
	else if ((query[1] & MASK_3BIT) == 5)
		fhandler->num_of_data_points = 10;

	rmi4_data->num_of_fingers = fhandler->num_of_data_points;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base,
			control,
			sizeof(control));
	if (retval < 0)
		return retval;

	/* Maximum x and y */
	rmi4_data->sensor_max_x = ((control[6] & MASK_8BIT) << 0) |
			((control[7] & MASK_4BIT) << 8);
	rmi4_data->sensor_max_y = ((control[8] & MASK_8BIT) << 0) |
			((control[9] & MASK_4BIT) << 8);
	dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: Function %02x max x = %d max y = %d\n",
			__func__, fhandler->fn_number,
			rmi4_data->sensor_max_x,
			rmi4_data->sensor_max_y);

	rmi4_data->sensor_max_chip_y = rmi4_data->sensor_max_y;
	if(b_boot == 0) {
		if(rmi4_data->sensor_max_x == 0 || rmi4_data->sensor_max_y == 0) {
			rmi4_data->sensor_max_x = 0xff;
			rmi4_data->sensor_max_y = 0xff;
		}
		rmi4_data->sensor_origin_x = rmi4_data->sensor_max_x;
		rmi4_data->sensor_origin_y = rmi4_data->sensor_max_y;
		b_boot = 1;
	}

	fhandler->intr_reg_num = (intr_count + 7) / 8;
	if (fhandler->intr_reg_num != 0)
		fhandler->intr_reg_num -= 1;

	/* Set an enable bit for each data source */
	intr_offset = intr_count % 8;
	fhandler->intr_mask = 0;
	for (ii = intr_offset;
			ii < ((fd->intr_src_count & MASK_3BIT) +
			intr_offset);
			ii++)
		fhandler->intr_mask |= 1 << ii;

	abs_data_size = query[5] & MASK_2BIT;
	abs_data_blk_size = 3 + (2 * (abs_data_size == 0 ? 1 : 0));
	fhandler->size_of_data_register_block = abs_data_blk_size;

	return retval;
}

static int synaptics_rmi4_f1a_alloc_mem(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	struct synaptics_rmi4_f1a_handle *f1a;

	f1a = kzalloc(sizeof(*f1a), GFP_KERNEL);
	if (!f1a) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for function handle\n",
				__func__);
		return -ENOMEM;
	}

	fhandler->data = (void *)f1a;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base,
			f1a->button_query.data,
			sizeof(f1a->button_query.data));
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to read query registers\n",
				__func__);
		return retval;
	}

	f1a->button_count = f1a->button_query.max_button_count + 1;
	f1a->button_bitmask_size = (f1a->button_count + 7) / 8;

	f1a->button_data_buffer = kcalloc(f1a->button_bitmask_size,
			sizeof(*(f1a->button_data_buffer)), GFP_KERNEL);
	if (!f1a->button_data_buffer) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for data buffer\n",
				__func__);
		return -ENOMEM;
	}

	f1a->button_map = kcalloc(f1a->button_count,
			sizeof(*(f1a->button_map)), GFP_KERNEL);
	if (!f1a->button_map) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for button map\n",
				__func__);
		return -ENOMEM;
	}

	return 0;
}

static int synaptics_rmi4_cap_button_map(
				struct synaptics_rmi4_data *rmi4_data,
				struct synaptics_rmi4_fn *fhandler)
{
	unsigned char ii;
	struct synaptics_rmi4_f1a_handle *f1a = fhandler->data;
	const struct synaptics_dsx_platform_data *pdata = rmi4_data->board;

	if (!pdata->cap_button_map) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: cap_button_map is" \
				"NULL in board file\n",
				__func__);
		return -ENODEV;
	} else if (!pdata->cap_button_map->map) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Button map is missing in board file\n",
				__func__);
		return -ENODEV;
	} else {
		if (pdata->cap_button_map->nbuttons !=
			f1a->button_count) {
			f1a->valid_button_count = min(f1a->button_count,
				pdata->cap_button_map->nbuttons);
		} else {
			f1a->valid_button_count = f1a->button_count;
		}

		for (ii = 0; ii < f1a->valid_button_count; ii++)
			f1a->button_map[ii] =
					pdata->cap_button_map->map[ii];
	}

	return 0;
}

static void synaptics_rmi4_f1a_kfree(struct synaptics_rmi4_fn *fhandler)
{
	struct synaptics_rmi4_f1a_handle *f1a = fhandler->data;

	if (f1a) {
		if(f1a->button_data_buffer)
		kfree(f1a->button_data_buffer);
		if(f1a->button_map)
		kfree(f1a->button_map);
		kfree(f1a);
		fhandler->data = NULL;
	}

	return;
}

static int synaptics_rmi4_f1a_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;
	unsigned char ii;
	unsigned short intr_offset;

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;

	fhandler->intr_reg_num = (intr_count + 7) / 8;
	if (fhandler->intr_reg_num != 0)
		fhandler->intr_reg_num -= 1;

	/* Set an enable bit for each data source */
	intr_offset = intr_count % 8;
	fhandler->intr_mask = 0;
	for (ii = intr_offset;
			ii < ((fd->intr_src_count & MASK_3BIT) +
			intr_offset);
			ii++)
		fhandler->intr_mask |= 1 << ii;

	retval = synaptics_rmi4_f1a_alloc_mem(rmi4_data, fhandler);
	if (retval < 0)
		goto error_exit;

	retval = synaptics_rmi4_cap_button_map(rmi4_data, fhandler);
	if (retval < 0)
		goto error_exit;

	rmi4_data->button_0d_enabled = 1;

	return 0;

error_exit:
	synaptics_rmi4_f1a_kfree(fhandler);

	return retval;
}

static int synaptics_rmi4_alloc_fh(struct synaptics_rmi4_fn **fhandler,
		struct synaptics_rmi4_fn_desc *rmi_fd, int page_number)
{
	*fhandler = kzalloc(sizeof(**fhandler), GFP_KERNEL);
	if (!(*fhandler))
		return -ENOMEM;

	(*fhandler)->full_addr.data_base =
			(rmi_fd->data_base_addr |
			(page_number << 8));
	(*fhandler)->full_addr.ctrl_base =
			(rmi_fd->ctrl_base_addr |
			(page_number << 8));
	(*fhandler)->full_addr.cmd_base =
			(rmi_fd->cmd_base_addr |
			(page_number << 8));
	(*fhandler)->full_addr.query_base =
			(rmi_fd->query_base_addr |
			(page_number << 8));
	(*fhandler)->fn_number = rmi_fd->fn_number;

	return 0;
}


 /**
 * synaptics_rmi4_query_device_info()
 *
 * Called by synaptics_rmi4_query_device().
 *
 */
static int synaptics_rmi4_query_device_info(
					struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char f01_query[F01_STD_QUERY_LEN];
	struct synaptics_rmi4_device_info *rmi = &(rmi4_data->rmi4_mod_info);

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_query_base_addr,
			f01_query,
			sizeof(f01_query));
	if (retval < 0)
		return retval;

	/* RMI Version 4.0 currently supported */
	rmi->version_major = 4;
	rmi->version_minor = 0;

	rmi->manufacturer_id = f01_query[0];
	rmi->product_props = f01_query[1];
	rmi->product_info[0] = f01_query[2] & MASK_7BIT;
	rmi->product_info[1] = f01_query[3] & MASK_7BIT;
	rmi->date_code[0] = f01_query[4] & MASK_5BIT;
	rmi->date_code[1] = f01_query[5] & MASK_4BIT;
	rmi->date_code[2] = f01_query[6] & MASK_5BIT;
	rmi->tester_id = ((f01_query[7] & MASK_7BIT) << 8) |
			(f01_query[8] & MASK_7BIT);
	rmi->serial_number = ((f01_query[9] & MASK_7BIT) << 8) |
			(f01_query[10] & MASK_7BIT);
	memcpy(rmi->product_id_string, &f01_query[11], 10);

	if (rmi->manufacturer_id != 1) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Non-Synaptics device found, manufacturer ID = %d\n",
				__func__, rmi->manufacturer_id);
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_query_base_addr + F01_BUID_ID_OFFSET,
			rmi->build_id,
			sizeof(rmi->build_id));
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to read firmware build id (code %d)\n",
				__func__, retval);
		return retval;
	}
	return 0;
}

 /**
 * synaptics_rmi4_crc_in_progress()
 *
 * Check if crc in progress ever occured
 *
 */
static bool synaptics_rmi4_crc_in_progress(struct synaptics_rmi4_data *rmi4_data, 
			struct synaptics_rmi4_f01_device_status *status)
{
	int retval;
	int times = 0;
	bool rescan = false;

	while (1) {
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				rmi4_data->f01_data_base_addr,
				status->data,
				sizeof(status->data));
		if (retval < 0) {
			dev_err(&rmi4_data->i2c_client->dev,
				"%s: read status register failed\n",
				__func__);
			return false;
		}
		if (status->status_code ==
			STATUS_CRC_IN_PROGRESS) {
			dev_info(&rmi4_data->i2c_client->dev,
				"%s: CRC is in progress...\n",
				__func__);
			rescan = true;
			msleep(20);
		} else {
			break;
		}
		if (times++ > 500)
			return false;
	}
	return rescan;
}

 /**
 * synaptics_rmi4_query_device()
 *
 * Called by synaptics_rmi4_probe().
 *
 * This funtion scans the page description table, records the offsets
 * to the register types of Function $01, sets up the function handlers
 * for Function $11 and Function $12, determines the number of interrupt
 * sources from the sensor, adds valid Functions with data inputs to the
 * Function linked list, parses information from the query registers of
 * Function $01, and enables the interrupt sources from the valid Functions
 * with data inputs.
 */
static int synaptics_rmi4_query_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char ii;
	unsigned char page_number;
	unsigned char intr_count;
	unsigned char data_sources;
	unsigned short pdt_entry_addr;
	unsigned short intr_addr;
	struct synaptics_rmi4_f01_device_status status;
	struct synaptics_rmi4_fn_desc rmi_fd;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

rescan:
	INIT_LIST_HEAD(&rmi->support_fn_list);
	intr_count = 0;
	data_sources = 0;


	/* Scan the page description tables of the pages to service */
	for (page_number = 0; page_number < PAGES_TO_SERVICE; page_number++) {
		for (pdt_entry_addr = PDT_START; pdt_entry_addr > PDT_END;
				pdt_entry_addr -= PDT_ENTRY_SIZE) {
			pdt_entry_addr |= (page_number << 8);

			retval = synaptics_rmi4_i2c_read(rmi4_data,
					pdt_entry_addr,
					(unsigned char *)&rmi_fd,
					sizeof(rmi_fd));
			if (retval < 0){
				dev_err(&rmi4_data->i2c_client->dev,
						"%s:synaptics_rmi4_i2c_read Failed\n",
						__func__);

				return retval;
				}

			fhandler = NULL;

			if (rmi_fd.fn_number == 0) {
				dev_dbg(&rmi4_data->i2c_client->dev,
						"%s: Reached end of PDT\n",
						__func__);
				break;
			}

			dev_dbg(&rmi4_data->i2c_client->dev,
					"%s: F%02x found (page %d)\n",
					__func__, rmi_fd.fn_number,
					page_number);

			switch (rmi_fd.fn_number) {
			case SYNAPTICS_RMI4_F01:
				rmi4_data->f01_query_base_addr =
						rmi_fd.query_base_addr;
				rmi4_data->f01_ctrl_base_addr =
						rmi_fd.ctrl_base_addr;
				rmi4_data->f01_data_base_addr =
						rmi_fd.data_base_addr;
				rmi4_data->f01_cmd_base_addr =
						rmi_fd.cmd_base_addr;

				if (synaptics_rmi4_crc_in_progress(rmi4_data, &status))
					goto rescan;

				retval =
				synaptics_rmi4_query_device_info(rmi4_data);
				if (retval < 0)
					return retval;

				if (status.flash_prog == 1) {
					pr_notice("%s: In flash prog mode, status = 0x%02x\n",
							__func__,
							status.status_code);
					goto flash_prog_mode;
				}
				break;

			case SYNAPTICS_RMI4_F11:
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					dev_err(&rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}

				retval = synaptics_rmi4_f11_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0)
					return retval;
				break;

			case SYNAPTICS_RMI4_F1A:
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					dev_err(&rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}

				retval = synaptics_rmi4_f1a_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0)
					return retval;
				break;

			case SYNAPTICS_RMI4_F54:
			case SYNAPTICS_RMI4_F55:
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					dev_err(&rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}
				break;
			case SYNAPTICS_RMI4_F34:
				//rmi4_data->f34.flag		 = rmi_fd.function_number;
				rmi4_data->f34_query_base_addr= rmi_fd.query_base_addr;
				rmi4_data->f34_ctrl_base_addr= rmi_fd.ctrl_base_addr;
				rmi4_data->f34_data_base_addr= rmi_fd.data_base_addr;
				//rmi4_data->f34.intr_mask	 = ((1<<(rmi_fd.intr_src_count &0x7))-1)<<(intr_count%8);
				break;

			default:
				break;
			}

			/* Accumulate the interrupt count */
			intr_count += (rmi_fd.intr_src_count & MASK_3BIT);

			if (fhandler && rmi_fd.intr_src_count) {
				list_add_tail(&fhandler->link,
						&rmi->support_fn_list);
			}
		}
	}
	rmi4_data->num_of_intr_regs = (intr_count + 7) / 8;
	dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: Number of interrupt registers = %d\n",
			__func__, rmi4_data->num_of_intr_regs);

return 0 ;
flash_prog_mode:

	rmi4_data->num_of_intr_regs = (intr_count + 7) / 8;
	dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: Number of interrupt registers = %d\n",
			__func__, rmi4_data->num_of_intr_regs);

	memset(rmi4_data->intr_mask, 0x00, sizeof(rmi4_data->intr_mask));

	/*
	 * Map out the interrupt bit masks for the interrupt sources
	 * from the registered function handlers.
	 */
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link)
			data_sources += fhandler->num_of_data_sources;
	}
	if (data_sources) {
		if (!list_empty(&rmi->support_fn_list)) {
			list_for_each_entry(fhandler,
						&rmi->support_fn_list, link) {
				if (fhandler->num_of_data_sources) {
					rmi4_data->intr_mask[fhandler->intr_reg_num] |=
							fhandler->intr_mask;
				}
			}
		}
	}

	/* Enable the interrupt sources */
	for (ii = 0; ii < rmi4_data->num_of_intr_regs; ii++) {
		if (rmi4_data->intr_mask[ii] != 0x00) {
			dev_dbg(&rmi4_data->i2c_client->dev,
					"%s: Interrupt enable mask %d = 0x%02x\n",
					__func__, ii, rmi4_data->intr_mask[ii]);
			intr_addr = rmi4_data->f01_ctrl_base_addr + 1 + ii;
			retval = synaptics_rmi4_i2c_write(rmi4_data,
					intr_addr,
					&(rmi4_data->intr_mask[ii]),
					sizeof(rmi4_data->intr_mask[ii]));
			if (retval < 0)
				return retval;
		}
	}

	return 0;
}


static int synaptics_rmi4_reset_command(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	int page_number;
	unsigned char command = 0x01;
	unsigned short pdt_entry_addr;
	struct synaptics_rmi4_fn_desc rmi_fd;
	struct synaptics_rmi4_f01_device_status status;
	bool done = false;

rescan:
	/* Scan the page description tables of the pages to service */
	for (page_number = 0; page_number < PAGES_TO_SERVICE; page_number++) {
		for (pdt_entry_addr = PDT_START; pdt_entry_addr > PDT_END;
				pdt_entry_addr -= PDT_ENTRY_SIZE) {
			pdt_entry_addr |= (page_number << 8);

			retval = synaptics_rmi4_i2c_read(rmi4_data,
					pdt_entry_addr,
					(unsigned char *)&rmi_fd,
					sizeof(rmi_fd));
			if (retval < 0)
				return retval;

			if (rmi_fd.fn_number == 0)
				break;

			switch (rmi_fd.fn_number) {
			case SYNAPTICS_RMI4_F01:
				rmi4_data->f01_cmd_base_addr =
						rmi_fd.cmd_base_addr;
				rmi4_data->f01_data_base_addr =
						rmi_fd.data_base_addr;
				if (synaptics_rmi4_crc_in_progress(rmi4_data, &status))
					goto rescan;
				done = true;
				break;
			}
		}
		if (done) {
			dev_info(&rmi4_data->i2c_client->dev,
				"%s: Find F01 in page description table 0x%x\n",
				__func__, rmi4_data->f01_cmd_base_addr);
			break;
		}
	}


	if (!done) {
		dev_err(&rmi4_data->i2c_client->dev,
			"%s: Cannot find F01 in page description table\n",
			__func__);
		return -EINVAL;;
	}

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_cmd_base_addr,
			&command,
			sizeof(command));
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to issue reset command, error = %d\n",
				__func__, retval);
		return retval;
	}

	msleep(rmi4_data->reset_delay_ms);
	return retval;
}

static int synaptics_rmi4_reset_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_device_info *rmi;
        struct synaptics_rmi4_exp_fn *exp_fhandler;

	rmi = &(rmi4_data->rmi4_mod_info);

	retval = synaptics_rmi4_reset_command(rmi4_data);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to send command reset\n",
				__func__);
		return retval;
	}

	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F1A)
				synaptics_rmi4_f1a_kfree(fhandler);
			else
				kfree(fhandler->data);
			kfree(fhandler);
		}
	}

	retval = synaptics_rmi4_query_device(rmi4_data);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to query device\n",
				__func__);
		return retval;
	}

	mutex_lock(&exp_data.mutex);
	if (!list_empty(&exp_data.list)) {
		list_for_each_entry(exp_fhandler, &exp_data.list, link) {
			if (exp_fhandler->fn_type == RMI_F54) {
				exp_fhandler->func_remove(rmi4_data);
				exp_fhandler->func_init(rmi4_data);
			}
		}
	}
	mutex_unlock(&exp_data.mutex);
	return 0;
}

/**
* synaptics_rmi4_detection_work()
*
* Called by the kernel at the scheduled time.
*
* This function is a self-rearming work thread that checks for the
* insertion and removal of other expansion Function modules such as
* rmi_dev and calls their initialization and removal callback functions
* accordingly.
*/
static void synaptics_rmi4_detection_work(struct work_struct *work)
{
	struct synaptics_rmi4_exp_fn *exp_fhandler, *next_list_entry;

	mutex_lock(&exp_data.mutex);
	if (!list_empty(&exp_data.list)) {
		list_for_each_entry_safe(exp_fhandler,
				next_list_entry,
				&exp_data.list,
				link) {
			if ((exp_fhandler->func_init != NULL) &&
					(exp_fhandler->inserted == false)) {
				exp_fhandler->func_init(exp_data.rmi4_data);
				exp_fhandler->inserted = true;
			} else if ((exp_fhandler->func_init == NULL) &&
					(exp_fhandler->inserted == true)) {
				exp_fhandler->func_remove(exp_data.rmi4_data);
				list_del(&exp_fhandler->link);
				kfree(exp_fhandler);
			}
		}
	}
	mutex_unlock(&exp_data.mutex);

	return;
}

/**
* synaptics_rmi4_new_function()
*
* Called by other expansion Function modules in their module init and
* module exit functions.
*
* This function is used by other expansion Function modules such as
* rmi_dev to register themselves with the driver by providing their
* initialization and removal callback function pointers so that they
* can be inserted or removed dynamically at module init and exit times,
* respectively.
*/
void synaptics_rmi4_new_function(enum exp_fn fn_type, bool insert,
		int (*func_init)(struct synaptics_rmi4_data *rmi4_data),
		void (*func_remove)(struct synaptics_rmi4_data *rmi4_data),
		void (*func_attn)(struct synaptics_rmi4_data *rmi4_data,
		unsigned char intr_mask))
{
	struct synaptics_rmi4_exp_fn *exp_fhandler;

	if (!exp_data.initialized) {
		mutex_init(&exp_data.mutex);
		INIT_LIST_HEAD(&exp_data.list);
		exp_data.initialized = 1;
	}

	mutex_lock(&exp_data.mutex);
	if (insert) {
		exp_fhandler = kzalloc(sizeof(*exp_fhandler), GFP_KERNEL);
		if (!exp_fhandler) {
			pr_err("%s: Failed to alloc mem for expansion function\n",
					__func__);
			goto exit;
		}
		exp_fhandler->fn_type = fn_type;
		exp_fhandler->func_init = func_init;
		exp_fhandler->func_attn = func_attn;
		exp_fhandler->func_remove = func_remove;
		exp_fhandler->inserted = false;
		list_add_tail(&exp_fhandler->link, &exp_data.list);
	} else {
		if (!list_empty(&exp_data.list)) {
		        list_for_each_entry(exp_fhandler, &exp_data.list, link) {
			        if (exp_fhandler->func_init == func_init) {
				        exp_fhandler->inserted = false;
				        exp_fhandler->func_init = NULL;
				        exp_fhandler->func_attn = NULL;
				        goto exit;
			        }
                	}
		}
	}
exit:
	mutex_unlock(&exp_data.mutex);

	if (exp_data.queue_work) {
		queue_delayed_work(exp_data.workqueue,
				&exp_data.work,
				msecs_to_jiffies(EXP_FN_WORK_DELAY_MS));
	}
	return;
}
EXPORT_SYMBOL(synaptics_rmi4_new_function);

/*ergate*/
#define SYN_FW_NAME "PR1101200-s2202_zte_32313032.img"
#define SYNAPTICS_MAX_Y_POSITION	73

//
//	touchscreen gpio functions
//
//#define VREG_VDD		"8941_l18"		// 3V
//#define VREG_VBUS		"8941_lvs1"		// 1.8V
//#define GPIO_TS_IRQ		61
//#define GPIO_TS_RST		60
#define VREG_VDD	"vdd" //"8226_l19"
#define VREG_VBUS	"vcc_i2c" //"8226_lvs1"
#define GPIO_TS_IRQ		17
#define GPIO_TS_RST		16

struct i2c_client *ts_client;
struct synaptics_rmi4_data *syn_ts;
static struct regulator *vdd, *vbus;

#if 0

static void touchscreen_irq( int hl, bool io_flag )
{
	//io_flag: true- default input, false - output

	if ( io_flag == true )
		gpio_direction_input(GPIO_TS_IRQ);
	else
		gpio_direction_output(GPIO_TS_IRQ, hl);

	return;
}

static int get_screeninfo(uint *xres, uint *yres)
{
	struct fb_info *info;

	info = registered_fb[0];
	if (!info) {
		pr_err("%s: Can not access lcd info \n",__func__);
		*xres = 720;
		*yres = 1280;
		return -ENODEV;
	}

	*xres = info->var.xres;
	*yres = info->var.yres;
	printk("lcd ( %d, %d ) \n", *xres, *yres );

	return 1;
}
#endif
static void touchscreen_reset( int hl ,unsigned gpio)
{
		pr_info("%s, gpio %d touchscreen_reset is %d.\n", __func__,gpio,hl);	
	gpio_direction_output(gpio, hl);
	return;
}

static int detect_device(struct synaptics_rmi4_data *rmi4_data)
{
	int i;
	char buf[1];
	int ret = 0;

	if ( rmi4_data == NULL )
		return ret;

	for (i=0; i<3; i++ )
	{
		// 0xFF: synaptics rmi page select register
		ret = synaptics_rmi4_i2c_read(rmi4_data,0xFF,buf,1);
		if ( ret >= 0 ){
			ret = 1;
			break;
		}
		ret=0;
		msleep(10);
	}

	return ret;
}

static int touchscreen_gpio_init(struct synaptics_rmi4_data *rmi4_data,int flag,char *vreg_vdd,char *vreg_vbus,
unsigned reset_gpio,unsigned irq_gpio)
{
	int ret = -EINVAL;


	pr_info("touchscreen_gpio_init\n");
	//init
	if ( flag == 1 )
	{
		vdd = vbus = NULL;

		vdd = regulator_get(&rmi4_data->i2c_client->dev, vreg_vdd);
		if (!vdd) {
			pr_err(" vdd get failed\n");
			return -1;
		}
		if ( regulator_set_voltage(vdd, 2850000,2850000) ){   //3000000,3000000
			pr_err("vdd set failed\n");
			return -1;
		}

		vbus =regulator_get(&rmi4_data->i2c_client->dev, vreg_vbus);
		if (!vbus) {
			pr_err("vbus get failed\n");
			return -1;
		}

		if ( regulator_set_voltage(vbus, 1800000,1800000)) {
			pr_err(" vbus set failed\n");
			//return -1;
		}

		ret = gpio_request(reset_gpio, "touch voltage");
		if (ret){
			pr_err(" gpio %d request is error!\n", reset_gpio);
			return -1;
		}

		ret = gpio_request(irq_gpio, "touch voltage");
		if (ret){
			pr_err("gpio %d request is error!\n", irq_gpio);
			return -1;
		}

	}


	//deinit
	if ( flag == 0)
	{
		regulator_put(vdd);
		regulator_put(vbus);
		gpio_free(GPIO_TS_IRQ);
		gpio_free(GPIO_TS_RST);
	}

	return 0;

}

static void touchscreen_power( int on )
{
	int rc = -EINVAL;
 
	if ( !vdd || !vbus )
		return;

	pr_info("%s, touchscreen_power  is %d.\n", __func__,on);	

	pr_info("vdd is  %s,vbus is %s\n",vdd->supply_name,vbus->supply_name);	
	if (on){
		rc = regulator_enable(vdd);
		if (rc) {
			pr_err("vdd enable failed\n");
			return;
		}
		rc = regulator_enable(vbus);
		if (rc) {
			pr_err("vbus enable failed\n");
			return;
		}
	}
	else 
	{
		rc = regulator_disable(vdd);
		if (rc) {
			pr_err("vdd disable failed\n");
			return;
		}
		rc = regulator_disable(vbus);
		if (rc) {
			pr_err("vbus disable failed\n");
			return;
		}
	}

	return;
}

#ifdef CONFIG_PROC_FS
#define TOUCH_PROC_FILE "driver/tpd_touch"
static struct proc_dir_entry *synapatics_touch_proc_file;
static int g_proc_addr, g_proc_val;

static ssize_t tpd_proc_read_val(struct file *file,
	char __user *buffer, size_t count, loff_t *offset)
{
	uint8_t buf[16];
	uint8_t irq_status[3];
	uint8_t buffer_synap[800];
	int finger = 0, ret = 0;
	ssize_t len = 0;
	
	finger = 0;//initializing the status

	len += sprintf(buffer_synap+len, "Synaptics Touchscreen v20140305.\n");
	//len += sprintf(buffer_synap+len, "ts_wq_status: %d, intr 0x%x value:0x%x.\n", ts_wq_status, g_ts->info->irq_gpio, gpio_get_value(g_ts->info->irq_gpio));
	//len += sprintf(buffer_synap+len, "debug:%d.\n", g_ts->b_debug_mode);
	len += sprintf(buffer_synap+len, "addr: 0x%x, value:0x%x.\n", g_proc_addr, g_proc_val);
	len += sprintf(buffer_synap+len, "Origin(x,y):(%d, %d), Current(x,y):(%d, %d).\n", syn_ts->sensor_origin_x, syn_ts->sensor_origin_y, syn_ts->sensor_max_x, syn_ts->sensor_max_y);
	
	ret = synaptics_rmi4_i2c_read(syn_ts, syn_ts->f01_ctrl_base_addr, irq_status, 3);
	len += sprintf(buffer_synap + len, "Device Control\"0x04:nosleep, 0x0:normal, 0x1:sleep. \"  Control:0x%x , IRQ: 0x%x Doze:0x%x.\n",
		irq_status[0], irq_status[1], irq_status[2]);
	
	ret = synaptics_rmi4_i2c_read(syn_ts, 0x0014, buf, 16);
	if (ret < 0) {
		len += sprintf(buffer_synap+len, "i2c_transfer failed\n");
	} else {
		len += sprintf(buffer_synap+len, "Debug: :"
			"%x %x %x %x %x %x %x %x %x"
				" %x %x %x %x %x %x, ret %d\n",
					   buf[0], buf[1], buf[2], buf[3],
					   buf[4], buf[5], buf[6], buf[7],
					buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], ret);/**/
	}
	
	return simple_read_from_buffer(buffer, count, offset, buffer_synap, len);
}

static ssize_t tpd_proc_write_val(struct file *filp,
					 const char *buff, size_t len,
					 loff_t * off)
{
	char messages[256];
	uint8_t buf[16];
	int reg, val;
	uint8_t reg_data; // zhangzhao 2014-5-19 for tsc hardware test
	int i = 0;
	
	if (copy_from_user(messages, buff, len))
		return -EFAULT;
	
	//TPD_DMESG( "%s\n", messages);
	if ('a' == messages[0]) {
		sscanf(&messages[1], "%x", &reg);
		synaptics_rmi4_i2c_read(syn_ts, reg, buf, 16);
		g_proc_addr = reg;
		g_proc_val = buf[0];
		
		TPD_DMESG("Read from addr:0x%x val=", reg);
		for(i = 0; i < 16; i++) {
			TPD_DMESG("0x%x ", buf[i]);
		}
		TPD_DMESG("\n");
		return len;
	} else if ('w' == messages[0]) {
		sscanf(&messages[1], "%x %x\n", &reg, &val );
		g_proc_addr = reg;
		buf[0] = val & 0xff;
		synaptics_rmi4_i2c_write(syn_ts, reg, buf, 1);
		TPD_DMESG("sscanf reg:0x%x val:0x%x\n", reg, val );
		return len;
	} else if ('d' == messages[0]) {
		//ts_debug |= DEBUG_REPORT_POINT_EN;
		return len;
	} else if('n' == messages[0]) {
		//ts_debug &= ~DEBUG_REPORT_POINT_EN;
		return len;
	} else if('r' == messages[0] && 'r' == messages[1]) {
		buf[0] = 0;
		synaptics_rmi4_i2c_write(syn_ts, syn_ts->f01_ctrl_base_addr + 1, buf, 1);
		buf[0] = 1;
		synaptics_rmi4_i2c_write(syn_ts, syn_ts->f01_ctrl_base_addr, buf, 1);
		msleep(10);
		buf[0] = 0;
		synaptics_rmi4_i2c_write(syn_ts, syn_ts->f01_ctrl_base_addr, buf, 1);
		buf[0] = 7;
		synaptics_rmi4_i2c_write(syn_ts, syn_ts->f01_ctrl_base_addr+ 1, buf, 1);
	} else if('f' == messages[0] && '5' == messages[1]) {
		sscanf(&messages[2], "%d\n", &val);
		TPD_DMESG("Fn54 sscanf val:%d\n", val);
		//g_proc_val = check_tp_module(val, (unsigned char *)messages, 256);
	} else if('r' == messages[0] && 'p' == messages[1]) {
		touchscreen_power(0);
		msleep(20);
		touchscreen_power(1);
		return len;
	} else if('r' == messages[0] && 'g' == messages[1]) {
		touchscreen_reset(0, syn_ts->board->reset_gpio);
		msleep(10);
		touchscreen_reset(1, syn_ts->board->reset_gpio);
		msleep(80);		
		
		return len;
	} else if('s' == messages[0]) {
		//TPD_DMESG("%s:ts_wq_status: %d\n", __func__, ts_wq_status);
	}
	reg_data = 0xFA;
	synaptics_rmi4_i2c_write(syn_ts,0x009B,&reg_data,1);
	reg_data = 0x00;
	synaptics_rmi4_i2c_write(syn_ts,0x0400,&reg_data,1);
	synaptics_rmi4_i2c_read(syn_ts,0x009B,&reg_data,1);
	pr_info("synaptics_read 0x9b is %x\n",reg_data);
	synaptics_rmi4_i2c_read(syn_ts,0x0400,&reg_data,1);
	pr_info("synaptics_read 0x0400 is %x\n",reg_data);

	return len;
}

static struct file_operations synapatics_touch_proc_ops = {
	.read = tpd_proc_read_val,
	.write = tpd_proc_write_val,
};

static void create_synapatics_touch_proc_file(void)
{
	synapatics_touch_proc_file =
		create_proc_entry(TOUCH_PROC_FILE, 0644, NULL);
	if (synapatics_touch_proc_file) {
		synapatics_touch_proc_file->proc_fops = &synapatics_touch_proc_ops;
	} else
		TPD_DMESG(KERN_INFO "proc file create failed!\n");
}

static void remove_synapatics_touch_proc_file(void)
{
	extern struct proc_dir_entry proc_root;
	remove_proc_entry(TOUCH_PROC_FILE, &proc_root);
}

#endif
#ifdef TPD_SUPPORT_SYS_FACE
struct vendor_l {
	int index;
	int vendor_id;
} synap_vendor_l[] = {
	{1, 2200},
	{1, 2200},
	{2, 2202},
	{3, 3200},
	{4, 3202},
	{5, 3203},
	{6, 7020},
	{7, 7300},
	{0xff, 0xff},
};

static int tpd_get_chip_id(int index)
{
	int i = 0;

	for(i = 0; i < sizeof(synap_vendor_l); i ++) {
		if(index == synap_vendor_l[i].index|| 0xff == synap_vendor_l[i].index) {
			return synap_vendor_l[i].vendor_id;
		}
	}
	return 0;
}

static int ascii_to_hex(unsigned int ascii_value)
{
	if(ascii_value >= 0x30 && ascii_value <= 0x39) {
		return ascii_value & 0x0f;
	} else if (ascii_value >= 0x41 && ascii_value <= 0x46) {
		return ascii_value - 0x37;
	} else if (ascii_value >= 0x61 && ascii_value <= 0x66) {
		return ascii_value - 0x57;
	}

	return 0;
}

static int tpd_init_tpinfo(struct tpd_classdev_t *cdev)
{
	unsigned int chipid_in_chip, vendorid_in_chip, fwver_in_chip, rev1, rev2;
	struct synaptics_rmi4_data *ts = (struct synaptics_rmi4_data*) cdev->private;
	uint8_t buf[4];

	// Read current firmware revision, partner etc...
	synaptics_rmi4_i2c_read(ts, ts->f34_ctrl_base_addr, buf, 4);

	chipid_in_chip = ascii_to_hex(buf[0]);
	vendorid_in_chip = ascii_to_hex(buf[1]);
	rev1 = ascii_to_hex(buf[2]);
	rev2 = ascii_to_hex(buf[3]);
	//fwver_in_chip = (rev1 << 4) | (rev2 & 0x0f);
	fwver_in_chip=(buf[2] << 8) | (buf[3] & 0xff);

	TPD_DMESG("%s Type:0x%x, Partner:0x%x, FwVersion:0x%x, ControlBase:0x%x.\n", __func__, 
		chipid_in_chip, vendorid_in_chip, fwver_in_chip, ts->f34_ctrl_base_addr);
	
	strcpy(cdev->tp_info.tp_name, "Synaptics");
	cdev->tp_info.chip_id = tpd_get_chip_id(chipid_in_chip);
	cdev->tp_info.vendor_id = vendorid_in_chip;
	cdev->tp_info.chip_ver = 0;
	cdev->tp_info.firmware_ver= fwver_in_chip;
	cdev->tp_info.i2c_type = 0;
	cdev->tp_info.i2c_addr = ts->i2c_client->addr;
	
	return 0;
}


static int tpd_check_fw_for_upgrade(struct tpd_classdev_t *cdev, unsigned char * data, unsigned int size, unsigned int force_upg)
{
	unsigned int chipid_in_chip, vendorid_in_chip, fwver_in_chip, rev1, rev2;
	unsigned int chipid_in_file, vendorid_in_file, fwver_in_file;
	uint8_t buf[4];
	struct synaptics_rmi4_data *ts = (struct synaptics_rmi4_data*) cdev->private;

	synaptics_rmi4_i2c_read(ts, ts->f34_ctrl_base_addr, buf, 4);

	chipid_in_chip = ascii_to_hex(buf[0]);
	vendorid_in_chip = ascii_to_hex(buf[1]);
	rev1 = ascii_to_hex(buf[2]);
	rev2 = ascii_to_hex(buf[3]);
	//fwver_in_chip = (rev1 << 4) | (rev2 & 0x0f);
	fwver_in_chip = (buf[2] << 8) | (buf[3] & 0xff);
		
	chipid_in_file = ascii_to_hex(data[0xb100]);
	vendorid_in_file = ascii_to_hex(data[0xb101]);
	//fwver_in_file = (ascii_to_hex(data[0xb102]) << 4) | (ascii_to_hex(data[0xb103]) & 0x0f);
	fwver_in_file=((data[0xb102]) << 8) | ((data[0xb103]) & 0xff);

	TPD_DMESG("Type:0x%x, Partner:0x%x, Fw_rev:0x%x, in chip\n", 
		chipid_in_chip, vendorid_in_chip, fwver_in_chip);
	TPD_DMESG("Type:0x%x, Partner:0x%x, Fw_rev:0x%x, in new firmware\n", 
		chipid_in_file, vendorid_in_file, fwver_in_file);
	
	strcpy(tpd_fw_cdev.sd_info.tp_name, tpd_fw_cdev.tp_info.tp_name);
	tpd_fw_cdev.sd_info.chip_id = tpd_get_chip_id(chipid_in_file);
	tpd_fw_cdev.sd_info.vendor_id = vendorid_in_file;
	tpd_fw_cdev.sd_info.chip_ver = tpd_fw_cdev.tp_info.chip_ver;
	tpd_fw_cdev.sd_info.firmware_ver = fwver_in_file;
	tpd_fw_cdev.sd_info.i2c_type = tpd_fw_cdev.tp_info.i2c_type;
	tpd_fw_cdev.sd_info.i2c_addr = tpd_fw_cdev.tp_info.i2c_addr;
	
	if(force_upg || ((chipid_in_chip == chipid_in_file) && (vendorid_in_chip == vendorid_in_file) && 
		(fwver_in_chip < fwver_in_file))) 	{
		TPD_DMESG("%s: check firmware revsion OK!\n", __func__);
		return 0;
	}

	TPD_DMESG("%s: Not need upgrade firmware\n", __func__);

	return 0xff;
}

extern int synaptics_fw_updater(unsigned char *fw_data);
static int tpd_flash_firmware(struct tpd_classdev_t *cdev, unsigned char * data, unsigned int size, int force_upg)
{
	int  i_ret = -1;
	
//#ifdef TPD_UPDATE_FIRMWARE
	TPD_DMESG("%s in, fw size is:0x%x, force:0x%x.\n", __func__, size, force_upg);
	if(cdev == NULL || data == NULL || 0 == size){
		TPD_DMESG("%s maybe read sdcard failed\n", __func__);
		return -1;
	}

	i_ret = tpd_check_fw_for_upgrade(cdev, data, cdev->tp_fw.size, force_upg);
	if( i_ret != 0)	{
		TPD_DMESG("%s line:%d RMI check firmware finished, return...\n", __func__, __LINE__);
		goto out;
	}
	//ts->b_fwloader = 1;
	i_ret = synaptics_fw_updater(data);	
	if (i_ret == 0) {
		TPD_DMESG("upgrade to new version\n");
	} else {
		TPD_DMESG("Upgrade firmware failed, try again!\n");	
		i_ret = synaptics_fw_updater(data);
		if( 0 != i_ret )	{
			TPD_DMESG("upgrade failed ret=%d.\n", i_ret);	
		} else {
			TPD_DMESG("Upgrade firware success at second try.\n");
		}
	}
	tpd_init_tpinfo(cdev);
	//ts->b_fwloader = 0;
	//tpd_rmi4_read_pdt( ts);
out:
//#endif
	return i_ret;
}

static int tpd_check(int type, unsigned char* buf, int max_buf_num)
{
	//return check_tp_module(type, buf, max_buf_num);
	return 0;
}

static int tpd_read_block(struct tpd_classdev_t *cdev, u16 addr, u8 *buf, int len)
{
	int ret = 0;
	int i = 0;
	struct synaptics_rmi4_data *ts = (struct synaptics_rmi4_data*) cdev->private;

	ret = synaptics_rmi4_i2c_read(ts, addr, buf, len);
	TPD_DMESG("Read from addr:0x%x val=", addr);
	for(i = 0; i < (len < 8? len : 8); i++)
	{
		TPD_DMESG("0x%x ", buf[i]);
	}
	TPD_DMESG("\n");
	
	return ret;
}

static int tpd_write_block(struct tpd_classdev_t *cdev, u16 addr, u8 *buf, int len)
{
	int ret = 0;
	struct synaptics_rmi4_data *ts = (struct synaptics_rmi4_data*) cdev->private;
	
	ret = synaptics_rmi4_i2c_read(ts, addr, buf, len);
	
	return ret;
}

static int tpd_compare_tp(struct tpd_classdev_t *cdev, unsigned char *data)
{
	int i_ret = 0;
	
	i_ret = tpd_check_fw_for_upgrade(cdev, data, cdev->tp_fw.size, 0);
	
	return i_ret;
}

static int tpd_register_fw_class(struct synaptics_rmi4_data *data)
{
	tpd_fw_cdev.name = "touchscreen";
	tpd_fw_cdev.flash_fw = tpd_flash_firmware;
	tpd_fw_cdev.private = (void*)data;
	tpd_fw_cdev.read_block = tpd_read_block;
	tpd_fw_cdev.write_block = tpd_write_block;
	tpd_fw_cdev.compare_tp = tpd_compare_tp;
	tpd_fw_cdev.check_tp = tpd_check;
	tpd_classdev_register(&(data->i2c_client->dev), &tpd_fw_cdev);
	tpd_init_tpinfo(&tpd_fw_cdev);
	
	return 0;
}
#endif

#ifdef CONFIG_OF
//static unsigned char TM_SAMPLE3_f1a_button_codes[] = {KEY_MENU, KEY_HOME,KEY_BACK,KEY_SEARCH};
static unsigned char TM_SAMPLE3_f1a_button_codes[] = {KEY_BACK, KEY_HOME,KEY_MENU};
static struct synaptics_dsx_cap_button_map TM_SAMPLE3_cap_button_map = {
	.nbuttons = ARRAY_SIZE(TM_SAMPLE3_f1a_button_codes),
	.map = TM_SAMPLE3_f1a_button_codes,
};	

static int syna_parse_dt(struct device *dev, struct synaptics_dsx_platform_data *pdata)
{
#if 0
	int rc;
	struct mxt_config_info *info;
	struct device_node *temp, *np = dev->of_node;
	struct property *prop;
	u32 temp_val;
#endif
	int rc;


	//pdata->gpio_init = touchscreen_gpio_init;
	//pdata->power	= touchscreen_power;
	//pdata->reset	= touchscreen_reset;
	//pdata->irq	= touchscreen_irq;
	//pdata->max_y_position = SYNAPTICS_MAX_Y_POSITION;	// 0 - no vkey, do nothing
	//strcpy(pdata->fwfile,SYN_FW_NAME);
	pdata->irq_gpio			= GPIO_TS_IRQ;
	pdata->reset_delay_ms	= 100;
	pdata->reset_gpio		= GPIO_TS_RST;
	pdata->vdd				= VREG_VDD;
	pdata->vbus				= VREG_VBUS;
	pdata->irq_flags		= IRQF_TRIGGER_FALLING;	
	pdata->x_flip			=0;
	pdata->y_flip			=0;
	pdata->cap_button_map	= &TM_SAMPLE3_cap_button_map;

	pr_info("the vdd is %s  and vbus is %s\n",pdata->vdd,pdata->vbus);
	rc = of_property_read_u32(dev->of_node, "synaptics,max_y", &pdata->maxy_offset);
	if (rc) {
		dev_err(dev, "Failed to read display max x\n");
		//pdata->maxy_offset=-1;
		return -EINVAL;
	}	
	printk("maxy_offset:%d\n",pdata->maxy_offset);	
	return 0;
}
#endif
#if defined(CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE)
int syna_update_flag = 0;
extern int syna_get_fw_ver(struct i2c_client *client, char *pfwfile);
extern int fwu_start_reflash(void);

//extern int syna_fwupdate(struct i2c_client *client, char *pfwfile);
extern int syna_fwupdate_init(struct i2c_client *client);
extern int rmi4_fw_update_module_init(void);
extern int syna_fwupdate_deinit(struct i2c_client *client);
#endif 
extern char *syna_file_name;
#if defined(CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_TEST_REPORTING)
extern int rmi4_f54_module_init(void);
#endif
#if defined(CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_RMI4_DEV)
extern int rmidev_module_init(void);
#endif

static void synaptics_get_configid(
	struct synaptics_rmi4_data *ts,
	char *p_chip_type,
	char *p_sensor,
	int *p_fw_ver )
{
	int ret;

	if ( !ts )
		return;

	ret = synaptics_rmi4_i2c_read(ts, ts->f34_ctrl_base_addr, (char *)&ts->config_id, 4);
	if (ret < 0){
		pr_err("%s: failed to get ts f34.ctrl_base\n",__func__);
	}	

	pr_info("chip_type=0x%x, sensor=0x%x, fw_ver=0x%x\n", 		
		ts->config_id.chip_type,
		ts->config_id.sensor,
		ts->config_id.fw_ver);


	if ( !p_chip_type || !p_sensor || !p_fw_ver )
		return;
	
	switch (ts->config_id.chip_type){
	case '1':
		sprintf(p_chip_type,"S2200(0x%x)", ts->config_id.chip_type);
		break;
	case '2':
		sprintf(p_chip_type,"S2202(0x%x)", ts->config_id.chip_type);
		break;
	case '3':
		sprintf(p_chip_type,"S3200(0x%x)", ts->config_id.chip_type);
		break;
	case '4':
		sprintf(p_chip_type,"S3202(0x%x)", ts->config_id.chip_type);
		break;
	case '5':
		sprintf(p_chip_type,"S3203(0x%x)", ts->config_id.chip_type);
		break;
	case '6':
		sprintf(p_chip_type,"S7020(0x%x)", ts->config_id.chip_type);
		break;
	case '7':
		sprintf(p_chip_type,"S7300(0x%x)", ts->config_id.chip_type);
		break;
	default:
		sprintf(p_chip_type,"unknown(0x%x)", ts->config_id.chip_type);
		break;
	}

	switch(ts->config_id.sensor){
	case '1':
		sprintf(p_sensor, "TPK(0x%x)",ts->config_id.sensor );
		touch_moudle=TPK;
		break;
	case '2':
		sprintf(p_sensor, "Truly(0x%x)",ts->config_id.sensor);
		touch_moudle=TRULY;
		break;
	case '3':
		sprintf(p_sensor, "Success(0x%x)",ts->config_id.sensor);
		touch_moudle=SUCCESS;
		break;
	case '4':
		sprintf(p_sensor, "Ofilm(0x%x)",ts->config_id.sensor);
		touch_moudle=OFILM;
		break;
	case '5':
		sprintf(p_sensor, "Lead(0x%x)",ts->config_id.sensor);
		touch_moudle=LEAD;
		break;
	case '6':
		sprintf(p_sensor, "Wintek(0x%x)",ts->config_id.sensor);
		touch_moudle=WINTEK;
		break;
	case '7':
		sprintf(p_sensor, "Laibao(0x%x)",ts->config_id.sensor);
		touch_moudle=LAIBAO;
		break;
	case '8':
		sprintf(p_sensor, "CMI(0x%x)",ts->config_id.sensor);
		touch_moudle=CMI;
		break;
	case '9':
		sprintf(p_sensor, "ECW(0x%x)",ts->config_id.sensor);
		touch_moudle=ECW;
		break;
	case 'A':
		sprintf(p_sensor, "Goworld(0x%x)",ts->config_id.sensor);
		touch_moudle=GOWORLD;
		break;
	case 'B':
		sprintf(p_sensor, "Baoming(0x%x)",ts->config_id.sensor);
		touch_moudle=BAOMING;
		break;				
	case 'E':
		sprintf(p_sensor, "JUNDA(0x%x)",ts->config_id.sensor);
		touch_moudle=JUNDA;
		break;
	default:
		sprintf(p_sensor, "unknown(0x%x)",ts->config_id.sensor);
		touch_moudle=UNKNOW;
		break;
	}

	*p_fw_ver = ts->config_id.fw_ver;
	
//#if defined(CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE)
	if(touch_moudle<SYN_MOUDLE_NUM_MAX){
	syna_file_name= syn_fwfile_table[touch_moudle];
	pr_info("syna_file_name:%s\n",syna_file_name);
	}
//#endif
	pr_info("chip: %s, sensor %s, fw 0x%x \n", p_chip_type, p_sensor, *p_fw_ver);

	return;
}


static int
proc_read_val(char *page, char **start, off_t off, int count, int *eof,
			  void *data)
{
	int len = 0;
	char chiptype[16], sensor[16];
	int fw_ver=0;
	int ready_fw_ver=-1;
	if ( syn_ts == NULL)
		return -1;
	
	len += sprintf(page + len, "Manufacturer : %s\n", "Synaptics");

	synaptics_get_configid( syn_ts, (char *)&chiptype,(char *)&sensor, &fw_ver);
#if defined(CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE)
	if(touch_moudle<SYN_MOUDLE_NUM_MAX)
	ready_fw_ver=syna_get_fw_ver(syn_ts->i2c_client,syn_fwfile_table[touch_moudle]);
#endif	
	len += sprintf(page + len, "chip type : %s \n", chiptype );
	len += sprintf(page + len, "sensor partner : %s \n", sensor );
	len += sprintf(page + len, "FW Revision : %c%c \n", fw_ver&0x000000ff, (fw_ver&0x0000ff00)>>8);
#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE
	len += sprintf(page + len, "update flag : 0x%x\n", syna_update_flag);
#endif
	if(ready_fw_ver!=-1){
		len += sprintf(page + len, "need update : %s\n", (ready_fw_ver>fw_ver?"yes":"no"));
		len += sprintf(page + len, "ready fw version : %c%c\n", (ready_fw_ver&0x0000ff00)>>8, ready_fw_ver&0x000000ff);
	}else
	{
		len += sprintf(page + len, "no fw to update\n");
	}
	if (off + count >= len)
		*eof = 1;

	if (len < off)
		return 0;

	*start = page + off;
	return ((count < len - off) ? count : len - off);
}

static int proc_write_val(struct file *file, const char *buffer,
		   unsigned long count, void *data)
{
	unsigned long val;
	sscanf(buffer, "%lu", &val);

#if defined(CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE)
	syna_update_flag = 0;
	if(touch_moudle>=SYN_MOUDLE_NUM_MAX)
	{
		printk("touchscreen moudle unknow!");
		syna_update_flag = 1;
		return -EINVAL;
	}
	disable_irq(syn_ts->i2c_client->irq);
	if(fwu_start_reflash())
	{
		enable_irq(syn_ts->i2c_client->irq);
		syna_update_flag = 1;
		pr_info("syna fw update fail! \n" );
		return -EINVAL;
	}
	
	enable_irq(syn_ts->i2c_client->irq);
	
	syna_update_flag = 2;
	pr_info("syna fw update Ok! \n" );
#endif

	return -EINVAL;
}

#ifdef CONFIG_FB
static void configure_sleep(struct synaptics_rmi4_data *rmi4_data)
{
	int retval = 0;

	rmi4_data->fb_notif.notifier_call = fb_notifier_callback;

	retval = fb_register_client(&rmi4_data->fb_notif);
	if (retval)
		dev_err(&rmi4_data->i2c_client->dev,
			"Unable to register fb_notifier: %d\n", retval);
	return;
}
#elif defined CONFIG_HAS_EARLYSUSPEND
static void configure_sleep(struct synaptics_rmi4_data *rmi4_data)
{
	rmi4_data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	rmi4_data->early_suspend.suspend = synaptics_rmi4_early_suspend;
	rmi4_data->early_suspend.resume = synaptics_rmi4_late_resume;
	register_early_suspend(&rmi4_data->early_suspend);

	return;
}
#else
static void configure_sleep(struct synaptics_rmi4_data *rmi4_data)
{
	return;
}
#endif


 /**
 * synaptics_rmi4_probe()
 *
 * Called by the kernel when an association with an I2C device of the
 * same name is made (after doing i2c_add_driver).
 *
 * This funtion allocates and initializes the resources for the driver
 * as an input driver, turns on the power to the sensor, queries the
 * sensor for its supported Functions and characteristics, registers
 * the driver to the input subsystem, sets up the interrupt, handles
 * the registration of the early_suspend and late_resume functions,
 * and creates a work queue for detection of other expansion Function
 * modules.
 */
static int __devinit synaptics_rmi4_probe(struct i2c_client *client,
		const struct i2c_device_id *dev_id)
{
	int retval;
	unsigned char ii;
	unsigned char attr_count;
	struct proc_dir_entry *dir, *refresh;
	struct synaptics_rmi4_f1a_handle *f1a;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_data *rmi4_data;
	struct synaptics_rmi4_device_info *rmi;
	struct synaptics_dsx_platform_data *platform_data; 
	char chiptype[16], sensor[16];
	int fw_ver=0;
	uint8_t reg_data;// zhangzhao 2014-5-19 for tsc hardware test

	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev,
				"%s: SMBus byte data not supported\n",
				__func__);
		return -EIO;
	}

#if 1
//#ifdef CONFIG_OF
		platform_data = devm_kzalloc(&client->dev,
			sizeof(struct synaptics_dsx_platform_data), GFP_KERNEL);
		platform_data = kzalloc(sizeof(struct synaptics_dsx_platform_data), GFP_KERNEL);		
		if (!platform_data) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -EINVAL;
		}
		retval=syna_parse_dt(&client->dev, platform_data);
		if(retval<0)
			goto err_devm;
#else	
		platform_data = client->dev.platform_data;
		if (!platform_data) {
			dev_err(&client->dev,
				"%s: No platform data found\n",
				__func__);
		return -EINVAL;
}

#endif

	rmi4_data = kzalloc(sizeof(*rmi4_data) * 2, GFP_KERNEL);
	if (!rmi4_data) {
		dev_err(&client->dev,
				"%s: Failed to alloc mem for rmi4_data\n",
				__func__);
		retval = -ENOMEM;
		goto err_devm;

	}

	rmi = &(rmi4_data->rmi4_mod_info);

	rmi4_data->input_dev = input_allocate_device();
	if (rmi4_data->input_dev == NULL) {
		dev_err(&client->dev,
				"%s: Failed to allocate input device\n",
				__func__);
		retval = -ENOMEM;
		goto err_input_device;
	}

/*
	if (platform_data->regulator_en) {
		rmi4_data->regulator = regulator_get(&client->dev, "vdd");
		if (IS_ERR(rmi4_data->regulator)) {
			dev_err(&client->dev,
					"%s: Failed to get regulator\n",
					__func__);
			retval = PTR_ERR(rmi4_data->regulator);
			goto err_regulator;
		}
		regulator_enable(rmi4_data->regulator);
	}
*/


	
	rmi4_data->i2c_client = client;
	rmi4_data->current_page = MASK_8BIT;
	rmi4_data->board = platform_data;
	rmi4_data->touch_stopped = false;
	rmi4_data->sensor_sleep = false;

	rmi4_data->i2c_read = synaptics_rmi4_i2c_read;
	rmi4_data->i2c_write = synaptics_rmi4_i2c_write;
	rmi4_data->irq_enable = synaptics_rmi4_irq_acquire;
	rmi4_data->reset_device = synaptics_rmi4_reset_device;

	rmi4_data->flip_x = rmi4_data->board->x_flip;
	rmi4_data->flip_y = rmi4_data->board->y_flip;
	rmi4_data->swap_axes = false;

	rmi4_data->reset_delay_ms = rmi4_data->board->reset_delay_ms ?
		rmi4_data->board->reset_delay_ms : 90;

	retval=touchscreen_gpio_init(rmi4_data,1,platform_data->vdd,platform_data->vbus,
	platform_data->reset_gpio,platform_data->irq_gpio);
	if ( retval < 0 ){
		pr_err("%s, gpio init failed! %d\n", __func__, retval);
		goto err_regulator;
	}


	init_waitqueue_head(&rmi4_data->wait);
	mutex_init(&(rmi4_data->rmi4_io_ctrl_mutex));
	mutex_init(&(rmi4_data->rmi4_report_mutex));



		touchscreen_reset(0,platform_data->reset_gpio);
		//touchscreen_power(0);
		msleep(100);
		touchscreen_power(1);
		msleep(10);
		touchscreen_reset(1,platform_data->reset_gpio);
		msleep(rmi4_data->reset_delay_ms);

	exp_data.queue_work=false;

	if ( !detect_device( rmi4_data )){
		pr_info("%s, device is not exsit.\n", __func__);
		retval=-EIO;
		goto err_detect;
	}

/*
	if (platform_data->reset_gpio) {
		if (gpio_is_valid(platform_data->reset_gpio)) {
			retval = gpio_request(platform_data->reset_gpio, "touch_reset");
			if (retval) {
				dev_err(&client->dev,
					"%s: Failed to request touch_reset GPIO, rc=%d\n",
					__func__, retval);
				goto err_request_reset_gpio;
			}		
			retval = gpio_direction_output(platform_data->reset_gpio, 1);
			if (retval) {
				dev_err(&client->dev,
					"%s: Failed to set reset GPIO direction, rc=%d\n",
					__func__, retval);
				goto err_reset;
			}
			gpio_set_value(platform_data->reset_gpio, 0);
			msleep(10);
			gpio_set_value(platform_data->reset_gpio, 1);
			msleep(rmi4_data->reset_delay_ms);
		} 
	} else {
		synaptics_rmi4_reset_command(rmi4_data);
	}
*/
	retval = synaptics_rmi4_query_device(rmi4_data);
	if (retval < 0) {
		dev_err(&client->dev,
				"%s: Failed to query device\n",
				__func__);
		goto err_query_device;
	}

	if (rmi4_data->swap_axes)
		synaptics_rmi4_swap_axis(rmi4_data);

	i2c_set_clientdata(client, rmi4_data);
	synaptics_wq = create_singlethread_workqueue("synaptics_wq");
	if (!synaptics_wq){
		pr_err("Could not create work queue synaptics_wq: no memory");
		retval = -ESRCH;
		goto err_create_singlethread;
	}
	INIT_WORK(&rmi4_data->work, synaptics_work_func);

	syn_ts=rmi4_data;
	synaptics_get_configid( syn_ts, (char *)&chiptype,(char *)&sensor, &fw_ver);

	rmi4_data->input_dev->name = DRIVER_NAME;
	rmi4_data->input_dev->phys = INPUT_PHYS_NAME;
	rmi4_data->input_dev->id.bustype = BUS_I2C;
	rmi4_data->input_dev->id.product = SYNAPTICS_DSX_DRIVER_PRODUCT;
	rmi4_data->input_dev->id.version = SYNAPTICS_DSX_DRIVER_VERSION;
	rmi4_data->input_dev->dev.parent = &client->dev;
	input_set_drvdata(rmi4_data->input_dev, rmi4_data);

	set_bit(EV_SYN, rmi4_data->input_dev->evbit);
	set_bit(EV_KEY, rmi4_data->input_dev->evbit);
	set_bit(EV_ABS, rmi4_data->input_dev->evbit);
	set_bit(BTN_TOUCH, rmi4_data->input_dev->keybit);
#ifdef TYPE_B_PROTOCOL	
	set_bit(BTN_TOOL_FINGER, rmi4_data->input_dev->keybit);
#endif

#ifdef INPUT_PROP_DIRECT
	set_bit(INPUT_PROP_DIRECT, rmi4_data->input_dev->propbit);
#endif

	rmi4_data->sensor_max_y = rmi4_data->sensor_max_y - rmi4_data->board->maxy_offset;

	input_set_abs_params(rmi4_data->input_dev, ABS_X,
			     0, rmi4_data->sensor_max_x, 0, 0);
	input_set_abs_params(rmi4_data->input_dev, ABS_Y,
			     0, rmi4_data->sensor_max_y, 0, 0);
	input_set_abs_params(rmi4_data->input_dev, ABS_PRESSURE,
			     0, 255, 0, 0);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_POSITION_X, 0,
			rmi4_data->sensor_max_x, 0, 0);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_POSITION_Y, 0,
			rmi4_data->sensor_max_y, 0, 0);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_PRESSURE, 0, 255, 0, 0);
#ifdef REPORT_2D_W
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_TOUCH_MAJOR, 0,
			MAX_ABS_MT_TOUCH_MAJOR, 0, 0);
#endif

#ifdef TYPE_B_PROTOCOL
#ifdef KERNEL_ABOVE_3_7
	/* input_mt_init_slots now has a "flags" parameter */
	input_mt_init_slots(rmi4_data->input_dev,
			rmi4_data->num_of_fingers, INPUT_MT_DIRECT);
#else
	input_mt_init_slots(rmi4_data->input_dev,
			rmi4_data->num_of_fingers);
#endif
#endif

	f1a = NULL;
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F1A)
				f1a = fhandler->data;
		}
	}

	if (f1a) {
		for (ii = 0; ii < f1a->valid_button_count; ii++) {
			set_bit(f1a->button_map[ii],
					rmi4_data->input_dev->keybit);
			input_set_capability(rmi4_data->input_dev,
					EV_KEY, f1a->button_map[ii]);
		}
	}

	retval = input_register_device(rmi4_data->input_dev);
	if (retval) {
		dev_err(&client->dev,
				"%s: Failed to register input device\n",
				__func__);
		goto err_register_input;
	}


//#ifdef CONFIG_HAS_EARLYSUSPEND
//	rmi4_data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
//	rmi4_data->early_suspend.suspend = synaptics_rmi4_early_suspend;
//	rmi4_data->early_suspend.resume = synaptics_rmi4_late_resume;
//	register_early_suspend(&rmi4_data->early_suspend);
//#endif
	configure_sleep(rmi4_data);
	touchscreen_suspend_pm=synaptics_rmi4_suspend_pm;
	touchscreen_resume_pm=synaptics_rmi4_resume_pm;

	if (!exp_data.initialized) {
		mutex_init(&exp_data.mutex);
		INIT_LIST_HEAD(&exp_data.list);
		exp_data.initialized = 1;
	}

	exp_data.workqueue =
			create_singlethread_workqueue("dsx_exp_workqueue");
	INIT_DELAYED_WORK(&exp_data.work,
			synaptics_rmi4_detection_work);
	exp_data.rmi4_data = rmi4_data;
	exp_data.queue_work = true;
	queue_delayed_work(exp_data.workqueue,
			&exp_data.work,
			msecs_to_jiffies(EXP_FN_WORK_DELAY_MS));

	if (platform_data->gpio_config) {
		retval = platform_data->gpio_config(platform_data->irq_gpio,
							true);
		if (retval < 0) {
			dev_err(&client->dev,
					"%s: Failed to configure GPIO\n",
					__func__);
			goto err_gpio;
		}
	}
		

	rmi4_data->irq = gpio_to_irq(platform_data->irq_gpio);

	retval = synaptics_rmi4_irq_acquire(rmi4_data, true);
	if (retval < 0) {
		dev_err(&client->dev,
				"%s: Failed to acquire irq\n",
				__func__);

		goto err_enable_irq;
	}


	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++) {
		retval = sysfs_create_file(&rmi4_data->input_dev->dev.kobj,
				&attrs[attr_count].attr);
		if (retval < 0) {
			dev_err(&client->dev,
					"%s: Failed to create sysfs attributes\n",
					__func__);
			goto err_sysfs;
		}
	}
	#if defined(CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE)
	rmi4_fw_update_module_init();
	syna_fwupdate_init(client);
	#endif	
	#if defined(CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_TEST_REPORTING)
	rmi4_f54_module_init();
	#endif
	#if defined(CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_RMI4_DEV)
	rmidev_module_init();
	#endif
	//dir = proc_mkdir("driver", NULL);
	//  refresh = create_proc_entry("tsc_id", 0664, dir);
	refresh = create_proc_entry("driver/ts_information",
				    0664, NULL);
	dir = NULL;    
	  if (refresh) {
		  refresh->data 	  = NULL;
		  refresh->read_proc  = proc_read_val;
		  refresh->write_proc = proc_write_val;
	  }

	  tpd_register_fw_class(rmi4_data);
	  create_synapatics_touch_proc_file();
// zhangzhao 2014-5-19 for tsc hardware test
	  reg_data = 0xFA;
	synaptics_rmi4_i2c_write(rmi4_data,0x009B,&reg_data,1);
	  reg_data = 0x00;
	
	synaptics_rmi4_i2c_write(rmi4_data,0x0400,&reg_data,1);
// zhangzhao 2014-5-19 for tsc hardware test end
	return retval;

err_sysfs:
	for (attr_count--; attr_count >= 0; attr_count--) {
		sysfs_remove_file(&rmi4_data->input_dev->dev.kobj,
				&attrs[attr_count].attr);
	}

err_enable_irq:
err_gpio:
	input_unregister_device(rmi4_data->input_dev);

err_register_input:
err_create_singlethread:	
err_query_device:

	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F1A)
				synaptics_rmi4_f1a_kfree(fhandler);
			else
				kfree(fhandler->data);
			
			kfree(fhandler);
		}
	}
	
//err_request_reset_gpio:
//err_reset:
//	if (gpio_is_valid(platform_data->reset_gpio))
//		gpio_free(platform_data->reset_gpio);
err_detect:
	touchscreen_power(0);
	touchscreen_gpio_init(rmi4_data,0,platform_data->vdd,platform_data->vbus,
		platform_data->reset_gpio,platform_data->irq_gpio);


err_regulator:
	input_free_device(rmi4_data->input_dev);
	rmi4_data->input_dev = NULL;

err_input_device:
	kfree(rmi4_data);
err_devm:
	kfree(platform_data);
	
	return retval;
}

 /**
 * synaptics_rmi4_remove()
 *
 * Called by the kernel when the association with an I2C device of the
 * same name is broken (when the driver is unloaded).
 *
 * This funtion terminates the work queue, stops sensor data acquisition,
 * frees the interrupt, unregisters the driver from the input subsystem,
 * turns off the power to the sensor, and frees other allocated resources.
 */
static int __devexit synaptics_rmi4_remove(struct i2c_client *client)
{
	unsigned char attr_count;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_data *rmi4_data = i2c_get_clientdata(client);
	struct synaptics_rmi4_device_info *rmi;
	const struct synaptics_dsx_platform_data *platform_data =
			rmi4_data->board;
#if defined(CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE)
		syna_fwupdate_deinit(client);
#endif

	rmi = &(rmi4_data->rmi4_mod_info);

	cancel_delayed_work_sync(&exp_data.work);
	flush_workqueue(exp_data.workqueue);
	destroy_workqueue(exp_data.workqueue);
	remove_synapatics_touch_proc_file();

	rmi4_data->touch_stopped = true;
	wake_up(&rmi4_data->wait);

	synaptics_rmi4_irq_acquire(rmi4_data, false);

	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++) {
		sysfs_remove_file(&rmi4_data->input_dev->dev.kobj,
				&attrs[attr_count].attr);
	}

	input_unregister_device(rmi4_data->input_dev);

	touchscreen_power(0);
	touchscreen_gpio_init(NULL,0,platform_data->vdd,platform_data->vbus,
		platform_data->reset_gpio,platform_data->irq_gpio);


	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F1A)
				synaptics_rmi4_f1a_kfree(fhandler);
			else
				kfree(fhandler->data);
			kfree(fhandler);
		}
	}
	input_free_device(rmi4_data->input_dev);

	kfree(rmi4_data);

	return 0;
}

#ifdef CONFIG_PM
 /**
 * synaptics_rmi4_sensor_sleep()
 *
 * Called by synaptics_rmi4_early_suspend() and synaptics_rmi4_suspend().
 *
 * This function stops finger data acquisition and puts the sensor to sleep.
 */
static void synaptics_rmi4_sensor_sleep(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char device_ctrl;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to enter sleep mode\n",
				__func__);
		rmi4_data->sensor_sleep = false;
		return;
	}

	device_ctrl = (device_ctrl & ~MASK_3BIT);
	device_ctrl = (device_ctrl | NO_SLEEP_OFF | SENSOR_SLEEP);
	pr_info("%s, ---------device_ctrl is %x.\n", __func__,device_ctrl);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to enter sleep mode\n",
				__func__);
		rmi4_data->sensor_sleep = false;
		return;
	} else {
		rmi4_data->sensor_sleep = true;
	}

	return;
}

 /**
 * synaptics_rmi4_sensor_wake()
 *
 * Called by synaptics_rmi4_resume() and synaptics_rmi4_late_resume().
 *
 * This function wakes the sensor from sleep.
 */
static void synaptics_rmi4_sensor_wake(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char device_ctrl;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to wake from sleep mode\n",
				__func__);
		rmi4_data->sensor_sleep = true;
		return;
	}

	device_ctrl = (device_ctrl & ~MASK_3BIT);
	device_ctrl = (device_ctrl | NO_SLEEP_OFF | NORMAL_OPERATION);
	pr_debug("%s, ---------device_ctrl is %x.\n", __func__,device_ctrl);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to wake from sleep mode\n",
				__func__);
		rmi4_data->sensor_sleep = true;
		return;
	} else {
		rmi4_data->sensor_sleep = false;
	}

	return;
}

#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
				unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct synaptics_rmi4_data *rmi4_data =
		container_of(self, struct synaptics_rmi4_data, fb_notif);

	pr_debug("%s.\n", __func__);

	if (evdata && evdata->data && event == FB_EVENT_BLANK &&
		rmi4_data && rmi4_data->i2c_client) {
		blank = evdata->data;
		pr_debug("%s.111111111blank is %x \n", __func__,*blank);
		if (*blank == FB_BLANK_UNBLANK)
			synaptics_rmi4_resume(&(rmi4_data->input_dev->dev));
		else if (*blank == FB_BLANK_POWERDOWN)
			synaptics_rmi4_suspend(&(rmi4_data->input_dev->dev));
	}

	return 0;
}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
 /**
 * synaptics_rmi4_early_suspend()
 *
 * Called by the kernel during the early suspend phase when the system
 * enters suspend.
 *
 * This function calls synaptics_rmi4_sensor_sleep() to stop finger
 * data acquisition and put the sensor to sleep.
 */
static void synaptics_rmi4_early_suspend(struct early_suspend *h)
{
	struct synaptics_rmi4_data *rmi4_data =
			container_of(h, struct synaptics_rmi4_data,
			early_suspend);
	rmi4_data->touch_stopped = true;
	wake_up(&rmi4_data->wait);
	synaptics_rmi4_irq_enable(rmi4_data, false);
	synaptics_rmi4_sensor_sleep(rmi4_data);

	if (rmi4_data->full_pm_cycle)
		synaptics_rmi4_suspend(&(rmi4_data->input_dev->dev));

	return;
}

 /**
 * synaptics_rmi4_late_resume()
 *
 * Called by the kernel during the late resume phase when the system
 * wakes up from suspend.
 *
 * This function goes through the sensor wake process if the system wakes
 * up from early suspend (without going into suspend).
 */
static void synaptics_rmi4_late_resume(struct early_suspend *h)
{
	struct synaptics_rmi4_data *rmi4_data =
			container_of(h, struct synaptics_rmi4_data,
			early_suspend);

	pr_info("%s, rmi4_data->full_pm_cycle is %d.\n", __func__,rmi4_data->full_pm_cycle);
	if (rmi4_data->full_pm_cycle)
		synaptics_rmi4_resume(&(rmi4_data->input_dev->dev));

	if (rmi4_data->sensor_sleep == true) {
		synaptics_rmi4_sensor_wake(rmi4_data);
		rmi4_data->touch_stopped = false;
		synaptics_rmi4_irq_enable(rmi4_data, true);
	}

	return;
}
#endif

static void synaptics_rmi4_release_all(struct synaptics_rmi4_data *rmi4_data)
{

#ifdef TYPE_B_PROTOCOL

	int finger;
	int max_num_fingers = rmi4_data->num_of_fingers;
#endif
	mutex_lock(&(rmi4_data->rmi4_report_mutex));

#ifdef TYPE_B_PROTOCOL
	for (finger = 0; finger < max_num_fingers; finger++) {
		input_mt_slot(rmi4_data->input_dev, finger);
		input_mt_report_slot_state(rmi4_data->input_dev,
				MT_TOOL_FINGER, 0);
	}
#endif
	input_report_key(rmi4_data->input_dev, BTN_TOUCH, 0);
#ifdef TYPE_B_PROTOCOL
	input_report_key(rmi4_data->input_dev,
			BTN_TOOL_FINGER, 0);
#endif

#ifndef TYPE_B_PROTOCOL
		input_mt_sync(rmi4_data->input_dev);
#else
	/* sync after groups of events */
	#ifdef KERNEL_ABOVE_3_7
	input_mt_sync_frame(rmi4_data->input_dev);
	#endif
#endif

	input_sync(rmi4_data->input_dev);

	mutex_unlock(&(rmi4_data->rmi4_report_mutex));

	rmi4_data->fingers_on_2d = false;

	pr_info("%s, release all finger .\n", __func__);	

}


 /**
 * synaptics_rmi4_suspend()
 *
 * Called by the kernel during the suspend phase when the system
 * enters suspend.
 *
 * This function stops finger data acquisition and puts the sensor to
 * sleep (if not already done so during the early suspend phase),
 * disables the interrupt, and turns off the power to the sensor.
 */
static int g_boot = 1;

static int synaptics_rmi4_suspend(struct device *dev)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	const struct synaptics_dsx_platform_data *platform_data =
			rmi4_data->board;
	pr_info("synaptics_rmi4_suspend\n");
	g_boot = 0;
	if (!rmi4_data->sensor_sleep) {
		rmi4_data->touch_stopped = true;
		wake_up(&rmi4_data->wait);
		synaptics_rmi4_irq_enable(rmi4_data, false);
		synaptics_rmi4_sensor_sleep(rmi4_data);
		synaptics_rmi4_release_all(rmi4_data);
		
	}

  touchscreen_power(0);
  msleep(100);  
  touchscreen_reset(0,platform_data->reset_gpio);
	//if (platform_data->regulator_en)
	//	regulator_disable(rmi4_data->regulator);

	return 0;
}

 /**
 * synaptics_rmi4_resume()
 *
 * Called by the kernel during the resume phase when the system
 * wakes up from suspend.
 *
 * This function turns on the power to the sensor, wakes the sensor
 * from sleep, enables the interrupt, and starts finger data
 * acquisition.
 */
static int synaptics_rmi4_resume(struct device *dev)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	const struct synaptics_dsx_platform_data *platform_data =rmi4_data->board;
	int retval =0; // zhangzhao 2014-5-19 for tsc hardware test
	unsigned char reg;// zhangzhao 2014-5-19 for tsc hardware test
	//if (platform_data->regulator_en)
	//	regulator_enable(rmi4_data->regulator);
	
	if(1 == g_boot) {
		printk("%s, ommit on startup resume.\n", __func__);	
		return 0;
	}
		touchscreen_power(1);
		msleep(10);
		touchscreen_reset(1,platform_data->reset_gpio);
		msleep(80);
	
	pr_debug("synaptics_rmi4_resume\n");	
// zhangzhao 2014-5-19 for tsc hardware test

	reg	= 0xFA;
	retval = synaptics_rmi4_i2c_write(rmi4_data,0x009B,&reg,1);
	if(retval<0)
		{
			dev_err(&rmi4_data->i2c_client->dev,
					"%s: Failed to write 0x9b register\n",
					__func__);
	}
		reg	= 0x00;

	retval = synaptics_rmi4_i2c_write(rmi4_data,0x0400,&reg,1);
	if(retval<0)
		{
			dev_err(&rmi4_data->i2c_client->dev,
					"%s: Failed to write 0x0400 register\n",
					__func__);
	}
	synaptics_rmi4_i2c_read(rmi4_data,0x009B,&reg,1);
	pr_info("synaptics_read 0x9b is %x\n",reg);
	synaptics_rmi4_i2c_read(rmi4_data,0x0400,&reg,1);
	pr_info("synaptics_read 0x0400 is %x\n",reg);
// zhangzhao 2014-5-19 for tsc hardware test


	synaptics_rmi4_sensor_wake(rmi4_data);
	rmi4_data->touch_stopped = false;
	synaptics_rmi4_irq_enable(rmi4_data, true);

	return 0;
}
int synaptics_rmi4_suspend_pm(void)
{

 pr_debug("synaptics_rmi4_suspend_pm\n");
 if (!syn_ts->sensor_sleep) {
	 syn_ts->touch_stopped = true;
	 wake_up(&syn_ts->wait);
	 synaptics_rmi4_irq_enable(syn_ts, false);
	 synaptics_rmi4_sensor_sleep(syn_ts);
 }

 //if (platform_data->regulator_en)
 //  regulator_disable(rmi4_data->regulator);

 return 0;
}
int synaptics_rmi4_resume_pm(void)
{

 pr_debug("synaptics_rmi4_resume_pm\n");  
 synaptics_rmi4_sensor_wake(syn_ts);
 syn_ts->touch_stopped = false;
 synaptics_rmi4_irq_enable(syn_ts, true);

 return 0;
}

#if (!defined(CONFIG_FB) && !defined(CONFIG_HAS_EARLYSUSPEND))
static const struct dev_pm_ops synaptics_rmi4_dev_pm_ops = {
	.suspend = synaptics_rmi4_suspend,
	.resume  = synaptics_rmi4_resume,
};
#else
static const struct dev_pm_ops synaptics_rmi4_dev_pm_ops = {
};
#endif
#endif

static const struct i2c_device_id synaptics_rmi4_id_table[] = {
	{DRIVER_NAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, synaptics_rmi4_id_table);
#ifdef CONFIG_OF
static struct of_device_id syna_match_table[] = {
	{ .compatible = "synaptics,syna-ts",},
	{ },
};
#endif


static struct i2c_driver synaptics_rmi4_driver = {
	.driver = {
		.name = "syna-touchscreen",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &synaptics_rmi4_dev_pm_ops,
#endif
#ifdef CONFIG_OF
		.of_match_table = syna_match_table,
#endif	

	},
	.probe = synaptics_rmi4_probe,
	.remove = __devexit_p(synaptics_rmi4_remove),
	.id_table = synaptics_rmi4_id_table,

};

 /**
 * synaptics_rmi4_init()
 *
 * Called by the kernel during do_initcalls (if built-in)
 * or when the driver is loaded (if a module).
 *
 * This function registers the driver to the I2C subsystem.
 *
 */
static int __init synaptics_rmi4_init(void)
{
	return i2c_add_driver(&synaptics_rmi4_driver);
}

 /**
 * synaptics_rmi4_exit()
 *
 * Called by the kernel when the driver is unloaded.
 *
 * This funtion unregisters the driver from the I2C subsystem.
 *
 */
static void __exit synaptics_rmi4_exit(void)
{
	i2c_del_driver(&synaptics_rmi4_driver);
}

module_init(synaptics_rmi4_init);
module_exit(synaptics_rmi4_exit);

MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics DSX I2C Touch Driver");
MODULE_LICENSE("GPL v2");
