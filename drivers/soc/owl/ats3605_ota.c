#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/module.h> 
#include <asm/setup.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

static int g_ota_progress = 0;

static int ota_progress_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", g_ota_progress);  
	return 0;
}

static char *get_user_string(const char __user *user_buf, size_t user_len)
{
	char *buffer;

	buffer = vmalloc(user_len + 1);
	if (buffer == NULL)
		return ERR_PTR(-ENOMEM);
	if (copy_from_user(buffer, user_buf, user_len) != 0) {
		vfree(buffer);
		return ERR_PTR(-EFAULT);
	}
	/* got the string, now strip linefeed. */
	if (buffer[user_len - 1] == '\n')
		buffer[user_len - 1] = 0;
	else
		buffer[user_len] = 0;
	return buffer;
}


static ssize_t ota_progress_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	char *buf, *str;
	char *end_ptr;
    buf = get_user_string(buffer, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	str = skip_spaces(buf);
	if( str ) {
		g_ota_progress= simple_strtoul(str, &end_ptr, 0);
		printk("ota_progress=%d\n", g_ota_progress);
	}
	vfree(buf);
	return count;
}


static int ota_progress_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ota_progress_proc_show, NULL);
}

static const struct file_operations ota_progress_proc_fops = {
	.open		= ota_progress_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
    .write		= ota_progress_proc_write,
};

static int __init ota_progress_init(void)
{
	proc_create("ota_progress", S_IRUGO | S_IWUSR, NULL, &ota_progress_proc_fops);
	return 0;
}
module_init(ota_progress_init);


extern int read_mi_item(char *name, void *buf, unsigned int count);
extern int write_mi_item(char *name, void *buf, unsigned int count);

#define OTA_FLAG_NAME	 "OTA_FLG"

static int ota_flag_proc_show(struct seq_file *m, void *v)
{
	char buf[2];
	int str_len;
	str_len = read_mi_item(OTA_FLAG_NAME, buf, 1);
	if (str_len != 1) {
		printk("read OTA_FLAG failed, ret=%d\n", str_len);
		seq_printf(m, "0\n");
	} else {
		printk("read OTA_FLAG =%c\n", buf[0]);
		if (buf[0] == '1')
			seq_printf(m, "1\n");
		else
			seq_printf(m, "0\n");
	}
	return 0;
}

static ssize_t ota_flag_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	char *buf, *str, wbuf[2];
	int ret;
    buf = get_user_string(buffer, count);
	if (IS_ERR(buf)) {
		printk("ota_flag:get str fail\n");
		return PTR_ERR(buf);
	}
	str = skip_spaces(buf);
	if( str ) {
		if( str[0] == '1')
			wbuf[0] = '1';
		else
			wbuf[0] = '0';
		ret = write_mi_item(OTA_FLAG_NAME, wbuf, 1);
		printk("ota_flag:write val=%c, ret=%d\n", wbuf[0], ret);
	} else {
		return -1;
	}
	vfree(buf);
	return count;
}

static int ota_flag_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ota_flag_proc_show, NULL);
}

static const struct file_operations ota_flag_proc_fops = {
	.open		= ota_flag_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
    .write		= ota_flag_proc_write,
};

static int __init ota_flag_init(void)
{
	proc_create("ota_flag", S_IRUGO | S_IWUSR, NULL, &ota_flag_proc_fops);
	return 0;
}
module_init(ota_flag_init);
