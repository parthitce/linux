
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/module.h> 
#include <asm/setup.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static int g_dvfslevel = -1;
static char gbootdev[16];
static char gserial_no[20];


int owl_get_dvfslevel(void)
{
    return g_dvfslevel;
}
EXPORT_SYMBOL_GPL(owl_get_dvfslevel);
static int __init dvfs_setup(char *str)
{
	g_dvfslevel = simple_strtoul(str, NULL, 0);
	return 1;
}

early_param("dvfslevel", dvfs_setup);

static int __init bootdev_setup(char *str)
{
	strncpy(gbootdev, str, 15);
	return 1;
}

early_param("bootdev", bootdev_setup);

static int __init serialno_setup(char *str)
{
	strncpy(gserial_no, str, 19);
	gserial_no[19] = 0;
	return 1;
}

early_param("androidboot.serialno", serialno_setup);

static int dvfslevel_proc_show(struct seq_file *m, void *v)
{
	if ( g_dvfslevel != -1 )
		seq_printf(m, "0x%x\n", g_dvfslevel);
	else
		seq_printf(m, "unkown\n");    
	return 0;
}

static int dvfslevel_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dvfslevel_proc_show, NULL);
}

static const struct file_operations dvfslevel_proc_fops = {
	.open		= dvfslevel_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_dvfslevel_init(void)
{
	proc_create("dvfslevel", 0, NULL, &dvfslevel_proc_fops);
	return 0;
}

static int bootdev_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", gbootdev); 
	return 0;
}

static int bootdev_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, bootdev_proc_show, NULL);
}

static const struct file_operations bootdev_proc_fops = {
	.open		= bootdev_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_bootdev_init(void)
{
	proc_create("bootdev", 0, NULL, &bootdev_proc_fops);
	return 0;
}


static int serialno_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", gserial_no); 
	return 0;
}

static int serialno_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, serialno_proc_show, NULL);
}

static const struct file_operations serialno_proc_fops = {
	.open		= serialno_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_serialno_init(void)
{
	proc_create("serialno", 0, NULL, &serialno_proc_fops);
	return 0;
}

module_init(proc_dvfslevel_init);
module_init(proc_bootdev_init);
module_init(proc_serialno_init);

MODULE_AUTHOR("Actions Semi Inc");
MODULE_DESCRIPTION("owl_chipid kernel module");
MODULE_LICENSE("GPL");
