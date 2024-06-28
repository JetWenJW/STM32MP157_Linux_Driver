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
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define GPIOLED_CNT 1          // Number of device numbers
#define GPIOLED_NAME "gpioled" // Name
#define LEDOFF 0               // Turn off LED
#define LEDON 1                // Turn on LED

// gpioled device structure
struct gpioled_dev {
    dev_t devid;            // Device number
    struct cdev cdev;       // cdev
    struct class *class;    // Class
    struct device *device;  // Device
    int major;              // Major device number
    int minor;              // Minor device number
    struct device_node *nd; // Device node
    int led_gpio;           // GPIO number used by the LED
    struct mutex lock;      // Mutex
};

static struct gpioled_dev gpioled; // LED device

// Open device
static int led_open(struct inode *inode, struct file *filp) {
    filp->private_data = &gpioled; // Set private data

    // Acquire mutex, can be interrupted by signals
    if (mutex_lock_interruptible(&gpioled.lock)) {
        return -ERESTARTSYS;
    }
#if 0
    mutex_lock(&gpioled.lock); // Cannot be interrupted by signals
#endif

    return 0;
}

// Read data from device
static ssize_t led_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt) {
    return 0;
}

// Write data to device
static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt) {
    int retvalue;
    unsigned char databuf[1];
    unsigned char ledstat;
    struct gpioled_dev *dev = filp->private_data;

    // Receive data sent by APP
    retvalue = copy_from_user(databuf, buf, cnt);
    if (retvalue < 0) {
        printk("kernel write failed!\r\n");
        return -EFAULT;
    }

    ledstat = databuf[0]; // Get status value

    if (ledstat == LEDON) {
        gpio_set_value(dev->led_gpio, 0); // Turn on LED
    } else if (ledstat == LEDOFF) {
        gpio_set_value(dev->led_gpio, 1); // Turn off LED
    }
    return 0;
}

// Close/release device
static int led_release(struct inode *inode, struct file *filp) {
    struct gpioled_dev *dev = filp->private_data;

    // Release mutex
    mutex_unlock(&dev->lock);
    return 0;
}

// Device operation functions
static struct file_operations gpioled_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .read = led_read,
    .write = led_write,
    .release = led_release,
};

// Driver initialization function
static int __init led_init(void) {
    int ret = 0;
    const char *str;

    // Initialize mutex
    mutex_init(&gpioled.lock);

    // Set GPIO used by LED
    gpioled.nd = of_find_node_by_path("/gpioled");
    if (gpioled.nd == NULL) {
        printk("gpioled node not find!\r\n");
        return -EINVAL;
    }

    // Read status property
    ret = of_property_read_string(gpioled.nd, "status", &str);
    if (ret < 0) return -EINVAL;

    if (strcmp(str, "okay")) return -EINVAL;

    // Read compatible property value and match
    ret = of_property_read_string(gpioled.nd, "compatible", &str);
    if (ret < 0) {
        printk("gpioled: Failed to get compatible property\n");
        return -EINVAL;
    }

    if (strcmp(str, "alientek,led")) {
        printk("gpioled: Compatible match failed\n");
        return -EINVAL;
    }

    // Get the GPIO number used by the LED from the device tree
    gpioled.led_gpio = of_get_named_gpio(gpioled.nd, "led-gpio", 0);
    if (gpioled.led_gpio < 0) {
        printk("can't get led-gpio");
        return -EINVAL;
    }
    printk("led-gpio num = %d\r\n", gpioled.led_gpio);

    // Request GPIO from the GPIO subsystem
    ret = gpio_request(gpioled.led_gpio, "LED-GPIO");
    if (ret) {
        printk(KERN_ERR "gpioled: Failed to request led-gpio\n");
        return ret;
    }

    // Set GPIO to output and default to high level (turn off LED)
    ret = gpio_direction_output(gpioled.led_gpio, 1);
    if (ret < 0) {
        printk("can't set gpio!\r\n");
    }

    // Register character device driver
    if (gpioled.major) { // Device number defined
        gpioled.devid = MKDEV(gpioled.major, 0);
        ret = register_chrdev_region(gpioled.devid, GPIOLED_CNT, GPIOLED_NAME);
        if (ret < 0) {
            pr_err("cannot register %s char driver [ret=%d]\n", GPIOLED_NAME, GPIOLED_CNT);
            goto free_gpio;
        }
    } else { // Device number not defined
        ret = alloc_chrdev_region(&gpioled.devid, 0, GPIOLED_CNT, GPIOLED_NAME); // Request device number
        if (ret < 0) {
            pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", GPIOLED_NAME, ret);
            goto free_gpio;
        }
        gpioled.major = MAJOR(gpioled.devid); // Get major device number
        gpioled.minor = MINOR(gpioled.devid); // Get minor device number
    }
    printk("gpioled major=%d,minor=%d\r\n", gpioled.major, gpioled.minor);

    // Initialize cdev
    gpioled.cdev.owner = THIS_MODULE;
    cdev_init(&gpioled.cdev, &gpioled_fops);

    // Add cdev
    cdev_add(&gpioled.cdev, gpioled.devid, GPIOLED_CNT);
    if (ret < 0)
        goto del_unregister;

    // Create class
    gpioled.class = class_create(THIS_MODULE, GPIOLED_NAME);
    if (IS_ERR(gpioled.class)) {
        goto del_cdev;
    }

    // Create device
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

// Driver exit function
static void __exit led_exit(void) {
    // Unregister character device driver
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
