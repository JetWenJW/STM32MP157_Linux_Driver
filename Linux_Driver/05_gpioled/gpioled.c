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
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define GPIOLED_CNT			1
#define GPIOLED_NAME		"gpioled"
#define LEDOFF 				0
#define LEDON 				1

struct gpioled_dev {
	dev_t devid;
	struct cdev cdev;
	struct class *class;
	struct device *device;
	int major;
	int minor;
	struct device_node *nd;
	int led_gpio;
};

struct gpioled_dev gpioled;

static int led_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &gpioled; /* Set private data of the file to gpioled structure */
	return 0;
}

static ssize_t led_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
	return 0; /* Reading from device not supported, return 0 */
}

static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
	int retvalue;
	unsigned char databuf[1];
	unsigned char ledstat;
	struct gpioled_dev *dev = filp->private_data;

	retvalue = copy_from_user(databuf, buf, cnt); /* Copy data from user space */
	if (retvalue < 0) {
		printk("kernel write failed!\r\n");
		return -EFAULT;
	}

	ledstat = databuf[0]; /* Get the LED status from user */

	if (ledstat == LEDON) {
		gpio_set_value(dev->led_gpio, 0); /* Turn on LED */
	} else if (ledstat == LEDOFF) {
		gpio_set_value(dev->led_gpio, 1); /* Turn off LED */
	}
	return 0; /* Return success */
}

static int led_release(struct inode *inode, struct file *filp)
{
	return 0; /* Release function (close operation), return success */
}

static struct file_operations gpioled_fops = {
	.owner = THIS_MODULE,
	.open = led_open,
	.read = led_read,
	.write = led_write,
	.release = led_release,
};

static int __init led_init(void)
{
	int ret = 0;
	const char *str;

	gpioled.nd = of_find_node_by_path("/gpioled"); /* Find device node in device tree */
	if (gpioled.nd == NULL) {
		printk("gpioled node not find!\r\n");
		return -EINVAL;
	}

	ret = of_property_read_string(gpioled.nd, "status", &str); /* Read 'status' property */
	if (ret < 0) 
	    return -EINVAL;

	if (strcmp(str, "okay"))
        return -EINVAL;
    
	ret = of_property_read_string(gpioled.nd, "compatible", &str); /* Read 'compatible' property */
	if (ret < 0) {
		printk("gpioled: Failed to get compatible property\n");
		return -EINVAL;
	}

    if (strcmp(str, "alientek,led")) {
        printk("gpioled: Compatible match failed\n");
        return -EINVAL;
    }

	gpioled.led_gpio = of_get_named_gpio(gpioled.nd, "led-gpio", 0); /* Get GPIO number */
	if (gpioled.led_gpio < 0) {
		printk("can't get led-gpio");
		return -EINVAL;
	}
	printk("led-gpio num = %d\r\n", gpioled.led_gpio);

	ret = gpio_request(gpioled.led_gpio, "LED-GPIO"); /* Request GPIO for LED */
    if (ret) {
        printk(KERN_ERR "gpioled: Failed to request led-gpio\n");
        return ret;
	}

	ret = gpio_direction_output(gpioled.led_gpio, 1); /* Set GPIO as output */
	if (ret < 0) {
		printk("can't set gpio!\r\n");
	}

	if (gpioled.major) {
		gpioled.devid = MKDEV(gpioled.major, 0);
		ret = register_chrdev_region(gpioled.devid, GPIOLED_CNT, GPIOLED_NAME); /* Register character device */
		if (ret < 0) {
			pr_err("cannot register %s char driver [ret=%d]\n", GPIOLED_NAME, GPIOLED_CNT);
			goto free_gpio;
		}
	} else {
		ret = alloc_chrdev_region(&gpioled.devid, 0, GPIOLED_CNT, GPIOLED_NAME); /* Allocate character device numbers */
		if (ret < 0) {
			pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", GPIOLED_NAME, ret);
			goto free_gpio;
		}
		gpioled.major = MAJOR(gpioled.devid);
		gpioled.minor = MINOR(gpioled.devid);
	}
	printk("gpioled major=%d,minor=%d\r\n", gpioled.major, gpioled.minor);

	gpioled.cdev.owner = THIS_MODULE;
	cdev_init(&gpioled.cdev, &gpioled_fops); /* Initialize cdev structure */

	cdev_add(&gpioled.cdev, gpioled.devid, GPIOLED_CNT); /* Add cdev to the system */
	if (ret < 0)
		goto del_unregister;

	gpioled.class = class_create(THIS_MODULE, GPIOLED_NAME); /* Create device class */
	if (IS_ERR(gpioled.class)) {
		goto del_cdev;
	}

	gpioled.device = device_create(gpioled.class, NULL, gpioled.devid, NULL, GPIOLED_NAME); /* Create device */
	if (IS_ERR(gpioled.device)) {
		goto destroy_class;
	}
	return 0; /* Initialization successful, return */

destroy_class:
	class_destroy(gpioled.class);
del_cdev:
	cdev_del(&gpioled.cdev);
del_unregister:
	unregister_chrdev_region(gpioled.devid, GPIOLED_CNT);
free_gpio:
	gpio_free(gpioled.led_gpio);
	return -EIO; /* Error handling */
}

static void __exit led_exit(void)
{
	cdev_del(&gpioled.cdev); /* Delete cdev */
	unregister_chrdev_region(gpioled.devid, GPIOLED_CNT); /* Unregister device numbers */
	device_destroy(gpioled.class, gpioled.devid); /* Destroy device */
	class_destroy(gpioled.class); /* Destroy class */
	gpio_free(gpioled.led_gpio); /* Free GPIO */
}

module_init(led_init); /* Specify module entry point */
module_exit(led_exit); /* Specify module exit point */
MODULE_LICENSE("GPL"); /* License for the module */
MODULE_AUTHOR("JetWen"); /* Author information */
MODULE_INFO(intree, "Y"); /* This module is built into the kernel tree */
