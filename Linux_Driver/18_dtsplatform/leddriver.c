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
#include <linux/of_gpio.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/irq.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/platform_device.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define LEDDEV_CNT		1				/* Device number count */
#define LEDDEV_NAME		"dtsplatled"	/* Device name */
#define LEDOFF 			0
#define LEDON 			1

/* Device structure */
struct leddev_dev {
	dev_t devid;				/* Device number */
	struct cdev cdev;			/* Character device structure */
	struct class *class;		/* Device class */
	struct device *device;		/* Device */
	struct device_node *node;	/* LED device node */
	int gpio_led;				/* LED GPIO number */
};

struct leddev_dev leddev; 		/* LED device */

/*
 * @description		: LED on/off function
 * @param - sta 	: LEDON (1) to turn on LED, LEDOFF (0) to turn off LED
 * @return 			: None
 */
void led_switch(u8 sta)
{
	if (sta == LEDON)
		gpio_set_value(leddev.gpio_led, 0); /* Turn on LED */
	else if (sta == LEDOFF)
		gpio_set_value(leddev.gpio_led, 1); /* Turn off LED */
}

/* Initialize GPIO for LED */
static int led_gpio_init(struct device_node *nd)
{
	int ret;

	/* Retrieve GPIO from device tree */
	leddev.gpio_led = of_get_named_gpio(nd, "led-gpio", 0);
	if (!gpio_is_valid(leddev.gpio_led)) {
		printk(KERN_ERR "leddev: Failed to get led-gpio\n");
		return -EINVAL;
	}

	/* Request GPIO */
	ret = gpio_request(leddev.gpio_led, "LED0");
	if (ret) {
		printk(KERN_ERR "led: Failed to request led-gpio\n");
		return ret;
	}

	/* Set GPIO as output and initial level */
	gpio_direction_output(leddev.gpio_led, 1);

	return 0;
}

/*
 * @description		: Open device function
 * @param - inode 	: Pointer to inode structure
 * @param - filp 	: Pointer to file structure
 * @return 			: 0 on success, negative error code on failure
 */
static int led_open(struct inode *inode, struct file *filp)
{
	return 0;
}

/*
 * @description		: Write data to device function
 * @param - filp 	: Pointer to file structure
 * @param - buf 	: User buffer containing data to write
 * @param - cnt 	: Number of bytes to write
 * @param - offt 	: Offset from file start
 * @return 			: Number of bytes written, or negative error code on failure
 */
static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
	int retvalue;
	unsigned char databuf[1];
	unsigned char ledstat;

	retvalue = copy_from_user(databuf, buf, cnt);
	if (retvalue < 0) {
		printk("kernel write failed!\r\n");
		return -EFAULT;
	}

	ledstat = databuf[0];
	if (ledstat == LEDON) {
		led_switch(LEDON);
	} else if (ledstat == LEDOFF) {
		led_switch(LEDOFF);
	}
	return 0;
}

/* Device operations structure */
static struct file_operations led_fops = {
	.owner = THIS_MODULE,
	.open = led_open,
	.write = led_write,
};

/*
 * @description		: Probe function for platform driver
 * @param - pdev 	: Pointer to platform_device structure
 * @return 			: 0 on success, negative error code on failure
 */
static int led_probe(struct platform_device *pdev)
{
	int ret;

	printk("led driver and device was matched!\r\n");

	/* Initialize LED */
	ret = led_gpio_init(pdev->dev.of_node);
	if (ret < 0)
		return ret;

	/* 1. Allocate device number */
	ret = alloc_chrdev_region(&leddev.devid, 0, LEDDEV_CNT, LEDDEV_NAME);
	if (ret < 0) {
		pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", LEDDEV_NAME, ret);
		goto free_gpio;
	}

	/* 2. Initialize cdev */
	leddev.cdev.owner = THIS_MODULE;
	cdev_init(&leddev.cdev, &led_fops);

	/* 3. Add cdev */
	ret = cdev_add(&leddev.cdev, leddev.devid, LEDDEV_CNT);
	if (ret < 0)
		goto del_unregister;

	/* 4. Create class */
	leddev.class = class_create(THIS_MODULE, LEDDEV_NAME);
	if (IS_ERR(leddev.class)) {
		goto del_cdev;
	}

	/* 5. Create device */
	leddev.device = device_create(leddev.class, NULL, leddev.devid, NULL, LEDDEV_NAME);
	if (IS_ERR(leddev.device)) {
		goto destroy_class;
	}

	return 0;

destroy_class:
	class_destroy(leddev.class);
del_cdev:
	cdev_del(&leddev.cdev);
del_unregister:
	unregister_chrdev_region(leddev.devid, LEDDEV_CNT);
free_gpio:
	gpio_free(leddev.gpio_led);
	return -EIO;
}

/*
 * @description		: Remove function for platform driver
 * @param - dev 	: Pointer to platform_device structure
 * @return 			: 0 on success, negative error code on failure
 */
static int led_remove(struct platform_device *dev)
{
	gpio_set_value(leddev.gpio_led, 1); 	/* Turn off LED when unloading driver */
	gpio_free(leddev.gpio_led);			/* Release GPIO */
	cdev_del(&leddev.cdev);				/* Delete cdev */
	unregister_chrdev_region(leddev.devid, LEDDEV_CNT); /* Unregister device number */
	device_destroy(leddev.class, leddev.devid);	/* Destroy device */
	class_destroy(leddev.class); 		/* Destroy class */
	return 0;
}

/* Match table */
static const struct of_device_id led_of_match[] = {
	{ .compatible = "alientek,led" },
	{ /* Sentinel */ }
};

MODULE_DEVICE_TABLE(of, led_of_match);

/* Platform driver structure */
static struct platform_driver led_driver = {
	.driver		= {
		.name	= "stm32mp1-led",			/* Driver name for device matching */
		.of_match_table	= led_of_match, /* Device tree match table */
	},
	.probe		= led_probe,
	.remove		= led_remove,
};

/*
 * @description	: Module initialization function
 * @param 		: None
 * @return 		: None
 */
static int __init leddriver_init(void)
{
	return platform_driver_register(&led_driver);
}

/*
 * @description	: Module exit function
 * @param 		: None
 * @return 		: None
 */
static void __exit leddriver_exit(void)
{
	platform_driver_unregister(&led_driver);
}

module_init(leddriver_init);
module_exit(leddriver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
