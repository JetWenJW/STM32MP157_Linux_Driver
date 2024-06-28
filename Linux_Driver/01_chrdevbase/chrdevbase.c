#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>

#define CHRDEVBASE_MAJOR 200
#define CHRDEVBASE_NAME "chrdevbase"

static char readbuf[100];
static char writebuf[100];
static char kerneldata[] = {"kernel data!"};

/*
 * Open function for the character device.
 */
static int chrdevbase_open(struct inode *inode, struct file *filp)
{
	return 0;
}

/*
 * Read function for the character device.
 */
static ssize_t chrdevbase_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
	int retvalue = 0;
	
	/* Copy kernel data to read buffer and transfer to user space */
	memcpy(readbuf, kerneldata, sizeof(kerneldata));
	retvalue = copy_to_user(buf, readbuf, cnt);
	if (retvalue == 0) {
		printk("kernel senddata ok!\n");
	} else {
		printk("kernel senddata failed!\n");
	}
	return 0;
}

/*
 * Write function for the character device.
 */
static ssize_t chrdevbase_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
	int retvalue = 0;
	
	/* Copy data from user space to write buffer and print it */
	retvalue = copy_from_user(writebuf, buf, cnt);
	if (retvalue == 0) {
		printk("kernel recevdata:%s\n", writebuf);
	} else {
		printk("kernel recevdata failed!\n");
	}
	return 0;
}

/*
 * Release function for the character device.
 */
static int chrdevbase_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/* File operations structure */
static struct file_operations chrdevbase_fops = {
	.owner = THIS_MODULE,
	.open = chrdevbase_open,
	.read = chrdevbase_read,
	.write = chrdevbase_write,
	.release = chrdevbase_release,
};

/*
 * Initialization function for the module.
 */
static int __init chrdevbase_init(void)
{
	int retvalue = 0;
	
	/* Register the character device driver */
	retvalue = register_chrdev(CHRDEVBASE_MAJOR, CHRDEVBASE_NAME, &chrdevbase_fops);
	if (retvalue < 0) {
		printk("chrdevbase driver register failed\n");
	}
	printk("chrdevbase init!\n");
	return 0;
}

/*
 * Exit function for the module.
 */
static void __exit chrdevbase_exit(void)
{
	/* Unregister the character device driver */
	unregister_chrdev(CHRDEVBASE_MAJOR, CHRDEVBASE_NAME);
	printk("chrdevbase exit!\n");
}

module_init(chrdevbase_init);
module_exit(chrdevbase_exit);
/* Module information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
