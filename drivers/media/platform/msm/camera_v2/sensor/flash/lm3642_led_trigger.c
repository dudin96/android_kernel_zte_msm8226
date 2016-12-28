/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
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
 
#include <linux/module.h>
#include "msm_led_flash.h"
#include "msm_cci.h"
#include "msm_camera_io_util.h"

//#define CONFIG_FLASH_DEBUG
#ifdef CONFIG_FLASH_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif

#define ENABLE_REG 0xa
#define FLAGS_REG 0x0b
#define FLASH_FEATURES_REG 0x08
#define CURRENT_REG 0x09
#define TORCH_RT_REG 0x06
//#define lm3642_ID 0xf
#define lm3642_FLASH_STROBE 32
#define lm3642_FLASH_TORCH_EN 31

static int32_t lm3642_flash_trigger_get_subdev_id(struct msm_led_flash_ctrl_t *fctrl,
	void *arg)
{
	uint32_t *subdev_id = (uint32_t *)arg;
	if (!subdev_id) {
		pr_err("%s:%d failed\n", __func__, __LINE__);
		return -EINVAL;
	}
	*subdev_id = fctrl->pdev->id;
	CDBG("%s:%d subdev_id %d\n", __func__, __LINE__, *subdev_id);
	return 0;
}

static int lm3642_flash_trigger_power_up(struct msm_led_flash_ctrl_t *a_ctrl)
{
	int rc = 0;
	
	CDBG("%s:ENTRE\n",__func__);
	if((rc = gpio_request(lm3642_FLASH_TORCH_EN, "lm3642_torch_enable")) < 0) {
		CDBG("chengjiatest: request gpio lm3642_FLASH_TORCH_EN 31 error!\n");
		return rc;
	}	
 	gpio_direction_output(lm3642_FLASH_TORCH_EN,0);
	if((rc = gpio_request(lm3642_FLASH_STROBE, "lm3642_flash_strobe")) < 0) {
		CDBG("chengjiatest: request gpio lm3642_FLASH_STROBE 32  error!\n");
		return rc;
	}	
 	gpio_direction_output(lm3642_FLASH_STROBE, 0);	

	msleep(5);
	return rc;
}

static int lm3642_flash_trigger_power_down(struct msm_led_flash_ctrl_t *a_ctrl)
{
    int rc = 0;
    CDBG("%s:ENTRE\n",__func__);

    gpio_set_value(lm3642_FLASH_TORCH_EN, 0);
    gpio_set_value(lm3642_FLASH_STROBE, 0);
    gpio_free(lm3642_FLASH_TORCH_EN);
    gpio_free(lm3642_FLASH_STROBE);
    return rc;
}

static int lm3642_flash_trigger_config(struct msm_led_flash_ctrl_t *fctrl,
    void *data)
{
    int rc = 0;
    uint16_t temp = 0x0;
    struct msm_camera_led_cfg_t *cfg = (struct msm_camera_led_cfg_t *)data;
    CDBG("%s: called led_state %d\n", __func__,cfg->cfgtype);

    switch (cfg->cfgtype) {
    case MSM_CAMERA_LED_OFF:
        rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write(fctrl->flash_i2c_client, 
                ENABLE_REG, 0x00 , MSM_CAMERA_I2C_BYTE_DATA);
        break;
    case MSM_CAMERA_LED_LOW:
        rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write(fctrl->flash_i2c_client, 
                TORCH_RT_REG, 0x00 , MSM_CAMERA_I2C_BYTE_DATA);
        rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write(fctrl->flash_i2c_client, 
                CURRENT_REG, 0x1a , MSM_CAMERA_I2C_BYTE_DATA);
        rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write(fctrl->flash_i2c_client, 
                ENABLE_REG, 0x02 , MSM_CAMERA_I2C_BYTE_DATA);//0x12
        break;
    case MSM_CAMERA_LED_HIGH:
        rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write(fctrl->flash_i2c_client, 
                FLASH_FEATURES_REG, 0x17 , MSM_CAMERA_I2C_BYTE_DATA);
        rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write(fctrl->flash_i2c_client, 
                CURRENT_REG, 0x1a , MSM_CAMERA_I2C_BYTE_DATA);
        rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write(fctrl->flash_i2c_client, 
                ENABLE_REG, 0x03 , MSM_CAMERA_I2C_BYTE_DATA);//0x23
        break;
    case MSM_CAMERA_LED_INIT:
        lm3642_flash_trigger_power_up(fctrl);
        rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_read(fctrl->flash_i2c_client, 
                FLAGS_REG, &temp , MSM_CAMERA_I2C_BYTE_DATA);	 
        break;
    case MSM_CAMERA_LED_RELEASE:
        lm3642_flash_trigger_power_down(fctrl);
        break;
    default:
        rc = -EFAULT;
        break;
    }
    CDBG("flash_set_led_state: return %d\n", rc);
    return rc;
}

static struct msm_camera_i2c_fn_t msm_led_cci_func_tbl = {
	.i2c_read = msm_camera_cci_i2c_read,
	.i2c_read_seq = msm_camera_cci_i2c_read_seq,
	.i2c_write = msm_camera_cci_i2c_write,
	.i2c_write_table = msm_camera_cci_i2c_write_table,
	.i2c_write_seq_table = msm_camera_cci_i2c_write_seq_table,
	.i2c_write_table_w_microdelay =
		msm_camera_cci_i2c_write_table_w_microdelay,
	.i2c_util = msm_sensor_cci_i2c_util,
};



static const struct of_device_id lm3642_led_trigger_dt_match[] = {
	{.compatible = "qcom,lm3642", .data = NULL},   
	{}
};


static struct msm_flash_fn_t lm3642_flash_trigger_func_tbl = {
	.flash_get_subdev_id = lm3642_flash_trigger_get_subdev_id,
	.flash_led_config = lm3642_flash_trigger_config,
};

#define lm3642_TORCH_MODE 2
#define lm3642_FLASH_MODE 1

static int check_lm3642_device_id(struct msm_led_flash_ctrl_t *a_ctrl)
{
	int rc = 0;
	uint16_t current_setting = 0x80;

	lm3642_flash_trigger_power_up(a_ctrl);

	rc = a_ctrl->flash_i2c_client->i2c_func_tbl->i2c_util(
		a_ctrl->flash_i2c_client, MSM_CCI_INIT);
	if (rc < 0) {
		CDBG("chengjiatest: cci_init failed\n");
		goto check_error2;
	}
	rc = a_ctrl->flash_i2c_client->i2c_func_tbl->i2c_read(a_ctrl->flash_i2c_client, 
		CURRENT_REG, &current_setting, MSM_CAMERA_I2C_BYTE_DATA);
	if(rc < 0) {
		CDBG("%s: i2c read lm3642 error!\n",__func__);
		goto check_error1;
	}
	CDBG("%s: read from device lm3642 current set val is 0x%x\n",__func__, current_setting);

	rc = a_ctrl->flash_i2c_client->i2c_func_tbl->i2c_util(
		a_ctrl->flash_i2c_client, MSM_CCI_RELEASE);
	if (rc < 0) {
		CDBG("chengjiatest: cci_release failed\n");
	}

	lm3642_flash_trigger_power_down(a_ctrl);
	return 0;	
check_error1:
	rc = a_ctrl->flash_i2c_client->i2c_func_tbl->i2c_util(
		a_ctrl->flash_i2c_client, MSM_CCI_RELEASE);
check_error2:
	lm3642_flash_trigger_power_down(a_ctrl);
	return -1;
}


#define FASTMMI_TEST_FLASH_ON      1
#define FASTMMI_TEST_FLASH_OFF     0

unsigned long g_flag = 0xFF;

static ssize_t
show_led_status(struct device *dev, struct device_attribute *attr,
		char *buf)
{
    int ret = 0;

    sprintf(buf, "%ld\n", g_flag);
	ret = strlen(buf) + 1;

	return ret;
}

static ssize_t
write_led_status(struct device *dev, struct device_attribute *attr,
 const char *buf, size_t count)
{
	int rc = -1;
    uint16_t temp = 0x0;
	struct msm_led_flash_ctrl_t *fctrl = dev_get_drvdata(dev);
    rc = kstrtoul(buf, 10, &g_flag);
    if (rc)
		return rc;

    if (FASTMMI_TEST_FLASH_ON == g_flag) {
        // init
        lm3642_flash_trigger_power_up(fctrl);
        // init cci
        rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_util(
            fctrl->flash_i2c_client, MSM_CCI_INIT);
        if (rc < 0) {
    		return rc;
    	}
        rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_read(fctrl->flash_i2c_client, 
            FLAGS_REG, &temp , MSM_CAMERA_I2C_BYTE_DATA);

        // off
        rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write(fctrl->flash_i2c_client, 
    		       ENABLE_REG, 0x00 , MSM_CAMERA_I2C_BYTE_DATA);

        // low
    	//gpio_set_value(lm3642_FLASH_STROBE, 0);
        //usleep(2000);
        if (NULL != fctrl) {
            rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write(fctrl->flash_i2c_client, 
    		       TORCH_RT_REG, 0x00 , MSM_CAMERA_I2C_BYTE_DATA);	 
    		rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write(fctrl->flash_i2c_client, 
    		       CURRENT_REG, 0x1a , MSM_CAMERA_I2C_BYTE_DATA);
    		rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write(fctrl->flash_i2c_client, 
    		       ENABLE_REG, 0x02 , MSM_CAMERA_I2C_BYTE_DATA);//0x12
        }
    }else if (FASTMMI_TEST_FLASH_OFF == g_flag) {
        // off
        rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write(fctrl->flash_i2c_client, 
    	        ENABLE_REG, 0x00 , MSM_CAMERA_I2C_BYTE_DATA);

        // release cci
        rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_util(
                fctrl->flash_i2c_client, MSM_CCI_RELEASE);

        // release
        lm3642_flash_trigger_power_down(fctrl);
    }

	return count;
}

static DEVICE_ATTR(led_onoff, 0664, show_led_status, write_led_status);

static struct attribute *dev_attrs[] = {
	&dev_attr_led_onoff.attr,
	NULL,
};
static struct attribute_group dev_attr_grp = {
	.attrs = dev_attrs,
};


static int32_t lm3642_led_trigger_probe(struct platform_device *pdev)
{
	int32_t rc = 0;
	struct msm_camera_cci_client *cci_client = NULL;
	struct msm_led_flash_ctrl_t *s_ctrl;
	enum cci_i2c_master_t cci_i2c_master;
    int ret;

	CDBG("%s: ENTRE\n",__func__);
      	if (!pdev->dev.of_node) {		
	       pr_err("of_node NULL\n");		
		return -EINVAL;	
	}
	s_ctrl = kzalloc(sizeof(struct msm_led_flash_ctrl_t), GFP_KERNEL);
	if(!s_ctrl){
		pr_err("%s:%d failed no memory\n", __func__, __LINE__);
		return -ENOMEM;
	}
	       
	rc = of_property_read_u32((&pdev->dev)->of_node, "cell-index",
		&pdev->id);
	if (rc < 0) {
		kfree(s_ctrl);
		CDBG("failed rc %d\n", rc);
		return rc;
	}
	rc = of_property_read_u32((&pdev->dev)->of_node, "qcom,cci-master",
		&cci_i2c_master);
	if (rc < 0) {
		kfree(s_ctrl);
		CDBG("failed rc %d\n", rc);
		return rc;
	}
	s_ctrl->flash_i2c_client = kzalloc(sizeof(
		struct msm_camera_i2c_client), GFP_KERNEL);
	if (!s_ctrl->flash_i2c_client) {
		CDBG("failed no memory\n");
		kfree(s_ctrl);
		return -ENOMEM;
	}
	s_ctrl->flash_i2c_client->cci_client = kzalloc(sizeof(
		struct msm_camera_cci_client), GFP_KERNEL);
	if (!s_ctrl->flash_i2c_client->cci_client) {
		CDBG("failed no memory\n");
		kfree(s_ctrl->flash_i2c_client);
		kfree(s_ctrl);
		return -ENOMEM;
	}
	
	cci_client = s_ctrl->flash_i2c_client->cci_client;
	cci_client->cci_subdev = msm_cci_get_subdev();
	cci_client->cci_i2c_master = cci_i2c_master;
	cci_client->sid = 0x63;//no  need to <<1
	cci_client->retries = 3;
	cci_client->id_map = 0;
	s_ctrl->pdev = pdev;
 	s_ctrl->flash_i2c_client->i2c_func_tbl = &msm_led_cci_func_tbl;
	s_ctrl->flash_i2c_client->addr_type = MSM_CAMERA_I2C_BYTE_ADDR; 
       s_ctrl->func_tbl = &lm3642_flash_trigger_func_tbl;

	if((rc = check_lm3642_device_id(s_ctrl)) != 0) {
		kfree(s_ctrl->flash_i2c_client->cci_client);
		kfree(s_ctrl->flash_i2c_client);
		kfree(s_ctrl);
		return rc;
	}
	rc = msm_led_flash_create_v4lsubdev(pdev, s_ctrl);

    CDBG("wangjunfeng pdev->name=%s   %s\n",pdev->name,pdev->dev.kobj.name);
    ret = sysfs_create_group(&pdev->dev.kobj, &dev_attr_grp);
    if (ret)
        pr_err("%s: Failed to create sysfs node: %d\n", __func__, ret);

    dev_set_drvdata(&pdev->dev, s_ctrl);
    CDBG("wangjunfeng s_ctrl = %d\n", (int)s_ctrl);

	return rc;
}

MODULE_DEVICE_TABLE(of, msm_led_trigger_dt_match);

static struct platform_driver lm3642_led_trigger_driver = {
	.driver = {
		.name = "qcom,lm3642",
		.owner = THIS_MODULE,
		.of_match_table = lm3642_led_trigger_dt_match,
	},
};

static int __init lm3642_led_trigger_add_driver(void)
{
	return platform_driver_probe(&lm3642_led_trigger_driver,
		lm3642_led_trigger_probe);
}

module_init(lm3642_led_trigger_add_driver);
MODULE_DESCRIPTION("LED TRIGGER FLASH");
MODULE_LICENSE("GPL v2");
