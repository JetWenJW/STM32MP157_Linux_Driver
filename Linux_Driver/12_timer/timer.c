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
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define TIMER_CNT		1
#define TIMER_NAME		"timer"
#define CLOSE_CMD 		(_IO(0XEF, 0x1))
#define OPEN_CMD		(_IO(0XEF, 0x2))
#define SETPERIOD_CMD	(_IO(0XEF, 0x3))
#define LEDON 			1
#define LEDOFF 			0

struct timer_dev {
	dev_t devid;				// Device ID
	struct cdev cdev;			// Character device structure
	struct class *class;		// Device class
	struct device *device;		// Device structure
	int major;					// Major device number
	int minor;					// Minor device number
	struct device_node *nd;		// Device tree node
	int led_gpio;				// GPIO pin for LED
	int timeperiod;				// Timer period in milliseconds
	struct timer_list timer;		// Kernel timer structure
	spinlock_t lock;			// Spinlock for synchronization
};

struct timer_dev timerdev;		// Timer device instance

static int led_init(void)
{
	int ret;
	const char *str;

	// Find device node for "/gpioled" in device tree
	timerdev.nd = of_find_node_by_path("/gpioled");
	if (timerdev.nd == NULL) {
		printk("timerdev node not find!\r\n");
		return -EINVAL;
	}

	// Read "status" property from device tree
	ret = of_property_read_string(timerdev.nd, "status", &str);
	if (ret < 0)
		return -EINVAL;

	// Check if "status" is "okay"
	if (strcmp(str, "okay"))
		return -EINVAL;

	// Read "compatible" property from device tree
	ret = of_property_read_string(timerdev.nd, "compatible", &str);
	if (ret < 0) {
		printk("timerdev: Failed to get compatible property\n");
		return -EINVAL;
	}

	// Check if "compatible" matches "alientek,led"
	if (strcmp(str, "alientek,led")) {
		printk("timerdev: Compatible match failed\n");
		return -EINVAL;
	}

	// Get GPIO pin number from "led-gpio" property in device tree
	timerdev.led_gpio = of_get_named_gpio(timerdev.nd, "led-gpio", 0);
	if (timerdev.led_gpio < 0) {
		printk("can't get led-gpio");
		return -EINVAL;
	}
	printk("led-gpio num = %d\r\n", timerdev.led_gpio);

	// Request and configure GPIO pin for output
	ret = gpio_request(timerdev.led_gpio, "led");
	if (ret) {
		printk(KERN_ERR "timerdev: Failed to request led-gpio\n");
		return ret;
	}

	ret = gpio_direction_output(timerdev.led_gpio, 1);
	if (ret < 0) {
		printk("can't set gpio!\r\n");
		return ret;
	}
	return 0;
}

static int timer_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	filp->private_data = &timerdev;

	timerdev.timeperiod = 1000;  // Default timer period: 1 second
	ret = led_init();            // Initialize LED GPIO
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static long timer_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct timer_dev *dev =  (struct timer_dev *)filp->private_data;
	int timerperiod;
	unsigned long flags;
	
	switch (cmd) {
		case CLOSE_CMD:  // Close timer
			del_timer_sync(&dev->timer);
			break;
		case OPEN_CMD:   // Open timer
			spin_lock_irqsave(&dev->lock, flags);
			timerperiod = dev->timeperiod;
			spin_unlock_irqrestore(&dev->lock, flags);
			mod_timer(&dev->timer, jiffies + msecs_to_jiffies(timerperiod));
			break;
		case SETPERIOD_CMD:  // Set timer period
			spin_lock_irqsave(&dev->lock, flags);
			dev->timeperiod = arg;
			spin_unlock_irqrestore(&dev->lock, flags);
			mod_timer(&dev->timer, jiffies + msecs_to_jiffies(arg));
			break;
		default:
			break;
	}
	return 0;
}

static int led_release(struct inode *inode, struct file *filp)
{
	struct timer_dev *dev = filp->private_data;
	gpio_set_value(dev->led_gpio, 1);  // Turn off LED on release
	gpio_free(dev->led_gpio);          // Free GPIO pin
	del_timer_sync(&dev->timer);       // Delete timer

	return 0;
}

static struct file_operations timer_fops = {
	.owner = THIS_MODULE,
	.open = timer_open,
	.unlocked_ioctl = timer_unlocked_ioctl,
	.release = led_release,
};

void timer_function(struct timer_list *arg)
{
	struct timer_dev *dev = from_timer(dev, arg, timer);
	static int sta = 1;  // LED state
	int timerperiod;
	unsigned long flags;

	sta = !sta;  // Toggle LED state
	gpio_set_value(dev->led_gpio, sta);

	// Restart timer with current period
	spin_lock_irqsave(&dev->lock, flags);
	timerperiod = dev->timeperiod;
	spin_unlock_irqrestore(&dev->lock, flags);
	mod_timer(&dev->timer, jiffies + msecs_to_jiffies(dev->timeperiod));
}

static int __init timer_init(void)
{
	int ret;

	spin_lock_init(&timerdev.lock);  // Initialize spinlock for timer

	// Register character device driver
	if (timerdev.major) {
		timerdev.devid = MKDEV(timerdev.major, 0);
		ret = register_chrdev_region(timerdev.devid, TIMER_CNT, TIMER_NAME);
		if (ret < 0) {
			pr_err("cannot register %s char driver [ret=%d]\n", TIMER_NAME, TIMER_CNT);
			return -EIO;
		}
	} else {
		ret = alloc_chrdev_region(&timerdev.devid, 0, TIMER_CNT, TIMER_NAME);
		if (ret < 0) {
			pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", TIMER_NAME, ret);
			return -EIO;
		}
		timerdev.major = MAJOR(timerdev.devid);
		timerdev.minor = MINOR(timerdev.devid);
	}
	printk("timerdev major=%d, minor=%d\r\n", timerdev.major, timerdev.minor);

	timerdev.cdev.owner = THIS_MODULE;
	cdev_init(&timerdev.cdev, &timer_fops);

	// Add character device to system
	ret = cdev_add(&timerdev.cdev, timerdev.devid, TIMER_CNT);
	if (ret < 0)
		goto del_unregister;

	// Create device class
	timerdev.class = class_create(THIS_MODULE, TIMER_NAME);
	if (IS_ERR(timerdev.class)) {
		goto del_cdev;
	}

	// Create device
	timerdev.device = device_create(timerdev.class, NULL, timerdev.devid, NULL, TIMER_NAME);
	if (IS_ERR(timerdev.device)) {
		goto destroy_class;
	}

	// Initialize timer without starting it
	timer_setup(&timerdev.timer, timer_function, 0);

	return 0;

destroy_class:
	device_destroy(timerdev.class, timerdev.devid);
del_cdev:
	cdev_del(&timerdev.cdev);
del_unregister:
	unregister_chrdev_region(timerdev.devid, TIMER_CNT);
	return -EIO;
}

static void __exit timer_exit(void)
{
	del_timer_sync(&timerdev.timer);    // Delete timer
	cdev_del(&timerdev.cdev);           // Delete character device
	unregister_chrdev_region(timerdev.devid, TIMER_CNT);  // Unregister device number

	device_destroy(timerdev.class, timerdev.devid);  // Destroy device
	class_destroy(timerdev.class);       // Destroy device class
}

module_init(timer_init);  // Specify module initialization function
module_exit(timer_exit);  // Specify module exit function

MODULE_LICENSE("GPL");  // Specify module license
MODULE_AUTHOR("JetWen");  // Specify module author
MODULE_INFO(intree, "Y");  // Specify module is integrated into kernel source
