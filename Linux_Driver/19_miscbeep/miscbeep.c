#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define MISCBEEP_NAME		"miscbeep"	/* Device name */
#define MISCBEEP_MINOR		144			/* Minor number */
#define BEEPOFF 			0			/* Turn off the beep */
#define BEEPON 				1			/* Turn on the beep */

/* miscbeep device structure */
struct miscbeep_dev {
	dev_t devid;			/* Device ID */
	struct cdev cdev;		/* Character device structure */
	struct class *class;	/* Device class */
	struct device *device;	/* Device */
	int beep_gpio;			/* GPIO number for the beep */
};

struct miscbeep_dev miscbeep;		/* beep device */

/*
 * @description		: Initialize beep GPIO
 * @param – pdev		: pointer to struct platform_device, the platform device pointer
 * @return			: 0 on success, negative value on failure
 */
static int beep_gpio_init(struct device_node *nd)
{
	int ret;
	
	/* Get GPIO from device tree */
	miscbeep.beep_gpio = of_get_named_gpio(nd, "beep-gpio", 0);
	if (!gpio_is_valid(miscbeep.beep_gpio)) {
		printk("miscbeep: Failed to get beep-gpio\n");
		return -EINVAL;
	}
	
	/* Request GPIO */
	ret = gpio_request(miscbeep.beep_gpio, "beep");
	if (ret) {
		printk("beep: Failed to request beep-gpio\n");
		return ret;
	}
	
	/* Set GPIO direction to output and initial state */
	gpio_direction_output(miscbeep.beep_gpio, 1);
	
	return 0;
}

/*
 * @description		: Open the device
 * @param - inode 	: pointer to the inode structure
 * @param - filp 	: file structure, typically sets private_data to point to device structure
 * @return 			: 0 on success, -1 on failure
 */
static int miscbeep_open(struct inode *inode, struct file *filp)
{
	return 0;
}

/*
 * @description		: Write data to the device
 * @param - filp 	: file structure, file descriptor opened for device
 * @param - buf 	: data to write to the device
 * @param - cnt 	: length of data to write
 * @param - offt 	: file offset
 * @return 			: number of bytes written, negative value on failure
 */
static ssize_t miscbeep_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
	int retvalue;
	unsigned char databuf[1];
	unsigned char beepstat;

	retvalue = copy_from_user(databuf, buf, cnt);
	if (retvalue < 0) {
		printk("kernel write failed!\r\n");
		return -EFAULT;
	}

	beepstat = databuf[0];		/* Get status value */
	if (beepstat == BEEPON) {	
		gpio_set_value(miscbeep.beep_gpio, 0);	/* Turn on the beep */
	} else if (beepstat == BEEPOFF) {
		gpio_set_value(miscbeep.beep_gpio, 1);	/* Turn off the beep */
	}
	return 0;
}

/* Device operations */
static struct file_operations miscbeep_fops = {
	.owner = THIS_MODULE,
	.open = miscbeep_open,
	.write = miscbeep_write,
};

/* MISC device structure */
static struct miscdevice beep_miscdev = {
	.minor = MISCBEEP_MINOR,
	.name = MISCBEEP_NAME,
	.fops = &miscbeep_fops,
};

 /*
  * @description     : Probe function for platform driver
  *                    executed when driver matches with device
  * @param - dev     : platform device
  * @return          : 0 on success, negative value on failure
  */
static int miscbeep_probe(struct platform_device *pdev)
{
	int ret = 0;

	printk("beep driver and device was matched!\r\n");

	/* Initialize BEEP */
	ret = beep_gpio_init(pdev->dev.of_node);
	if (ret < 0)
		return ret;
		
	/* Register MISC device */
	ret = misc_register(&beep_miscdev);
	if (ret < 0) {
		printk("misc device register failed!\r\n");
		goto free_gpio;
	}

	return 0;
	
free_gpio:
	gpio_free(miscbeep.beep_gpio);
	return -EINVAL;
}

/*
 * @description     : Remove function for platform driver
 *                    executed when platform driver is removed
 * @param - dev     : platform device
 * @return          : 0 on success, negative value on failure
 */
static int miscbeep_remove(struct platform_device *dev)
{
	/* Turn off beep when device is removed */
	gpio_set_value(miscbeep.beep_gpio, 1);
	
	/* Free GPIO */
	gpio_free(miscbeep.beep_gpio);

	/* Deregister MISC device */
	misc_deregister(&beep_miscdev);
	return 0;
}

 /* Match list */
 static const struct of_device_id beep_of_match[] = {
     { .compatible = "alientek,beep" },
     { /* Sentinel */ }
 };
 
 /* Platform driver structure */
static struct platform_driver beep_driver = {
     .driver     = {
         .name   = "stm32mp1-beep",         /* Driver name, used for device matching */
         .of_match_table = beep_of_match,    /* Device tree match table */
     },
     .probe      = miscbeep_probe,
     .remove     = miscbeep_remove,
};

/*
 * @description	: Driver initialization function
 * @param 		: none
 * @return 		: 0 on success, negative value on failure
 */
static int __init miscbeep_init(void)
{
	return platform_driver_register(&beep_driver);
}

/*
 * @description	: Driver exit function
 * @param 		: none
 * @return 		: none
 */
static void __exit miscbeep_exit(void)
{
	platform_driver_unregister(&beep_driver);
}

module_init(miscbeep_init);
module_exit(miscbeep_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
