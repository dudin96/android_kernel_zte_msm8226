/***********************
 * file : tpd_fw.c
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include "tpd_fw.h"
//#include "tpd_common.h"


DECLARE_RWSEM(tp_firmware_list_lock);
LIST_HEAD(tp_firmware_list);

#define MAX_BUF_SIZE 256 * 1024

struct tpd_classdev_t tpd_fw_cdev;

static struct class *tsp_fw_class;
static struct tpd_classdev_t *g_cdev = NULL;
static unsigned char filepath[256];
static unsigned char tpinfopath[128];
static int g_force_upgrade = 0;
static struct mutex tpd_sys_cmd_mutex;

/*compare the touchpanel info to sdcard fw info start ,added by haozhijian20130404*/
static int compare_is_ok = 0;
static int tpd_free_buffer(struct tpd_classdev_t *cdev);
static int tpd_image_file_read(struct tpd_classdev_t *cdev, unsigned char * filepath);

static void tsp_get_fw_name(struct tpd_classdev_t *cdev, char *name)
{
	char fw_name[10] = "TPFW";
	
	TPD_DMESG("%s\n",__func__);
	
	if(NULL != strstr(cdev->tp_info.tp_name, "Synaptics")) {
		strcat(fw_name, "01");
	} else if (NULL != strstr(cdev->tp_info.tp_name, "Atmel")) {
		strcat(fw_name, "02");
	} else if (NULL != strstr(cdev->tp_info.tp_name, "Cyttsp")) {
		strcat(fw_name, "03");
	} else if (NULL != strstr(cdev->tp_info.tp_name, "Focal"))	{
		strcat(fw_name, "04");
	} else if (NULL != strstr(cdev->tp_info.tp_name, "Goodix")) {
		strcat(fw_name, "05");
	} else if (NULL != strstr(cdev->tp_info.tp_name, "Melfas")) {
		strcat(fw_name, "06");
	} else if (NULL != strstr(cdev->tp_info.tp_name, "Mstar")) {
		strcat(fw_name, "07");
	} else {
		strcat(fw_name, "08");
	}

	if(cdev->tp_info.vendor_id < 0xff) {
		snprintf(&fw_name[6], sizeof(fw_name)-6, "%02X", cdev->tp_info.vendor_id);
	} else {
		snprintf(&fw_name[6], sizeof(fw_name)-6, "%02X", 0xff);
	}

	strcpy(name, fw_name);
	TPD_DMESG("fun:%s fwname:%s.\n",__func__, name);
	
}

static void tsp_fw_compare_work(struct work_struct *work)
{
	static unsigned char fwpath[128];
	int fw_count = 0,i = 0,sta;
	char *path[] = {"/mnt/sdcard", "/mnt/sdcard2", "/system/usr"};
	struct tpd_classdev_t *cdev = container_of(work, struct tpd_classdev_t, check_fwinfo_work);
	char fwname[10];
	
	TPD_DMESG("%s \n", __func__);
	
	tsp_get_fw_name(cdev,fwname);
	memset(cdev->fw_info.compare,0,sizeof(cdev->fw_info.compare));
	strcpy(cdev->fw_info.compare,"true");//eq not upgrade
	
	TPD_DMESG("%s:image may in 0x%x dir.\n", __func__, sizeof(path));

	for(i = 0; i < 3; i++) {
		memset(fwpath, 0, sizeof(fwpath));
		sprintf(fwpath, "%s/%s.hex", path[i],fwname);
		tpd_free_buffer(cdev);		
		fw_count = tpd_image_file_read(cdev, fwpath);
		if(fw_count > 0) {			
			if(cdev == NULL) {
				TPD_DMESG("%s  g_client is null\n", __func__);
				strcpy(cdev->fw_info.compare,"true");
				break;
			} else {
				sta = cdev->compare_tp(cdev, cdev->tp_fw.data);
				memset(cdev->fw_info.compare,0,sizeof(cdev->fw_info.compare));
				if(0 == sta)
				strcpy(cdev->fw_info.compare,"false");//upgrade
				else
				strcpy(cdev->fw_info.compare,"true");//eq not upgrade
			}		
			break;
		} else
			continue;
	}
	compare_is_ok = 0;
	wake_up_interruptible(&cdev->wait);
}

static ssize_t tsp_fw_compare(char *string, int size)
{
	off_t fsize; 
	loff_t pos;
	mm_segment_t old_fs;
	struct file* fp = NULL;
	TPD_DMESG("%s \n", __func__ );
	fp= filp_open("/data/system/touchp", O_CREAT | O_RDWR | O_TRUNC, 0777);
	
		if(IS_ERR(fp)){
			TPD_DMESG("%s:error occured while opening file.\n", __func__);	
			return -1;
		}

		 fsize = size; 
		 old_fs = get_fs();
		 set_fs(KERNEL_DS);
		 pos = 0;
	
		 vfs_write(fp, string, fsize, &pos);
		 //vfs_read(fp, &temple, fsize, &pos);
		 TPD_DMESG("%s temple:%s\n", __func__,string);
		 filp_close(fp, NULL);
		set_fs(old_fs);	

	return 0;
}
static int tsp_fw_compare_info(struct tpd_classdev_t *cdev)
{
	strcpy(cdev->sd_info.tp_name, cdev->tp_info.tp_name);
	cdev->sd_info.chip_id = cdev->tp_info.chip_id;
	cdev->sd_info.vendor_id = cdev->tp_info.vendor_id;
	cdev->sd_info.chip_ver = cdev->tp_info.chip_ver;
	cdev->sd_info.firmware_ver = cdev->tp_info.firmware_ver;
	cdev->sd_info.i2c_type = cdev->tp_info.i2c_type;
	cdev->sd_info.i2c_addr = cdev->tp_info.i2c_addr;
	
	compare_is_ok = 1;
	schedule_work(&cdev->check_fwinfo_work);	
	wait_event_interruptible(cdev->wait, compare_is_ok != 1);
	compare_is_ok = 0;
	TPD_DMESG("%s, check result:%s \n", __func__, cdev->fw_info.compare);
	
	tsp_fw_compare((char *)&cdev->fw_info.compare, strlen(cdev->fw_info.compare));

	return 0;
}
/*compare the touchpanel info to sdcard fw info end,,added by haozhijian20130404 */

/*store the touchpanel info before upgrade start ,,added by haozhijian20130404*/

static int tpd_store_file_info(struct tpd_classdev_t *cdev)
{
	struct file* pfile = NULL;
	struct inode *inode;
	unsigned long magic; 
	char buffer[128];
	int len = 0;
	
	off_t fsize; 
	loff_t pos;
	int retval = 0;
	mm_segment_t old_fs;
	//char fw_name[20],tp_name[20];
	//tsp_get_fw_name(cdev,fw_name);
	//strcpy(tp_name,cdev->tp_info.tp_name);
	//strcpy(cdev->tp_info.tp_name,fw_name);
	memset(tpinfopath, 0, sizeof(tpinfopath));
	sprintf(tpinfopath, "/data/system/tpinfo");
	TPD_DMESG("%s file:%s\n", __func__, tpinfopath);
	
	if(NULL == pfile){
		pfile = filp_open(tpinfopath, O_CREAT | O_RDWR | O_TRUNC, 0777);
	}
	if(IS_ERR(pfile)){
		pr_err("%s:error occured while opening file %s.\n", __func__, tpinfopath);		
		retval = -1;
		goto out;
	}
	inode = pfile->f_dentry->d_inode; 
	 magic = inode->i_sb->s_magic;
	 fsize = sizeof(cdev->tp_info); 
	
	 old_fs = get_fs();
	 set_fs(KERNEL_DS);
	 pos = 0;
	
	len = sprintf(buffer, "%u 0x%x 0x%x 0x%x 0x%x 0x%x %s", cdev->tp_info.chip_id, 
		cdev->tp_info.vendor_id, cdev->tp_info.chip_ver, cdev->tp_info.firmware_ver, 
		cdev->tp_info.i2c_type, cdev->tp_info.i2c_addr, 
		cdev->tp_info.tp_name);

	vfs_write(pfile, buffer, len + 1, &pos);
	TPD_DMESG("%s fsize:%d,i2c_addr:0x%x\n", __func__, len, cdev->tp_info.i2c_addr);
	 filp_close(pfile, NULL);
	 set_fs(old_fs);
	 //strcpy(cdev->tp_info.tp_name,tp_name);
out:
	return 0;
}

static int tpd_read_file_info(struct tpd_classdev_t *cdev)
{
	struct file* pfile = NULL;
	struct inode *inode;
	unsigned long magic; 
	off_t fsize; 
	loff_t pos;
	int retval = 0;
	char buffer[128];
	mm_segment_t old_fs;

	memset(tpinfopath, 0, sizeof(tpinfopath));
	sprintf(tpinfopath, "/data/system/tpinfo");
	TPD_DMESG("%s file:%s\n", __func__, tpinfopath);
	
	if(NULL == pfile){
		pfile = filp_open(tpinfopath, O_RDONLY, 0);
	}
	if(IS_ERR(pfile)){
		pr_err("%s:error occured while opening file %s.\n", __func__, tpinfopath);		
		memset(tpinfopath, 0, sizeof(tpinfopath));		
		sprintf(tpinfopath, "/cache/tpinfo");
		pfile = filp_open(tpinfopath, O_RDONLY, 0);
		TPD_DMESG("%s file:%s\n", __func__, tpinfopath);
		if(IS_ERR(pfile)){
		retval = -1;
			pr_err("%s:error occured while opening file %s.\n", __func__, tpinfopath);
		goto out;
	}
	}
	TPD_DMESG("open file:%s ok\n", tpinfopath);
	inode = pfile->f_dentry->d_inode; 
	magic = inode->i_sb->s_magic;
	fsize = inode->i_size; 

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;
	TPD_DMESG("will read :%s ok\n", tpinfopath);
	//vfs_read(pfile, &cdev->tp_info, fsize, &pos);
	if(fsize < sizeof(buffer)){
		vfs_read(pfile, buffer, fsize, &pos);
		sscanf(buffer, "%u 0x%x 0x%x 0x%x 0x%x 0x%x %s\n", &cdev->tp_info.chip_id, 
			&cdev->tp_info.vendor_id, &cdev->tp_info.chip_ver, &cdev->tp_info.firmware_ver, 
			&cdev->tp_info.i2c_type, &cdev->tp_info.i2c_addr, 
			(char *)&cdev->tp_info.tp_name);
		//vfs_read(pfile, &cdev->tp_info, fsize, &pos);
	} else {
		TPD_DMESG("the buffer size is too small\n");
	}
	filp_close(pfile, NULL);
	set_fs(old_fs);
out:
	return retval;

}
/*store the touchpanel info before upgrade end,added by haozhijian20130404 */

static int tpd_flash_image(struct tpd_classdev_t *cdev, int cmd, int forced)
{
	int retval = 0;

	if(!mutex_trylock(&cdev->flash_mutex))
	{
		TPD_DMESG("%s: Pre func execing.\n", __func__);
		retval = -1;
		goto out;
	}
	
	if(cdev->flash_fw &&( (STATUS_BUF_RDY == cdev->status)||  \
		(STATUS_UPG_FAIL == cdev->status)||(STATUS_UPG_NONEED == cdev->status))) {
		if(NULL == cdev->tp_fw.data || 0 == cdev->tp_fw.size) {
			TPD_DMESG("BUFFER is NULL.\n");
			cdev->status = STATUS_UPG_FAIL;
			retval = -1;
			goto out;
		}
		
		retval = cdev->flash_fw(cdev, cdev->tp_fw.data, cdev->tp_fw.size, forced);
		if(retval == 0) {
			cdev->status = STATUS_UPG_SUCC;
		} else if (retval == 0xff) {
			cdev->status = STATUS_UPG_NONEED;
		} else {
			cdev->status = STATUS_UPG_FAIL;
		}
	} else {
		TPD_DMESG("%s: status:%d\n", __func__, cdev->status);
	}

	mutex_unlock(&cdev->flash_mutex);

out:
	return retval;
}

static int tpd_alloc_buffer(struct tpd_classdev_t *cdev, int alloc_size)
{
	if(cdev->tp_fw.data == NULL) {
		cdev->tp_fw.data = (unsigned char *)vmalloc(alloc_size);
		if(!cdev->tp_fw.data) {
			dev_err(cdev->dev, "memory alloc failed\n");
			goto error;
		}
		TPD_DMESG("%s malloc %d byte success.\n", __func__, alloc_size);
	}

	return 0;
error:
	return -1;
}

static int tpd_free_buffer(struct tpd_classdev_t *cdev)
{
	if(cdev->tp_fw.data != NULL) {
		vfree(cdev->tp_fw.data);
		cdev->tp_fw.size = 0;
		cdev->tp_fw.data = NULL;
		cdev->status = STATUS_OK;
	}

	return 0;
}

static int tpd_load_buffer(struct tpd_classdev_t *cdev, unsigned char *buffer, loff_t offset, size_t count)
{
	int i = 0;
	int retval = 0;
	int data_len = 0;

	TPD_DMESG("%s write %d byte, offset %lld byte.\n", __func__, count, offset);

	if(0 != tpd_alloc_buffer(cdev, MAX_BUF_SIZE)) {
		goto out;
	}

	if(offset == 0) {
		if(cdev->tp_fw.size + count > MAX_BUF_SIZE) {
			dev_err(cdev->dev, "[TSP]firmware size overflow\n");
			retval = 0;
			goto out;
		}
		
		memcpy(cdev->tp_fw.data + cdev->tp_fw.size, buffer, count);
		retval = count;
		cdev->status = STATUS_BUF_RDY;
		cdev->tp_fw.size = cdev->tp_fw.size + count;
	} else {
		if(cdev->tp_fw.size - offset >= count) {
			data_len = 0;
		} else {
			data_len = count - (cdev->tp_fw.size - offset);
		}

		if(cdev->tp_fw.size + data_len > MAX_BUF_SIZE) {
			dev_err(cdev->dev, "[TSP]firmware size overflow\n");
			retval = 0;
			goto out;
		}
		memcpy(cdev->tp_fw.data + offset, buffer, count);
		cdev->tp_fw.size = cdev->tp_fw.size + data_len;
		retval = count;
		cdev->status = STATUS_BUF_RDY;
	}

	TPD_DMESG("%s:", __func__);
	for(i = 0; i < 16; i++) {
		TPD_DMESG("0x%x ", cdev->tp_fw.data[i]);
	}
	TPD_DMESG("\n");

out:
	return retval;
}

static int tpd_image_file_read(struct tpd_classdev_t *cdev, unsigned char * filepath)
{
	struct file* pfile = NULL;
	struct inode *inode;
	unsigned long magic; 
	off_t fsize; 
	loff_t pos;
	int retval = 0;
	mm_segment_t old_fs;

	TPD_DMESG("%s file:%s\n", __func__, filepath);

	if(NULL == pfile){
		pfile = filp_open(filepath, O_RDONLY, 0);
	}
	if(IS_ERR(pfile)){
		pr_err("error occured while opening file %s.\n", filepath);
		retval = -1;
		goto out;
	}
	
	inode = pfile->f_dentry->d_inode; 
	magic = inode->i_sb->s_magic;
	fsize = inode->i_size; 

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;
	
	if(0 != tpd_alloc_buffer(cdev, fsize) || g_cdev == NULL) {
		TPD_DMESG("%s memory alloc failed\n", __func__);
		retval = -1;
		goto out;
	}
	vfs_read(pfile, cdev->tp_fw.data, fsize, &pos);

	filp_close(pfile, NULL);
	set_fs(old_fs);

	cdev->tp_fw.size = fsize;

	cdev->status = STATUS_BUF_RDY;
	retval = fsize;
	
out:
	return retval;
}

static void tpd_image_file_work(struct work_struct *work)
{
	int fw_count = 0;
	struct tpd_classdev_t *cdev = container_of(work, struct tpd_classdev_t, sdcard_upgrade_work);

	cdev->b_fwloader = 1;
		
	tpd_free_buffer(cdev);
	fw_count = tpd_image_file_read(cdev, filepath);
	if(fw_count > 0) {
		tpd_flash_image(cdev, cdev->cmd, g_force_upgrade);
	} else if(fw_count <= 0) {
		TPD_DMESG("%s: Read file failed.\n", __func__);
		cdev->status = STATUS_FILE_FAIL;
	}
	cdev->b_fwloader = 0;
	wake_up_interruptible(&cdev->wait);
}

static int tpd_sdcard_image_flash(struct tpd_classdev_t *cdev)
{
	char *path[] = {"/mnt/sdcard", "/mnt/sdcard2", "/system/usr"};
	char fwname[20];
	int i = 0;
	tsp_get_fw_name(cdev,fwname);

	TPD_DMESG("%s:image may in 0x%x dir.\n", __func__, sizeof(path));

	for(i = 0; i < 3; i++) {
		memset(filepath, 0, sizeof(filepath));
		sprintf(filepath, "%s/%s.hex", path[i],  fwname);
		cdev->b_fwloader = 1;
		schedule_work(&cdev->sdcard_upgrade_work);
		wait_event_interruptible(cdev->wait, cdev->b_fwloader != 1);
		if(cdev->status != STATUS_FILE_FAIL) {
			break;
		}
	}

	return 0;
}

static int tpd_cmd_exec(struct tpd_classdev_t *cdev, int cmd)
{
	int retval = 0;
	cdev->cmd = cmd;
	TPD_DMESG("%s cmd:%d\n", __func__, cmd);
	
	switch(cmd) {
	case CMD_WRITE_FW:
	case CMD_READ_FW:
	case CMD_WRITE_REG:
	case CMD_READ_REG:
	case CMD_CHECK_CHANNEL:
		cdev->status = STATUS_NULL;
		cdev->b_cmd_done = 0;
		break;
	case CMD_TP_CALIB:
		if(cdev->tpd_calib) {
			cdev->status = cdev->tpd_calib(cdev);
		} else {
			cdev->status = STATUS_OK;
		}
		break;
	case CMD_FW_UPG:
		tpd_flash_image(cdev, cmd, 0);
		break;
	case CMD_CLEAN_BUF:
		tpd_free_buffer(cdev);
		break;
	case CMD_SDCARD_UPG:
		cdev->status = STATUS_BUF_NULL;
		g_force_upgrade = 0;
		tpd_store_file_info(cdev);
		if(0 == cdev->b_fwloader) {
			cdev->b_fwloader = 1;
			tpd_sdcard_image_flash(cdev);
		}
		break;
	case CMD_FORCE_UPG:
		cdev->status = STATUS_BUF_NULL;
		g_force_upgrade = 1;
		tpd_read_file_info(cdev);
		if(0 == cdev->b_fwloader) {
			cdev->b_fwloader = 1;
			tpd_sdcard_image_flash(cdev);
		}
		break;
	case CMD_CHECK_FW: 
		tsp_fw_compare_info(cdev);
		break;
	default:
		TPD_DMESG("%s Not support this cmd:0x%x\n", __func__, cmd);
		break;
	}

	return retval;
}

static ssize_t tsp_fw_cmd_show(struct device *dev, 
		struct device_attribute *attr, char *buf)
{
	struct tpd_classdev_t *tsp_fw_cdev = dev_get_drvdata(dev);

	return sprintf(buf, "Current cmd is 0x%x.\n", tsp_fw_cdev->cmd);
}

static ssize_t tsp_fw_cmd_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct tpd_classdev_t *tsp_fw_cdev = dev_get_drvdata(dev);
	ssize_t ret = -EINVAL;
	char *after;
	unsigned long state = simple_strtoul(buf, &after, 10);
	size_t count = after - buf;

	if (isspace(*after))
		count++;

	if (count == size) {
		ret = count;
	}
	
	mutex_lock(&tpd_sys_cmd_mutex);
	tpd_cmd_exec(tsp_fw_cdev, state);
	mutex_unlock(&tpd_sys_cmd_mutex);

	return ret;
}

static ssize_t tsp_fw_tpinfo_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tpd_classdev_t *cdev = dev_get_drvdata(dev);

	return sprintf(buf, "%u 0x%x 0x%x 0x%x 0x%x 0x%x %s\n", cdev->tp_info.chip_id, 
		cdev->tp_info.vendor_id, cdev->tp_info.chip_ver, cdev->tp_info.firmware_ver, 
		cdev->tp_info.i2c_type, cdev->tp_info.i2c_addr, 
		cdev->tp_info.tp_name);
}

static ssize_t tsp_fw_upg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	//ruct tpd_classdev_t *cdev = dev_get_drvdata(dev);

	return sprintf(buf, "Current firmware file path:%s .\n", filepath);
}

static ssize_t tsp_fw_upg_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct tpd_classdev_t *cdev = dev_get_drvdata(dev);

	mutex_lock(&tpd_sys_cmd_mutex);
	g_force_upgrade = 1;
	memset(filepath, 0, sizeof(filepath));
	sprintf(filepath, "%s", (unsigned char*)buf);
	filepath[size - 1] = '\0';
	cdev->b_fwloader = 1;
	schedule_work(&cdev->sdcard_upgrade_work);
	wait_event_interruptible(cdev->wait, cdev->b_fwloader != 1);
	mutex_unlock(&tpd_sys_cmd_mutex);

	return size;
}

static ssize_t tsp_fw_check_show(struct device *dev, 
		struct device_attribute *attr, char *buf)
{
	struct tpd_classdev_t *tsp_fw_cdev = dev_get_drvdata(dev);
	int *pdata;

	pdata = (int*)tsp_fw_cdev->tp_fw.data;

	// buf: txnum rxnum type retval size
	if(tsp_fw_cdev->tp_fw.size > 5 * sizeof(int)) {
		return sprintf(buf, "%d %d %d %d %d\n", pdata[0], pdata[1], pdata[2], pdata[3], pdata[4]);
	} else {
		return sprintf(buf, "%d %d %d %d %d\n", 0, 0, 0, 0, 0);
	}
}

static ssize_t tsp_fw_check_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct tpd_classdev_t *tsp_fw_cdev = dev_get_drvdata(dev);
	ssize_t ret = -EINVAL;
	char *after;
	unsigned long state = simple_strtoul(buf, &after, 10);
	size_t count = after - buf;

	if (isspace(*after))
		count++;

	if (count == size) {
		ret = count;
	}
	mutex_lock(&tpd_sys_cmd_mutex);
	if(tsp_fw_cdev->check_tp) {
		tsp_fw_cdev->cmd = CMD_CHECK_CHANNEL;
		tsp_fw_cdev->status = STATUS_NULL;
		tpd_free_buffer(tsp_fw_cdev);
		tpd_alloc_buffer(tsp_fw_cdev, 4096);
		tsp_fw_cdev->tp_fw.size = tsp_fw_cdev->check_tp(state, tsp_fw_cdev->tp_fw.data, 4096);
		tsp_fw_cdev->status = STATUS_OK;
	}
	mutex_unlock(&tpd_sys_cmd_mutex);
	return ret;
}

static ssize_t tsp_fw_reg_show(struct device *dev, 
		struct device_attribute *attr, char *buf)
{
	struct tpd_classdev_t *cdev = dev_get_drvdata(dev);
	int retval = 0;
	u8 regval = 0;

	if(cdev->read_block) {
		cdev->read_block(cdev, 0x02, &regval , 1);
	}
	
	if(cdev->tp_fw.data == NULL)// //zhangzhao for cts test
			return -1;

	cdev->tp_fw.data[0] = regval & 0xFF;
	memcpy(buf, cdev->tp_fw.data, cdev->tp_fw.size);
	
	TPD_DMESG("%s read val:%d.\n", __func__, buf[0]);
	
	retval = cdev->tp_fw.size;

	return retval;
}

static ssize_t tsp_fw_reg_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct tpd_classdev_t *cdev = dev_get_drvdata(dev);
	TPD_DMESG("%s write %d byte.\n", __func__, size);

	mutex_lock(&tpd_sys_cmd_mutex);

	if(cdev->tp_fw.data == NULL)  //zhangzhao for cts test
		return -1;

	cdev->tp_fw.size = 0;
	tpd_load_buffer(cdev, (unsigned char *)&buf[0], 0, size);
	
	if(cdev->write_block) {
		cdev->write_block(cdev, 0x02, &cdev->tp_fw.data[0], 1);
	}
	TPD_DMESG("%s write val:%d.\n", __func__, cdev->tp_fw.data[0]);
	mutex_unlock(&tpd_sys_cmd_mutex);

	return size;
}

static ssize_t tsp_fw_sdinfo_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tpd_classdev_t *cdev = dev_get_drvdata(dev);

	return sprintf(buf, "%u 0x%x 0x%x 0x%x %s\n",cdev->sd_info.chip_id, cdev->sd_info.vendor_id, 
		  cdev->sd_info.chip_ver, cdev->sd_info.firmware_ver, cdev->sd_info.tp_name);
}

static struct device_attribute tsp_fw_class_attrs[] = {
	__ATTR(cmd, 0644, tsp_fw_cmd_show, tsp_fw_cmd_store),
	__ATTR(check, 0644, tsp_fw_check_show, tsp_fw_check_store),
	__ATTR(reg, 0644, tsp_fw_reg_show, tsp_fw_reg_store),
	__ATTR(tpinfo, 0644, tsp_fw_tpinfo_show, NULL),
	__ATTR(sdinfo, 0444, tsp_fw_sdinfo_show, NULL),
	__ATTR(upg, 0644, tsp_fw_upg_show, tsp_fw_upg_store),
	__ATTR_NULL,
};

static ssize_t tsp_fw_data_read(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buffer, loff_t offset, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct tpd_classdev_t *cdev = dev_get_drvdata(dev);
	int ret_count = 0;

	TPD_DMESG("%s: offset:%lld\n", __func__, offset);
	
	if((cdev->cmd == CMD_READ_FW || cdev->cmd == CMD_CHECK_CHANNEL
		|| cdev->cmd == CMD_WRITE_REG || cdev->cmd == CMD_READ_REG)
		&& cdev->b_cmd_done == 1) {
		if (offset > cdev->tp_fw.size) {
			ret_count = -EINVAL;
			goto out;
		}
		if(cdev->tp_fw.data) {	
			if (count > cdev->tp_fw.size - offset)
				count = cdev->tp_fw.size - offset;
			ret_count = count;
			memcpy(buffer, cdev->tp_fw.data + offset, count);
		} else {
			ret_count = 0;
			goto out;
		}
	} else {
		//TPD_DMESG("%s: cdev->status:%d\n", __func__, cdev->status);
		memcpy(buffer, &cdev->status, sizeof(int));
		ret_count = sizeof(int);
	}
	
	if(STATUS_OK == cdev->status) {
		cdev->b_cmd_done = 1;
	}

out:
	return ret_count;
}

static ssize_t tsp_fw_data_write(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *bin_attr,
				   char *buffer, loff_t offset, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct tpd_classdev_t *cdev = dev_get_drvdata(dev);
	int retval = 0;
	int *tp_data;
	u8 regval = 0;

	tp_data = (int *)buffer;

	if(cdev->cmd == CMD_READ_REG) {
		if(cdev->read_block) {
			retval = cdev->read_block(cdev, tp_data[0], &regval, 1);
			tp_data[0] = retval;
			tp_data[1] = regval;
		}

		retval = count;
	} else if(cdev->cmd == CMD_WRITE_REG) {
		if(cdev->write_block) {
			regval = tp_data[1];
			retval = cdev->write_block(cdev, tp_data[0], &regval, 1);
			tp_data[0] = retval;
			tp_data[1] = regval;
		}
		retval = count;
	} else {
		cdev->status = STATUS_BUF_NULL;
		retval = tpd_load_buffer(cdev, buffer, offset, count);
	}

	cdev->status = STATUS_OK;
	
	return retval;
}

static struct bin_attribute firmware_attr_data = {
	.attr = { .name = "data", .mode = 0644 },
	.size = 0,
	.read = tsp_fw_data_read,
	.write = tsp_fw_data_write,
};

/**
 * tpd_classdev_register - register a new object of tpd_classdev_t class.
 * @parent: The device to register.
 * @tsp_fw_cdev: the tpd_classdev_t structure for this device.
 */
int tpd_classdev_register(struct device *parent, struct tpd_classdev_t *tsp_fw_cdev)
{
	int error = 0;
	
	tsp_fw_cdev->dev = device_create(tsp_fw_class, parent, 0, tsp_fw_cdev,
					  "%s", tsp_fw_cdev->name);
	if (IS_ERR(tsp_fw_cdev->dev))
		return PTR_ERR(tsp_fw_cdev->dev);

	error = device_create_bin_file(tsp_fw_cdev->dev, &firmware_attr_data);
	if (error) {
		dev_err(tsp_fw_cdev->dev, "%s: sysfs_create_bin_file failed\n", __func__);
	}

	/* add to the list of tp_firmware */
	down_write(&tp_firmware_list_lock);
	list_add_tail(&tsp_fw_cdev->node, &tp_firmware_list);
	up_write(&tp_firmware_list_lock);

	g_cdev = tsp_fw_cdev;

	mutex_init(&tsp_fw_cdev->flash_mutex);
	mutex_init(&tpd_sys_cmd_mutex);
	init_waitqueue_head(&tsp_fw_cdev->wait);
	INIT_WORK(&tsp_fw_cdev->sdcard_upgrade_work, tpd_image_file_work);
		INIT_WORK(&tsp_fw_cdev->check_fwinfo_work,tsp_fw_compare_work);
	TPD_DMESG(KERN_DEBUG "Registered tsp_fw device: %s\n",
			tsp_fw_cdev->name);

	return 0;
}
EXPORT_SYMBOL_GPL(tpd_classdev_register);

/**
 * tpd_classdev_unregister - unregisters a object of tsp_fw_properties class.
 * @tsp_fw_cdev: the tsp_fw device to unregister
 *
 * Unregisters a previously registered via tpd_classdev_register object.
 */
void tpd_classdev_unregister(struct tpd_classdev_t *tsp_fw_cdev)
{
	device_unregister(tsp_fw_cdev->dev);

	down_write(&tp_firmware_list_lock);
	list_del(&tsp_fw_cdev->node);
	up_write(&tp_firmware_list_lock);
}
EXPORT_SYMBOL_GPL(tpd_classdev_unregister);

static int __init tpd_class_init(void)
{
	tsp_fw_class = class_create(THIS_MODULE, "tsp_fw");
	if (IS_ERR(tsp_fw_class))
		return PTR_ERR(tsp_fw_class);
	tsp_fw_class->dev_attrs = tsp_fw_class_attrs;

	tpd_fw_cdev.tp_fw.data = NULL;
	tpd_fw_cdev.tp_fw.size = 0;
	
	return 0;
}

static void __exit tpd_class_exit(void)
{
	tpd_free_buffer(&tpd_fw_cdev);
	class_destroy(tsp_fw_class);
}

subsys_initcall(tpd_class_init);
module_exit(tpd_class_exit);

MODULE_AUTHOR("John Lenz, Richard Purdie");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TSP FW Class Interface");


