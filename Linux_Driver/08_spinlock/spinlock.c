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
	int dev_stats;
	spinlock_t lock;
};

static struct gpioled_dev gpioled;

/* Open function for the LED device */
static int led_open(struct inode *inode, struct file *filp)
{
	unsigned long flags;
	filp->private_data = &gpioled;

	spin_lock_irqsave(&gpioled.lock, flags);
	if (gpioled.dev_stats) {
		spin_unlock_irqrestore(&gpioled.lock, flags);
		return -EBUSY;
	}
	gpioled.dev_stats++;
	spin_unlock_irqrestore(&gpioled.lock, flags);

	return 0;
}

/* Read function for the LED device */
static ssize_t led_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
	return 0;  // No data to read
}

/* Write function for the LED device */
static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
	int retvalue;
	unsigned char databuf[1];
	unsigned char ledstat;
	struct gpioled_dev *dev = filp->private_data;

	retvalue = copy_from_user(databuf, buf, cnt);
	if (retvalue < 0) {
		printk("kernel write failed!\r\n");
		return -EFAULT;
	}

	ledstat = databuf[0];

	if (ledstat == LEDON) {	
		gpio_set_value(dev->led_gpio, 0);  // Turn on LED
	} else if (ledstat == LEDOFF) {
		gpio_set_value(dev->led_gpio, 1);  // Turn off LED
	}
	return 0;
}

/* Release function for the LED device */
static int led_release(struct inode *inode, struct file *filp)
{
	unsigned long flags;
	struct gpioled_dev *dev = filp->private_data;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->dev_stats) {
		dev->dev_stats--;
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	
	return 0;
}

/* File operations structure for the LED device */
static struct file_operations gpioled_fops = {
	.owner = THIS_MODULE,
	.open = led_open,
	.read = led_read,
	.write = led_write,
	.release = led_release,
};

/* Initialization function for the LED module */
static int __init led_init(void)
{
	int ret = 0;
	const char *str;

	spin_lock_init(&gpioled.lock);

	// Find the device node corresponding to /gpioled
	gpioled.nd = of_find_node_by_path("/gpioled");
	if (gpioled.nd == NULL) {
		printk("gpioled node not found!\r\n");
		return -EINVAL;
	}

	// Read the 'status' property of the device node
	ret = of_property_read_string(gpioled.nd, "status", &str);
	if (ret < 0) 
	    return -EINVAL;

	// Check if the status is "okay"
	if (strcmp(str, "okay"))
        return -EINVAL;
    
	// Read the 'compatible' property of the device node
	ret = of_property_read_string(gpioled.nd, "compatible", &str);
	if (ret < 0) {
		printk("gpioled: Failed to get compatible property\n");
		return -EINVAL;
	}

    // Check if the compatible string matches "alientek,led"
    if (strcmp(str, "alientek,led")) {
        printk("gpioled: Compatible match failed\n");
        return -EINVAL;
    }

	// Get the GPIO number from the 'led-gpio' property
	gpioled.led_gpio = of_get_named_gpio(gpioled.nd, "led-gpio", 0);
	if (gpioled.led_gpio < 0) {
		printk("can't get led-gpio");
		return -EINVAL;
	}
	printk("led-gpio number = %d\r\n", gpioled.led_gpio);

	// Request the GPIO pin for output
	ret = gpio_request(gpioled.led_gpio, "LED-GPIO");
    if (ret) {
        printk(KERN_ERR "gpioled: Failed to request led-gpio\n");
        return ret;
	}

	// Set the GPIO pin direction to output and initial value to high (LED off)
	ret = gpio_direction_output(gpioled.led_gpio, 1);
	if (ret < 0) {
		printk("can't set gpio!\r\n");
	}

	// Register the character device
	if (gpioled.major) {
		gpioled.devid = MKDEV(gpioled.major, 0);
		ret = register_chrdev_region(gpioled.devid, GPIOLED_CNT, GPIOLED_NAME);
		if (ret < 0) {
			pr_err("cannot register %s char driver [ret=%d]\n", GPIOLED_NAME, GPIOLED_CNT);
			goto free_gpio;
		}
	} else {
		ret = alloc_chrdev_region(&gpioled.devid, 0, GPIOLED_CNT, GPIOLED_NAME);
		if (ret < 0) {
			pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", GPIOLED_NAME, ret);
			goto free_gpio;
		}
		gpioled.major = MAJOR(gpioled.devid);
		gpioled.minor = MINOR(gpioled.devid);
	}
	printk("gpioled major=%d, minor=%d\r\n", gpioled.major, gpioled.minor);	
	
	// Initialize the cdev structure
	gpioled.cdev.owner = THIS_MODULE;
	cdev_init(&gpioled.cdev, &gpioled_fops);
	
	// Add the cdev structure to the kernel
	ret = cdev_add(&gpioled.cdev, gpioled.devid, GPIOLED_CNT);
	if (ret < 0)
		goto del_unregister;
		
	// Create a class for the device
	gpioled.class = class_create(THIS_MODULE, GPIOLED_NAME);
	if (IS_ERR(gpioled.class)) {
		goto del_cdev;
	}

	// Create a device node in /dev for the device
	gpioled.device = device_create(gpioled.class, NULL, gpioled.devid, NULL, GPIOLED_NAME);
	if (IS_ERR(gpioled.device)) {
		goto destroy_class;
	}
	return 0;

destroy_class:
	device_destroy(gpioled.class, gpioled.devid);
del_cdev:
	cdev_del(&gpioled.cdev);
del_unregister:
	unregister_chrdev_region(gpioled.devid, GPIOLED_CNT);
free_gpio:
	gpio_free(gpioled.led_gpio);
	return -EIO;
}

/* Exit function for the LED module */
static void __exit led_exit(void)
{
	// Remove the cdev structure from the kernel
	cdev_del(&gpioled.cdev);
	// Unregister the character device region
	unregister_chrdev_region(gpioled.devid, GPIOLED_CNT);
	// Destroy the device node in /dev
	device_destroy(gpioled.class, gpioled.devid);
	// Destroy the class created for the device
	class_destroy(gpioled.class);
	// Free the GPIO pin
	gpio_free(gpioled.led_gpio);
}

// Module initialization and exit macros
module_init(led_init);
module_exit(led_exit);

// Module license information
MODULE_LICENSE("GPL");
// Module author
MODULE_AUTHOR("JetWen");
// Module is in-tree
MODULE_INFO(intree, "Y");
