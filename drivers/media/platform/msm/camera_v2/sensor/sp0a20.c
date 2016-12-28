/* Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "msm_sensor.h"
#include "msm_cci.h"
#include "msm_camera_io_util.h"
#include <linux/proc_fs.h>

//#define CONFIG_MSMB_CAMERA_DEBUG
#ifdef CONFIG_MSMB_CAMERA_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif



#define SP0A20_SENSOR_NAME "sp0a20"


DEFINE_MSM_MUTEX(sp0a20_mut);

static struct msm_sensor_ctrl_t sp0a20_s_ctrl;

static struct msm_sensor_power_setting sp0a20_power_setting[] = {
	#if 0
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_STANDBY,
		.config_val = GPIO_OUT_LOW,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VDIG,
		.config_val = 0,
		.delay = 0,
	},
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VIO,
		.config_val = 0,
		.delay = 0,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_VANA,
		.config_val = GPIO_OUT_HIGH,
		.delay = 5,
	},

	{
		.seq_type = SENSOR_CLK,
		.seq_val = SENSOR_CAM_MCLK,
		.config_val = 0,
		.delay = 10,
	},

	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_STANDBY,
		.config_val = GPIO_OUT_HIGH,
		.delay = 10,
	},

	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_STANDBY,
		.config_val = GPIO_OUT_LOW,
		.delay = 10,
	},
	
	{
		.seq_type = SENSOR_I2C_MUX,
		.seq_val = 0,
		.config_val = 0,
		.delay = 0,
	},
	#else
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_VANA,
		.config_val = GPIO_OUT_HIGH,
		.delay = 5,
	},
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VDIG,
		.config_val = 0,
		.delay = 0,
	},
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VIO,
		.config_val = 0,
		.delay = 0,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_STANDBY,
		.config_val = GPIO_OUT_LOW,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_STANDBY,
		.config_val = GPIO_OUT_HIGH,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_STANDBY,
		.config_val = GPIO_OUT_LOW,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_CLK,
		.seq_val = SENSOR_CAM_MCLK,
		.config_val = 0,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_I2C_MUX,
		.seq_val = 0,
		.config_val = 0,
		.delay = 0,
	},
	#endif
};

static struct msm_camera_i2c_reg_conf sp0a20_start_settings[] = {
	{0xfd, 0x00},
	{0x92, 0x61},//01 mipi stream on	0x71
	{0xfd, 0x01},	
	{0x36, 0x00},		
	{0xfd, 0x00},
};

static struct msm_camera_i2c_reg_conf sp0a20_stop_settings[] = {
	{0xfd, 0x00},	
	{0x92, 0x60},//mipi stream disable  0x70
	{0xfd, 0x01},	
	{0x36, 0x02},//disable vsync hsync	
	{0xfd, 0x00},
	{0xe7, 0x03},	
	{0xe7, 0x00},
	{0xfd, 0x00},
 
};

 

static struct msm_camera_i2c_reg_conf sp0a20_recommend_settings[] = {
{0xfd,0x01},
{0x36,0x02},
{0xfd,0x00},
{0x92,0x60},//0x70
{0x0c,0x00},
{0x12,0x02},
{0x13,0x2f},
{0x6d,0x32},
{0x6c,0x32},
{0x6f,0x33},
{0x6e,0x34},
{0x99,0x04},
{0x16,0x38},
{0x17,0x38},
{0x70,0x3a},
{0x14,0x02},
{0x15,0x20},
{0x71,0x23},
{0x69,0x25},
{0x6a,0x1a},
{0x72,0x1c},
{0x75,0x1e},
{0x73,0x3c},
{0x74,0x21},
{0x79,0x00},
{0x77,0x10},
{0x1a,0x4d},
{0x1b,0x27},
{0x1c,0x07},
{0x1e,0x15},
{0x21,0x0e},
{0x22,0x28},
{0x26,0x66},
{0x28,0x0b},
{0x37,0x5a},
{0xfd,0x02},
{0x01,0x80},
{0x52,0x10},
{0x54,0x00},
{0xfd,0x01},
{0x41,0x00},
{0x42,0x00},
{0x43,0x00},
{0x44,0x00},
//yuxin modify for fps 18-22fps ++
{0xfd,0x00},//AE
{0x03,0x02},//0x01
{0x04,0x94},//0x1c//0xc2
{0x05,0x00},
{0x06,0x00},
{0x07,0x00},
{0x08,0x00},
{0x09,0x00},//0x01//0x02//0x04
{0x0a,0xf7},//0xe9//0xf4//0x84
{0xfd,0x01},
{0xf0,0x00},
{0xf7,0x6e},//0x5a//0x4b
{0x02,0x05},//0x08//0x0c//0x0e
{0x03,0x01},
{0x06,0x6e},//0x5a//0x4b
{0x07,0x00},
{0x08,0x01},
{0x09,0x00},
{0xfd,0x02},
{0xbe,0x26},//0xd0//0x84//0x1a
{0xbf,0x02},//0x03//0x04
{0xd0,0x26},//0xd0//0x84//0x1a
{0xd1,0x02},//0x03//0x04
//yuxin modify for fps 18-22fps --
{0xfd,0x01},
{0x5a,0x40},
{0xfd,0x02},
{0xbc,0x70},
{0xbd,0x50},
{0xb8,0x66},
{0xb9,0x88},
{0xba,0x30},
{0xbb,0x45},
{0xfd,0x01},
{0xe0,0x44},
{0xe1,0x36},
{0xe2,0x30},
{0xe3,0x2a},
{0xe4,0x2a},
{0xe5,0x28},
{0xe6,0x28},
{0xe7,0x26},
{0xe8,0x26},
{0xe9,0x26},
{0xea,0x24},
{0xf3,0x24},
{0xf4,0x24},
{0xfd,0x01},
{0x04,0xa0},
{0x05,0x24},
{0x0a,0xa0},
{0x0b,0x24},
{0xfd,0x01},
{0xeb,0x78},
{0xec,0x78},
{0xed,0x05},
{0xee,0x0c},
{0xfd,0x01},
{0xf2,0x4d},
{0xfd,0x02},
{0x5b,0x05},
{0x5c,0xa0},
{0xfd,0x01},
{0x26,0x80},
{0x27,0x4f},
{0x28,0x00},
{0x29,0x20},
{0x2a,0x00},
{0x2b,0x03},
{0x2c,0x00},
{0x2d,0x20},
{0x30,0x00},
{0x31,0x00},
{0xfd,0x01},
{0xa1,0x23},
{0xa2,0x23},
{0xa3,0x20},
{0xa4,0x20},
{0xa5,0x13},
{0xa6,0x13},
{0xa7,0x13},
{0xa8,0x13},
{0xa9,0x0d},
{0xaa,0x0d},
{0xab,0x0b},
{0xac,0x0b},
{0xad,0x00},
{0xae,0x00},
{0xaf,0x00},
{0xb0,0x00},
{0xb1,0x00},
{0xb2,0x00},
{0xb3,0x00},
{0xb4,0x00},
{0xb5,0x00},
{0xb6,0x00},
{0xb7,0x00},
{0xb8,0x00},
{0xfd,0x02},
{0x08,0x00},
{0x09,0x06},
{0x1d,0x03},
{0x1f,0x05},
{0xfd,0x01},
{0x32,0x00},
{0xfd,0x02},
{0x26,0xbf},
{0x27,0xa3},
{0x10,0x00},
{0x11,0x00},
{0x1b,0x80},
{0x1a,0x80},
{0x18,0x27},
{0x19,0x26},
{0x2a,0x00},
{0x2b,0x00},
{0x28,0xf8},
{0x29,0x08},
//yuxin modify for spa020 display yellow or blue(from YangYi)++
{0x66,0x45},
{0x67,0x65},
{0x68,0xdc},
{0x69,0xf7},
{0x6a,0xa5},
{0x7c,0x30},
{0x7d,0x4b},
{0x7e,0xf7},
{0x7f,0x13},
{0x80,0xa6},
{0x70,0x24},
{0x71,0x3d},
{0x72,0x24},
{0x73,0x49},
{0x74,0xaa},
{0x6b,0x0b},
{0x6c,0x24},
{0x6d,0x30},
{0x6e,0x4b},
{0x6f,0xaa},
{0x61,0xf7},
{0x62,0x14},
{0x63,0x4b},
{0x64,0x68},
//yuxin modify for spa020 display yellow or blue(from YangYi)--
{0x65,0x6a},
{0x75,0x80},
{0x76,0x09},
{0x77,0x02},
{0x24,0x25},
{0x0e,0x16},
{0x3b,0x09},
{0xfd,0x02},
{0xde,0x0f},
{0xd7,0x08},
{0xd8,0x08},
{0xd9,0x10},
{0xda,0x14},
{0xe8,0x20},
{0xe9,0x20},
{0xea,0x20},
{0xeb,0x20},
{0xec,0x20},
{0xed,0x20},
{0xee,0x20},
{0xef,0x20},
{0xd3,0x20},
{0xd4,0x48},
{0xd5,0x20},
{0xd6,0x08},
{0xfd,0x01},
{0xd1,0x20},
{0xfd,0x02},
{0xdc,0x05},
{0x05,0x20},
{0xfd,0x02},
{0x81,0x00},
{0xfd,0x01},
{0xfc,0x00},
{0x7d,0x05},
{0x7e,0x05},
{0x7f,0x09},
{0x80,0x08},
{0xfd,0x02},
{0xdd,0x0f},
{0xfd,0x01},
{0x6d,0x08},
{0x6e,0x08},
{0x6f,0x10},
{0x70,0x18},
{0x86,0x18},
{0x71,0x0a},
{0x72,0x0a},
{0x73,0x14},
{0x74,0x14},
{0x75,0x08},
{0x76,0x0a},
{0x77,0x06},
{0x78,0x06},
{0x79,0x25},
{0x7a,0x23},
{0x7b,0x22},
{0x7c,0x00},
{0x81,0x0d},
{0x82,0x18},
{0x83,0x20},
{0x84,0x24},
{0xfd,0x02},
{0x83,0x12},
{0x84,0x14},
{0x86,0x04},
{0xfd,0x01},
{0x61,0x60},
{0x62,0x28},
{0x8a,0x10},
{0xfd,0x01},
{0x8b,0x00},
{0x8c,0x0d},
{0x8d,0x1b},
{0x8e,0x2a},
{0x8f,0x36},
{0x90,0x4a},
{0x91,0x5a},
{0x92,0x67},
{0x93,0x74},
{0x94,0x88},
{0x95,0x98},
{0x96,0xa8},
{0x97,0xb5},
{0x98,0xc0},
{0x99,0xcc},
{0x9a,0xd6},
{0x9b,0xdf},
{0x9c,0xe7},
{0x9d,0xee},
{0x9e,0xf4},
{0x9f,0xfa},
{0xa0,0xff},
{0xfd,0x02},
{0x15,0xcc},
{0x16,0x8c},
{0xa0,0x66},
{0xa1,0x4c},
//yuxin modify for red skin ++
{0xa2,0xce},
{0xa3,0xe7},
{0xa4,0xc0},
{0xa5,0xda},
{0xa6,0xf4},
{0xa7,0xcd},
{0xa8,0xbf},
//yuxin modify for red skin --
{0xa9,0x30},
{0xaa,0x33},
{0xab,0x0f},
{0xac,0x80},
{0xad,0x06},
{0xae,0xfa},
{0xaf,0xda},
{0xb0,0xd9},
{0xb1,0xcd},
{0xb2,0xda},
{0xb3,0xc0},
{0xb4,0xe6},
{0xb5,0x30},
{0xb6,0x33},
{0xb7,0x0f},
{0xfd,0x01},
{0xd3,0x6d},
{0xd4,0x7a},
{0xd5,0x5d},
{0xd6,0x4b},
{0xd7,0x6d},
{0xd8,0x7a},
{0xd9,0x5d},
{0xda,0x4b},
{0xfd,0x01},
{0xdd,0x30},
{0xde,0x10},
{0xdf,0xff},
{0x00,0x00},
{0xfd,0x01},
{0xc2,0xaa},
{0xc3,0x88},
{0xc4,0x77},
{0xc5,0x66},
{0xfd,0x01},
{0xcd,0x10},
{0xce,0x1f},
{0xcf,0x30},
{0xd0,0x45},
{0xfd,0x02},
{0x31,0x60},
{0x32,0x60},
{0x33,0xc0},
{0x35,0x60},
{0x37,0x13},
{0xfd,0x01},
{0x0e,0x80},
{0x0f,0x20},
{0x10,0x80},
{0x11,0x80},
{0x12,0x80},
{0x13,0x80},
{0x14,0x86},
{0x15,0x86},
{0x16,0x86},
{0x17,0x86},
{0xfd,0x00},
{0x31,0x00},//0x06 // yuxin modify for CTS test 2014.05.20
{0xfd,0x01},
{0x32,0x15},
{0x33,0xef},
{0x34,0x07},
{0xd2,0x01},
{0xfb,0x25},
{0xf2,0x49},
{0x35,0x40},
{0x5d,0x11},
{0xfd,0x01},

};



static struct v4l2_subdev_info sp0a20_subdev_info[] = {
	{
		.code   = V4L2_MBUS_FMT_YUYV8_2X8,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.fmt    = 1,
		.order  = 0,
	},
};

static const struct i2c_device_id sp0a20_i2c_id[] = {
	{SP0A20_SENSOR_NAME, (kernel_ulong_t)&sp0a20_s_ctrl},
	{ }
};

static ssize_t sp0a20_camera_id_read_proc(char *page,char **start,off_t off,
	                                                int count,int *eof,void* data)
{		 	   
	int ret;	    
	unsigned char *camera_status = "FRONT Camera ID:Sp0a20 0.3M";  
	ret = strlen(camera_status);    
	sprintf(page,"%s\n",camera_status);	 	    
	return (ret + 1);
}
static void sp0a20_camera_proc_file(void)
{	   
	struct proc_dir_entry *proc_file  = create_proc_entry("driver/camera_id_front",0644,NULL);	    
	if(proc_file)     
	{		  	     
		proc_file->read_proc = sp0a20_camera_id_read_proc;			     
	}else     
	{       
	      pr_err(KERN_INFO "camera_proc_file error!\r\n");	    
	}
 }
static int32_t msm_sp0a20_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	CDBG("%s, E. ", __func__);

	return msm_sensor_i2c_probe(client, id, &sp0a20_s_ctrl);
}

static struct i2c_driver sp0a20_i2c_driver = {
	.id_table = sp0a20_i2c_id,
	.probe  = msm_sp0a20_i2c_probe,
	.driver = {
		.name = SP0A20_SENSOR_NAME,
	},
};

static struct msm_camera_i2c_client sp0a20_sensor_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
};

static const struct of_device_id sp0a20_dt_match[] = {
	{.compatible = "qcom,sp0a20", .data = &sp0a20_s_ctrl},
	{}
};

MODULE_DEVICE_TABLE(of, sp0a20_dt_match);

static struct platform_driver sp0a20_platform_driver = {
	.driver = {
		.name = "qcom,sp0a20",
		.owner = THIS_MODULE,
		.of_match_table = sp0a20_dt_match,
	},
};

static int32_t sp0a20_platform_probe(struct platform_device *pdev)
{
	int32_t rc;
	const struct of_device_id *match;
	CDBG("%s, E.", __func__);
	match = of_match_device(sp0a20_dt_match, &pdev->dev);
	rc = msm_sensor_platform_probe(pdev, match->data);
	if (rc == 0)   
	{       
		sp0a20_camera_proc_file();    
	}   
	return rc;
}

static int __init sp0a20_init_module(void)
{
	int32_t rc;
	pr_info("%s:%d\n", __func__, __LINE__);
	rc = platform_driver_probe(&sp0a20_platform_driver,
		sp0a20_platform_probe);
	if (!rc)
		return rc;
	pr_err("%s:%d rc %d\n", __func__, __LINE__, rc);
	return i2c_add_driver(&sp0a20_i2c_driver);
}

static void __exit sp0a20_exit_module(void)
{
	pr_info("%s:%d\n", __func__, __LINE__);
	if (sp0a20_s_ctrl.pdev) {
		msm_sensor_free_sensor_data(&sp0a20_s_ctrl);
		platform_driver_unregister(&sp0a20_platform_driver);
	} else
		i2c_del_driver(&sp0a20_i2c_driver);
	return;
}




int sp0a20_sensor_config(struct msm_sensor_ctrl_t *s_ctrl, void __user *argp)
{
	struct sensorb_cfg_data *cdata = (struct sensorb_cfg_data *)argp;
	long rc = 0;
	int i = 0;
	mutex_lock(s_ctrl->msm_sensor_mutex);
	CDBG("%s:%d %s cfgtype = %d\n", __func__, __LINE__,
		s_ctrl->sensordata->sensor_name, cdata->cfgtype);
	switch (cdata->cfgtype) {
	case CFG_GET_SENSOR_INFO:
		memcpy(cdata->cfg.sensor_info.sensor_name,
			s_ctrl->sensordata->sensor_name,
			sizeof(cdata->cfg.sensor_info.sensor_name));
		cdata->cfg.sensor_info.session_id =
			s_ctrl->sensordata->sensor_info->session_id;
		for (i = 0; i < SUB_MODULE_MAX; i++)
			cdata->cfg.sensor_info.subdev_id[i] =
				s_ctrl->sensordata->sensor_info->subdev_id[i];
		cdata->cfg.sensor_info.is_mount_angle_valid =
			s_ctrl->sensordata->sensor_info->is_mount_angle_valid;
		cdata->cfg.sensor_info.sensor_mount_angle =
			s_ctrl->sensordata->sensor_info->sensor_mount_angle;
		CDBG("%s:%d sensor name %s\n", __func__, __LINE__,
			cdata->cfg.sensor_info.sensor_name);
		CDBG("%s:%d session id %d\n", __func__, __LINE__,
			cdata->cfg.sensor_info.session_id);
		for (i = 0; i < SUB_MODULE_MAX; i++)
			CDBG("%s:%d subdev_id[%d] %d\n", __func__, __LINE__, i,
				cdata->cfg.sensor_info.subdev_id[i]);
		CDBG("%s:%d mount angle valid %d value %d\n", __func__,
			__LINE__, cdata->cfg.sensor_info.is_mount_angle_valid,
			cdata->cfg.sensor_info.sensor_mount_angle);

		break;
	case CFG_GET_SENSOR_INIT_PARAMS:
		cdata->cfg.sensor_init_params.modes_supported =
			s_ctrl->sensordata->sensor_info->modes_supported;
		cdata->cfg.sensor_init_params.position =
			s_ctrl->sensordata->sensor_info->position;
		cdata->cfg.sensor_init_params.sensor_mount_angle =
			s_ctrl->sensordata->sensor_info->sensor_mount_angle;
		CDBG("%s:%d init params mode %d pos %d mount %d\n", __func__,
			__LINE__,
			cdata->cfg.sensor_init_params.modes_supported,
			cdata->cfg.sensor_init_params.position,
			cdata->cfg.sensor_init_params.sensor_mount_angle);
		break;
	case CFG_SET_SLAVE_INFO: {
		struct msm_camera_sensor_slave_info sensor_slave_info;
		struct msm_camera_power_ctrl_t *p_ctrl;
		uint16_t size;
		int s_index = 0;
		if (copy_from_user(&sensor_slave_info,
				(void *)cdata->cfg.setting,
				sizeof(sensor_slave_info))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}
		/* Update sensor slave address */
		if (sensor_slave_info.slave_addr) {
			s_ctrl->sensor_i2c_client->cci_client->sid =
				sensor_slave_info.slave_addr >> 1;
		}

		/* Update sensor address type */
		s_ctrl->sensor_i2c_client->addr_type =
			sensor_slave_info.addr_type;
		p_ctrl = &s_ctrl->sensordata->power_info;

		/* Update power up sequence */
		size = sensor_slave_info.power_setting_array.size;
		if (p_ctrl->power_setting_size < size) {
			struct msm_sensor_power_setting *tmp;
			tmp = kmalloc(sizeof(*tmp) * size, GFP_KERNEL);
			if (!tmp) {
				pr_err("%s: failed to alloc mem\n", __func__);
				rc = -ENOMEM;
				break;
			}
			kfree(p_ctrl->power_setting);
			p_ctrl->power_setting = tmp;
		}
		p_ctrl->power_setting_size = size;


		rc = copy_from_user(p_ctrl->power_setting, (void *)
			sensor_slave_info.power_setting_array.power_setting,
			size * sizeof(struct msm_sensor_power_setting));
		if (rc) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			kfree(sensor_slave_info.power_setting_array.
				power_setting);
			rc = -EFAULT;
			break;
		}
		CDBG("%s sensor id %x\n", __func__,
			sensor_slave_info.slave_addr);
		CDBG("%s sensor addr type %d\n", __func__,
			sensor_slave_info.addr_type);
		CDBG("%s sensor reg %x\n", __func__,
			sensor_slave_info.sensor_id_info.sensor_id_reg_addr);
		CDBG("%s sensor id %x\n", __func__,
			sensor_slave_info.sensor_id_info.sensor_id);
		for (s_index = 0; s_index <
			p_ctrl->power_setting_size; s_index++) {
			CDBG("%s i %d power up setting %d %d %ld %d\n",
				__func__,
				s_index,
				p_ctrl->power_setting[s_index].seq_type,
				p_ctrl->power_setting[s_index].seq_val,
				p_ctrl->power_setting[s_index].config_val,
				p_ctrl->power_setting[s_index].delay);
		}

		/* Update power down sequence */
		if (!sensor_slave_info.power_setting_array.power_down_setting ||
			0 == size) {
			pr_err("%s: Missing dedicated power down sequence\n",
				__func__);
			break;
		}
		size = sensor_slave_info.power_setting_array.size_down;

		if (p_ctrl->power_down_setting_size < size) {
			struct msm_sensor_power_setting *tmp;
			tmp = kmalloc(sizeof(*tmp) * size, GFP_KERNEL);
			if (!tmp) {
				pr_err("%s: failed to alloc mem\n", __func__);
				rc = -ENOMEM;
				break;
			}
			kfree(p_ctrl->power_down_setting);
			p_ctrl->power_down_setting = tmp;
		}
		p_ctrl->power_down_setting_size = size;


		rc = copy_from_user(p_ctrl->power_down_setting, (void *)
			sensor_slave_info.power_setting_array.
			power_down_setting,
			size * sizeof(struct msm_sensor_power_setting));
		if (rc) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			kfree(sensor_slave_info.power_setting_array.
				power_down_setting);
			rc = -EFAULT;
			break;
		}
		for (s_index = 0; s_index <
			p_ctrl->power_down_setting_size; s_index++) {
			CDBG("%s i %d power DOWN setting %d %d %ld %d\n",
				__func__,
				s_index,
				p_ctrl->power_down_setting[s_index].seq_type,
				p_ctrl->power_down_setting[s_index].seq_val,
				p_ctrl->power_down_setting[s_index].config_val,
				p_ctrl->power_down_setting[s_index].delay);
		}

		break;
	}

	case CFG_SET_INIT_SETTING:
		/* Write Recommend settings */
		pr_err("%s, sensor write init setting!!", __func__);
		rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->
			i2c_write_conf_tbl(s_ctrl->sensor_i2c_client,
			sp0a20_recommend_settings,
			ARRAY_SIZE(sp0a20_recommend_settings),
			MSM_CAMERA_I2C_BYTE_DATA);
		break;
		
	case CFG_SET_RESOLUTION:
		break;
		
	case CFG_SET_STOP_STREAM:
		pr_err("%s, sensor stop stream!!", __func__);
		rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->
			i2c_write_conf_tbl(s_ctrl->sensor_i2c_client,
			sp0a20_stop_settings,
			ARRAY_SIZE(sp0a20_stop_settings),
			MSM_CAMERA_I2C_BYTE_DATA);
		break;
		
	case CFG_SET_START_STREAM:
		pr_err("%s, sensor start stream!!", __func__);
		rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->
			i2c_write_conf_tbl(s_ctrl->sensor_i2c_client,
			sp0a20_start_settings,
			ARRAY_SIZE(sp0a20_start_settings),
			MSM_CAMERA_I2C_BYTE_DATA);
		break;	
		
	case CFG_WRITE_I2C_ARRAY: {
		struct msm_camera_i2c_reg_setting conf_array;
		struct msm_camera_i2c_reg_array *reg_setting = NULL;

		if (s_ctrl->sensor_state != MSM_SENSOR_POWER_UP) {
			pr_err("%s:%d failed: invalid state %d\n", __func__,
				__LINE__, s_ctrl->sensor_state);
			rc = -EFAULT;
			break;
		}

		if (copy_from_user(&conf_array,
			(void *)cdata->cfg.setting,
			sizeof(struct msm_camera_i2c_reg_setting))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}

		if (!conf_array.size) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}

		reg_setting = kzalloc(conf_array.size *
			(sizeof(struct msm_camera_i2c_reg_array)), GFP_KERNEL);
		if (!reg_setting) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -ENOMEM;
			break;
		}
		if (copy_from_user(reg_setting, (void *)conf_array.reg_setting,
			conf_array.size *
			sizeof(struct msm_camera_i2c_reg_array))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			kfree(reg_setting);
			rc = -EFAULT;
			break;
		}

		conf_array.reg_setting = reg_setting;
		rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write_table(
			s_ctrl->sensor_i2c_client, &conf_array);
		kfree(reg_setting);
		break;
	}
	case CFG_SLAVE_READ_I2C: {
		struct msm_camera_i2c_read_config read_config;
		uint16_t local_data = 0;
		uint16_t orig_slave_addr = 0, read_slave_addr = 0;
		if (copy_from_user(&read_config,
			(void *)cdata->cfg.setting,
			sizeof(struct msm_camera_i2c_read_config))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}
		read_slave_addr = read_config.slave_addr;
		CDBG("%s:CFG_SLAVE_READ_I2C:", __func__);
		CDBG("%s:slave_addr=0x%x reg_addr=0x%x, data_type=%d\n",
			__func__, read_config.slave_addr,
			read_config.reg_addr, read_config.data_type);
		if (s_ctrl->sensor_i2c_client->cci_client) {
			orig_slave_addr =
				s_ctrl->sensor_i2c_client->cci_client->sid;
			s_ctrl->sensor_i2c_client->cci_client->sid =
				read_slave_addr >> 1;
		} else if (s_ctrl->sensor_i2c_client->client) {
			orig_slave_addr =
				s_ctrl->sensor_i2c_client->client->addr;
			s_ctrl->sensor_i2c_client->client->addr =
				read_slave_addr >> 1;
		} else {
			pr_err("%s: error: no i2c/cci client found.", __func__);
			rc = -EFAULT;
			break;
		}
		CDBG("%s:orig_slave_addr=0x%x, new_slave_addr=0x%x",
				__func__, orig_slave_addr,
				read_slave_addr >> 1);
		rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_read(
				s_ctrl->sensor_i2c_client,
				read_config.reg_addr,
				&local_data, read_config.data_type);
		if (rc < 0) {
			pr_err("%s:%d: i2c_read failed\n", __func__, __LINE__);
			break;
		}
		if (copy_to_user((void __user *)read_config.data,
			(void *)&local_data, sizeof(uint16_t))) {
			pr_err("%s:%d copy failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}
		break;
	}
	case CFG_SLAVE_WRITE_I2C_ARRAY: {
		struct msm_camera_i2c_array_write_config write_config;
		struct msm_camera_i2c_reg_array *reg_setting = NULL;
		uint16_t write_slave_addr = 0;
		uint16_t orig_slave_addr = 0;

		if (copy_from_user(&write_config,
			(void *)cdata->cfg.setting,
			sizeof(struct msm_camera_i2c_array_write_config))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}
		CDBG("%s:CFG_SLAVE_WRITE_I2C_ARRAY:", __func__);
		CDBG("%s:slave_addr=0x%x, array_size=%d\n", __func__,
			write_config.slave_addr,
			write_config.conf_array.size);

		if (!write_config.conf_array.size) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}
		reg_setting = kzalloc(write_config.conf_array.size *
			(sizeof(struct msm_camera_i2c_reg_array)), GFP_KERNEL);
		if (!reg_setting) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -ENOMEM;
			break;
		}
		if (copy_from_user(reg_setting,
				(void *)(write_config.conf_array.reg_setting),
				write_config.conf_array.size *
				sizeof(struct msm_camera_i2c_reg_array))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			kfree(reg_setting);
			rc = -EFAULT;
			break;
		}
		write_config.conf_array.reg_setting = reg_setting;
		write_slave_addr = write_config.slave_addr;
		if (s_ctrl->sensor_i2c_client->cci_client) {
			orig_slave_addr =
				s_ctrl->sensor_i2c_client->cci_client->sid;
			s_ctrl->sensor_i2c_client->cci_client->sid =
				write_slave_addr >> 1;
		} else if (s_ctrl->sensor_i2c_client->client) {
			orig_slave_addr =
				s_ctrl->sensor_i2c_client->client->addr;
			s_ctrl->sensor_i2c_client->client->addr =
				write_slave_addr >> 1;
		} else {
			pr_err("%s: error: no i2c/cci client found.", __func__);
			kfree(reg_setting);
			rc = -EFAULT;
			break;
		}
		CDBG("%s:orig_slave_addr=0x%x, new_slave_addr=0x%x",
				__func__, orig_slave_addr,
				write_slave_addr >> 1);
		rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write_table(
			s_ctrl->sensor_i2c_client, &(write_config.conf_array));
		if (s_ctrl->sensor_i2c_client->cci_client) {
			s_ctrl->sensor_i2c_client->cci_client->sid =
				orig_slave_addr;
		} else if (s_ctrl->sensor_i2c_client->client) {
			s_ctrl->sensor_i2c_client->client->addr =
				orig_slave_addr;
		} else {
			pr_err("%s: error: no i2c/cci client found.", __func__);
			kfree(reg_setting);
			rc = -EFAULT;
			break;
		}
		kfree(reg_setting);
		break;
	}
	case CFG_WRITE_I2C_SEQ_ARRAY: {
		struct msm_camera_i2c_seq_reg_setting conf_array;
		struct msm_camera_i2c_seq_reg_array *reg_setting = NULL;

		if (s_ctrl->sensor_state != MSM_SENSOR_POWER_UP) {
			pr_err("%s:%d failed: invalid state %d\n", __func__,
				__LINE__, s_ctrl->sensor_state);
			rc = -EFAULT;
			break;
		}

		if (copy_from_user(&conf_array,
			(void *)cdata->cfg.setting,
			sizeof(struct msm_camera_i2c_seq_reg_setting))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}

		if (!conf_array.size) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}
		reg_setting = kzalloc(conf_array.size *
			(sizeof(struct msm_camera_i2c_seq_reg_array)),
			GFP_KERNEL);
		if (!reg_setting) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -ENOMEM;
			break;
		}
		if (copy_from_user(reg_setting, (void *)conf_array.reg_setting,
			conf_array.size *
			sizeof(struct msm_camera_i2c_seq_reg_array))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			kfree(reg_setting);
			rc = -EFAULT;
			break;
		}

		conf_array.reg_setting = reg_setting;
		rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->
			i2c_write_seq_table(s_ctrl->sensor_i2c_client,
			&conf_array);
		kfree(reg_setting);
		break;
	}

	case CFG_POWER_UP:
		if (s_ctrl->sensor_state != MSM_SENSOR_POWER_DOWN) {
			pr_err("%s:%d failed: invalid state %d\n", __func__,
				__LINE__, s_ctrl->sensor_state);
			rc = -EFAULT;
			break;
		}
		if (s_ctrl->func_tbl->sensor_power_up) {
			if (s_ctrl->sensordata->misc_regulator)
				msm_sensor_misc_regulator(s_ctrl, 1);

			rc = s_ctrl->func_tbl->sensor_power_up(s_ctrl);
			if (rc < 0) {
				pr_err("%s:%d failed rc %ld\n", __func__,
					__LINE__, rc);
				break;
			}
			s_ctrl->sensor_state = MSM_SENSOR_POWER_UP;
			pr_err("%s:%d sensor state %d\n", __func__, __LINE__,
				s_ctrl->sensor_state);
		} else {
			rc = -EFAULT;
		}
		break;

	case CFG_POWER_DOWN:
		kfree(s_ctrl->stop_setting.reg_setting);
		s_ctrl->stop_setting.reg_setting = NULL;
		if (s_ctrl->sensor_state != MSM_SENSOR_POWER_UP) {
			pr_err("%s:%d failed: invalid state %d\n", __func__,
				__LINE__, s_ctrl->sensor_state);
			rc = -EFAULT;
			break;
		}
		if (s_ctrl->func_tbl->sensor_power_down) {
			if (s_ctrl->sensordata->misc_regulator)
				msm_sensor_misc_regulator(s_ctrl, 0);

			rc = s_ctrl->func_tbl->sensor_power_down(s_ctrl);
			if (rc < 0) {
				pr_err("%s:%d failed rc %ld\n", __func__,
					__LINE__, rc);
				break;
			}
			s_ctrl->sensor_state = MSM_SENSOR_POWER_DOWN;
			pr_err("%s:%d sensor state %d\n", __func__, __LINE__,
				s_ctrl->sensor_state);
		} else {
			rc = -EFAULT;
		}
		break;

	case CFG_SET_STOP_STREAM_SETTING: {
		struct msm_camera_i2c_reg_setting *stop_setting =
			&s_ctrl->stop_setting;
		struct msm_camera_i2c_reg_array *reg_setting = NULL;
		if (copy_from_user(stop_setting,
			(void *)cdata->cfg.setting,
			sizeof(struct msm_camera_i2c_reg_setting))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}

		reg_setting = stop_setting->reg_setting;

		if (!stop_setting->size) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}
		stop_setting->reg_setting = kzalloc(stop_setting->size *
			(sizeof(struct msm_camera_i2c_reg_array)), GFP_KERNEL);
		if (!stop_setting->reg_setting) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -ENOMEM;
			break;
		}
		if (copy_from_user(stop_setting->reg_setting,
			(void *)reg_setting,
			stop_setting->size *
			sizeof(struct msm_camera_i2c_reg_array))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			kfree(stop_setting->reg_setting);
			stop_setting->reg_setting = NULL;
			stop_setting->size = 0;
			rc = -EFAULT;
			break;
		}
		break;
	}
	default:
		rc = -EFAULT;
		break;
	}

	mutex_unlock(s_ctrl->msm_sensor_mutex);

	return rc;
}


int32_t sp0a20_match_id(struct msm_sensor_ctrl_t *s_ctrl)
{
	int32_t rc = 0;
	uint16_t chipid = 0;

	CDBG("%s,  E. calling i2c_read:, i2c_addr:0x%x, id_reg_addr:0x%x",
		__func__,
		s_ctrl->sensordata->slave_info->sensor_slave_addr,
		s_ctrl->sensordata->slave_info->sensor_id_reg_addr);

	rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_read(
			s_ctrl->sensor_i2c_client,
			0x02,
			&chipid, MSM_CAMERA_I2C_BYTE_DATA);
	if (rc < 0) {
		pr_err("%s: %s: read id failed\n", __func__,
			s_ctrl->sensordata->sensor_name);
		return rc;
	}
	CDBG("%s: read  id: %x ; expected id: 0x2b\n", __func__, chipid);
	if (chipid != 0x2b) {
		pr_err("msm_sensor_match_id chip id doesnot match \n");
		return -ENODEV;
	}

	return rc;
}


static struct msm_sensor_fn_t sp0a20_sensor_func_tbl = {
	.sensor_config = sp0a20_sensor_config,
	.sensor_power_up = msm_sensor_power_up,
	.sensor_power_down = msm_sensor_power_down,
	.sensor_match_id = sp0a20_match_id,
};

static struct msm_sensor_ctrl_t sp0a20_s_ctrl = {
	.sensor_i2c_client = &sp0a20_sensor_i2c_client,
	.power_setting_array.power_setting = sp0a20_power_setting,
	.power_setting_array.size = ARRAY_SIZE(sp0a20_power_setting),
	.msm_sensor_mutex = &sp0a20_mut,
	.sensor_v4l2_subdev_info = sp0a20_subdev_info,
	.sensor_v4l2_subdev_info_size = ARRAY_SIZE(sp0a20_subdev_info),
	.func_tbl = &sp0a20_sensor_func_tbl,
};

module_init(sp0a20_init_module);
module_exit(sp0a20_exit_module);
MODULE_DESCRIPTION("0.3MP YUV sensor driver");
MODULE_LICENSE("GPL v2");
