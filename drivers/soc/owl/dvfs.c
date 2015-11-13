#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h> 
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/compat.h>

#if 1
#define DVFS_PRINT(fmt, args...)\
	printk(fmt, ##args)
#else
#define DVFS_PRINT(fmt, args...)
#endif

#define DEVDRV_NAME_OWL_CHIPID	"dvfs"
#define TRANS_INPUT_KEY_MAX 40

#define OWL_CHIPID 0xc3000000
#define OWL_TRANS  0xc3000001

typedef struct{
	unsigned char *keybox;
	int keybox_len;
	unsigned char *cert;
	int cert_len;
} dvfs_trans_t;


typedef struct {
	dvfs_trans_t *dvfs;
	int ret;
} dvfs_trans_ret;

typedef struct{
	unsigned int keybox;
	int keybox_len;
	unsigned int cert;
	int cert_len;
} dvfs_trans_t32;

#define TRANS_INPUT_KEY_MAX 40
#define TRANS_SCHEME_INFO_MAX 8
#define TRANS_DVFS_INFO_MAX 16


#define __asmeq(x, y)  ".ifnc " x "," y " ; .err ; .endif\n\t"

static	unsigned char keybox[TRANS_INPUT_KEY_MAX];
static unsigned char cert[32+4];


noinline u64 _invoke_fn_smc(u64 function_id, u64 arg0, u64 arg1,
				u64 arg2)
{
	__asm__ __volatile__(
		__asmeq("%0", "x0")
		__asmeq("%1", "x1")
		__asmeq("%2", "x2")
		__asmeq("%3", "x3")
		"smc    #0\n"
		: "+r" (function_id)
		: "r" (arg0), "r" (arg1), "r" (arg2));

	return function_id;
}


static void __read_chipid(unsigned int *dvfs_val)
{
	*dvfs_val = (int)_invoke_fn_smc(OWL_CHIPID, 0, 0, 0);
	printk("__read_chipid dvfs_val = 0x%x\n", *dvfs_val);
}

static void trans(dvfs_trans_ret *arg)
{
	unsigned long ptr;

	ptr = __pa(arg->dvfs);

	arg->ret = _invoke_fn_smc(OWL_TRANS, ptr, 0, 0);
}

static int cpu0_read_chipid(unsigned int *dvfs_val)
{
	int ret;

	ret = smp_call_function_single(0, (smp_call_func_t)__read_chipid, (void *)dvfs_val, 1);
	if (ret < 0) {
		printk("smp_call_function_single error %d\n ", ret);
		return ret;
	}
	printk("cpu0_read_chipid dvfs_val = 0x%x\n", *dvfs_val);

	return 0;
}

static int cpu0_trans(dvfs_trans_t *arg)
{
	int ret;
	dvfs_trans_ret dvfs_ret;

	dvfs_ret.dvfs = arg;
	ret = smp_call_function_single(0, (smp_call_func_t)trans, (void *)&dvfs_ret, 1);
	if (ret < 0) {
		printk("smp_call_function_single error %d\n ", ret);
		return ret;
	}

	return dvfs_ret.ret;
}


static long owl_chipid_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int val = 0, len;
	int ret = 0;
	dvfs_trans_t temp;
	int keybox_len;

	switch (cmd) {
	case 0xddff0001:
		ret = cpu0_read_chipid(&val);
		if (ret < 0) {
			printk("read chipid error\n");
			return -EFAULT;
		}
		ret = copy_to_user((void __user *)arg, &val, sizeof(int));
		if (ret) {
			DVFS_PRINT("copy_ret:%d\n", ret);
			return -EFAULT;
		}
		break;
	case 0xddff0002:
		ret = copy_from_user(&temp, (void *)arg, sizeof(dvfs_trans_t));
		if (ret) {
			DVFS_PRINT("---copy_from_user err,copy_ret:0x%x---\n", ret);
			return -EFAULT;
		}

		if (temp.keybox_len > TRANS_INPUT_KEY_MAX) {
			DVFS_PRINT("---keybox_len too long---\n");
			return -EINVAL;
		}

		keybox_len = temp.keybox_len;
		printk("key box len = 0x%x\n", keybox_len);
		ret = copy_from_user(keybox, (void __user *)temp.keybox, keybox_len);
		if (ret) {
			DVFS_PRINT("---copy_from_user err,copy_ret:0x%x---\n", ret);
			return -EFAULT;
		}

		temp.keybox = (unsigned long)__pa(keybox);
		temp.cert = (unsigned long)__pa(cert);
		ret = cpu0_trans(&temp);
		if (ret < 0) {
			printk("cert error\n");
			return ret;
		}
		temp.cert = cert;

		((dvfs_trans_t *)arg)->cert_len = temp.cert_len;
		ret = copy_to_user(((dvfs_trans_t *)arg)->cert, temp.cert, temp.cert_len);
		if (ret) {
			DVFS_PRINT("---copy_to_user err---\n");
			return -EFAULT;
		}
		break;

	default:
		return -EIO;
	}

	return 0;
}



static int owl_chipid_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int owl_chipid_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int compat_get_dvfs_trans(
		dvfs_trans_t32 __user *dvfs32,
		dvfs_trans_t __user *dvfs)
{
	compat_int_t i;
	compat_ulong_t ul;
	int err;

	err = get_user(ul, &dvfs32->keybox);
	err |= put_user(ul, &dvfs->keybox);
	err |= get_user(i, &dvfs32->keybox_len);
	err |= put_user(i, &dvfs->keybox_len);
	err |= get_user(ul, &dvfs32->cert);
	err |= put_user(ul, &dvfs->cert);
	err |= get_user(i, &dvfs32->cert_len);
	err |= put_user(i, &dvfs->cert_len);

	return err;
}

static int compat_put_dvfs_trans(
		dvfs_trans_t32 __user *dvfs32,
		dvfs_trans_t __user *dvfs)
{
	compat_int_t i;
	compat_ulong_t ul;
	int err;

	err = get_user(ul, &dvfs->keybox);
	err |= put_user(ul, &dvfs32->keybox);
	err |= get_user(i, &dvfs->keybox_len);
	err |= put_user(i, &dvfs32->keybox_len);
	err |= get_user(ul, &dvfs->cert);
	err |= put_user(ul, &dvfs32->cert);
	err |= get_user(i, &dvfs->cert_len);
	err |= put_user(i, &dvfs32->cert_len);

    return err;
}

static long owl_chipid_ioctl_compat(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret;
	dvfs_trans_t32 __user *dvfs32;
	dvfs_trans_t *dvfs;
	int err;

	switch (cmd) {
	case 0xddff0001:
		ret = owl_chipid_ioctl(file, cmd,
		(unsigned long) compat_ptr(arg));
		return ret;
	case 0xddff0002:
		dvfs32 = compat_ptr(arg);
		dvfs = compat_alloc_user_space(sizeof(*dvfs));
		if (dvfs == NULL)
			return -EFAULT;

		err = compat_get_dvfs_trans(dvfs32, dvfs);
		if (err)
		return err;

		ret = owl_chipid_ioctl(file, cmd, dvfs);

		err = compat_put_dvfs_trans(dvfs32, dvfs);

		return ret ? ret : err;
	default:
		return -ENOIOCTLCMD;
	}
}


static const struct file_operations owl_chipid_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = owl_chipid_ioctl,
	.compat_ioctl = owl_chipid_ioctl_compat,
	.open = owl_chipid_open,
	.release = owl_chipid_release,
};

static struct miscdevice owl_chipid_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVDRV_NAME_OWL_CHIPID,
	.fops = &owl_chipid_fops,
};


static int owl_chipid_init(void)
{
	int ret;

	DVFS_PRINT("%s\n", __FUNCTION__);

	ret = misc_register(&owl_chipid_miscdevice);
	if (ret) {
		DVFS_PRINT("register owl_chipid misc device failed!\n");
		goto err;
	}
	return 0;
err:
	return ret;
}

static void owl_chipid_exit(void)
{
	DVFS_PRINT("%s\n", __FUNCTION__);
	misc_deregister(&owl_chipid_miscdevice);
}

module_init(owl_chipid_init);
module_exit(owl_chipid_exit);

MODULE_AUTHOR("Actions Semi Inc");
MODULE_DESCRIPTION("owl_chipid kernel module");
MODULE_LICENSE("GPL");
