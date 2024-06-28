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

#define GPIOLED_CNT		1
#define GPIOLED_NAME	"gpioled"
#define LEDOFF 			0
#define LEDON 			1

struct gpioled_dev {
	dev_t devid;
	struct cdev cdev;
	struct class *class;
	struct device *device;
	int major;
	int minor;
	struct device_node *nd;
	int led_gpio;
	atomic_t lock;
};

static struct gpioled_dev gpioled;

/*
 * @description	: Open device handler
 * @param - inode	: Inode structure
 * @param - filp	: File structure
 * @return			: 0 on success, -EBUSY if device is busy
 */
static int led_open(struct inode *inode, struct file *filp)
{
	if (!atomic_dec_and_test(&gpioled.lock)) {
		atomic_inc(&gpioled.lock);
		return -EBUSY;
	}
	
	filp->private_data = &gpioled;
	return 0;
}

/*
 * @description	: Read from device handler
 * @param - filp	: File structure
 * @param - buf		: Buffer to store data
 * @param - cnt		: Number of bytes to read
 * @param - offt	: Offset in file (unused)
 * @return			: Always returns 0
 */
static ssize_t led_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
	return 0;
}

/*
 * @description	: Write to device handler
 * @param - filp	: File structure
 * @param - buf		: Buffer containing data to write
 * @param - cnt		: Number of bytes to write
 * @param - offt	: Offset in file (unused)
 * @return			: Always returns 0
 */
static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
	int retvalue;
	unsigned char databuf[1];
	unsigned char ledstat;
	struct gpioled_dev *dev = filp->private_data;

	retvalue = copy_from_user(databuf, buf, cnt);
	if(retvalue < 0) {
		printk("kernel write failed!\r\n");
		return -EFAULT;
	}

	ledstat = databuf[0];

	if(ledstat == LEDON) {	
		gpio_set_value(dev->led_gpio, 0); // Turn on LED
	} else if(ledstat == LEDOFF) {
		gpio_set_value(dev->led_gpio, 1); // Turn off LED
	}
	return 0;
}

/*
 * @description	: Close/release device handler
 * @param - inode	: Inode structure
 * @param - filp	: File structure
 * @return			: Always returns 0
 */
static int led_release(struct inode *inode, struct file *filp)
{
	struct gpioled_dev *dev = filp->private_data;
	
	atomic_inc(&dev->lock); // Release the lock
	
	return 0;
}

/* File operations structure for the GPIO LED driver */
static struct file_operations gpioled_fops = {
	.owner = THIS_MODULE,
	.open = led_open,
	.read = led_read,
	.write = led_write,
	.release = led_release,
};

/*
 * @description	: Module initialization function
 * @return			: 0 on success, negative error code on failure
 */
static int __init led_init(void)
{
	int ret = 0;
	const char *str;

	gpioled.lock = (atomic_t)ATOMIC_INIT(0);
	atomic_set(&gpioled.lock, 1);

	gpioled.nd = of_find_node_by_path("/gpioled");
	if(gpioled.nd == NULL) {
		printk("gpioled node not find!\r\n");
		return -EINVAL;
	}

	ret = of_property_read_string(gpioled.nd, "status", &str);
	if(ret < 0) 
	    return -EINVAL;

	if (strcmp(str, "okay"))
        return -EINVAL;
    
	ret = of_property_read_string(gpioled.nd, "compatible", &str);
	if(ret < 0) {
		printk("gpioled: Failed to get compatible property\n");
		return -EINVAL;
	}

    if (strcmp(str, "alientek,led")) {
        printk("gpioled: Compatible match failed\n");
        return -EINVAL;
    }

	gpioled.led_gpio = of_get_named_gpio(gpioled.nd, "led-gpio", 0);
	if(gpioled.led_gpio < 0) {
		printk("can't get led-gpio");
		return -EINVAL;
	}
	printk("led-gpio num = %d\r\n", gpioled.led_gpio);

	ret = gpio_request(gpioled.led_gpio, "LED-GPIO");
    if (ret) {
        printk(KERN_ERR "gpioled: Failed to request led-gpio\n");
        return ret;
	}

	ret = gpio_direction_output(gpioled.led_gpio, 1);
	if(ret < 0) {
		printk("can't set gpio!\r\n");
	}

	if (gpioled.major) {
		gpioled.devid = MKDEV(gpioled.major, 0);
		ret = register_chrdev_region(gpioled.devid, GPIOLED_CNT, GPIOLED_NAME);
		if(ret < 0) {
			pr_err("cannot register %s char driver [ret=%d]\n", GPIOLED_NAME, GPIOLED_CNT);
			goto free_gpio;
		}
	} else {
		ret = alloc_chrdev_region(&gpioled.devid, 0, GPIOLED_CNT, GPIOLED_NAME);
		if(ret < 0) {
			pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", GPIOLED_NAME, ret);
			goto free_gpio;
		}
		gpioled.major = MAJOR(gpioled.devid);
		gpioled.minor = MINOR(gpioled.devid);
	}
	printk("gpioled major=%d,minor=%d\r\n",gpioled.major, gpioled.minor);	
	
	gpioled.cdev.owner = THIS_MODULE;
	cdev_init(&gpioled.cdev, &gpioled_fops);
	
	ret = cdev_add(&gpioled.cdev, gpioled.devid, GPIOLED_CNT);
	if(ret < 0)
		goto del_unregister;
		
	gpioled.class = class_create(THIS_MODULE, GPIOLED_NAME);
	if (IS_ERR(gpioled.class)) {
		goto del_cdev;
	}

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

/*
 * @description	: Module exit function
 * @return			: None
 */
static void __exit led_exit(void)
{
	cdev_del(&gpioled.cdev);
	unregister_chrdev_region(gpioled.devid, GPIOLED_CNT);
	device_destroy(gpioled.class, gpioled.devid);
	class_destroy(gpioled.class);
	gpio_free(gpioled.led_gpio);
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
