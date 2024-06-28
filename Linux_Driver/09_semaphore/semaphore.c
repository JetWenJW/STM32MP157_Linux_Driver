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

#define GPIOLED_CNT 1               /* Number of device numbers */
#define GPIOLED_NAME "gpioled"      /* Device name */
#define LEDOFF 0                    /* Turn off the LED */
#define LEDON 1                     /* Turn on the LED */

/* GPIO LED device structure */
struct gpioled_dev {
    dev_t devid;                    /* Device number */
    struct cdev cdev;               /* Character device structure */
    struct class *class;            /* Device class */
    struct device *device;          /* Device structure */
    int major;                      /* Major device number */
    int minor;                      /* Minor device number */
    struct device_node *nd;         /* Device node */
    int led_gpio;                   /* GPIO number used by the LED */
    struct semaphore sem;           /* Semaphore */
};

static struct gpioled_dev gpioled;  /* LED device */

/* Open device */
static int led_open(struct inode *inode, struct file *filp) {
    filp->private_data = &gpioled; /* Set private data */

    /* Acquire semaphore */
    if (down_interruptible(&gpioled.sem)) { /* Acquires the semaphore, process goes to sleep and can be interrupted by signals, count becomes 0 */
        return -ERESTARTSYS;
    }
    return 0;
}

/* Read data from the device */
static ssize_t led_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt) {
    return 0;
}

/* Write data to the device */
static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt) {
    int retvalue;
    unsigned char databuf[1];
    unsigned char ledstat;
    struct gpioled_dev *dev = filp->private_data;

    retvalue = copy_from_user(databuf, buf, cnt); /* Receive data sent from APP */
    if (retvalue < 0) {
        printk("kernel write failed!\r\n");
        return -EFAULT;
    }

    ledstat = databuf[0]; /* Get status value */

    if (ledstat == LEDON) {
        gpio_set_value(dev->led_gpio, 0); /* Turn on the LED */
    } else if (ledstat == LEDOFF) {
        gpio_set_value(dev->led_gpio, 1); /* Turn off the LED */
    }
    return 0;
}

/* Close/release device */
static int led_release(struct inode *inode, struct file *filp) {
    struct gpioled_dev *dev = filp->private_data;
    up(&dev->sem); /* Release semaphore, increase count value by 1 */
    return 0;
}

/* Device operation functions */
static struct file_operations gpioled_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .read = led_read,
    .write = led_write,
    .release = led_release,
};

/* Driver initialization function */
static int __init led_init(void) {
    int ret = 0;
    const char *str;

    /* Initialize semaphore */
    sema_init(&gpioled.sem, 1);

    /* Set GPIO used by the LED */
    /* 1. Get the device node: gpioled */
    gpioled.nd = of_find_node_by_path("/gpioled");
    if (gpioled.nd == NULL) {
        printk("gpioled node not find!\r\n");
        return -EINVAL;
    }

    /* 2. Read status property */
    ret = of_property_read_string(gpioled.nd, "status", &str);
    if (ret < 0)
        return -EINVAL;

    if (strcmp(str, "okay"))
        return -EINVAL;

    /* 3. Read compatible property and match */
    ret = of_property_read_string(gpioled.nd, "compatible", &str);
    if (ret < 0) {
        printk("gpioled: Failed to get compatible property\n");
        return -EINVAL;
    }

    if (strcmp(str, "alientek,led")) {
        printk("gpioled: Compatible match failed\n");
        return -EINVAL;
    }

    /* 4. Get GPIO property from device tree, get the GPIO number used by the LED */
    gpioled.led_gpio = of_get_named_gpio(gpioled.nd, "led-gpio", 0);
    if (gpioled.led_gpio < 0) {
        printk("can't get led-gpio");
        return -EINVAL;
    }
    printk("led-gpio num = %d\r\n", gpioled.led_gpio);

    /* 5. Request GPIO from gpio subsystem */
    ret = gpio_request(gpioled.led_gpio, "LED-GPIO");
    if (ret) {
        printk(KERN_ERR "gpioled: Failed to request led-gpio\n");
        return ret;
    }

    /* 6. Set GPIO direction to output and set high level, turn off LED by default */
    ret = gpio_direction_output(gpioled.led_gpio, 1);
    if (ret < 0) {
        printk("can't set gpio!\r\n");
    }

    /* Register character device driver */
    /* 1. Create device number */
    if (gpioled.major) { /* Device number is defined */
        gpioled.devid = MKDEV(gpioled.major, 0);
        ret = register_chrdev_region(gpioled.devid, GPIOLED_CNT, GPIOLED_NAME);
        if (ret < 0) {
            pr_err("cannot register %s char driver [ret=%d]\n", GPIOLED_NAME, GPIOLED_CNT);
            goto free_gpio;
        }
    } else { /* Device number is not defined */
        ret = alloc_chrdev_region(&gpioled.devid, 0, GPIOLED_CNT, GPIOLED_NAME); /* Allocate device number */
        if (ret < 0) {
            pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", GPIOLED_NAME, ret);
            goto free_gpio;
        }
        gpioled.major = MAJOR(gpioled.devid); /* Get major device number */
        gpioled.minor = MINOR(gpioled.devid); /* Get minor device number */
    }
    printk("gpioled major=%d,minor=%d\r\n", gpioled.major, gpioled.minor);

    /* 2. Initialize cdev */
    gpioled.cdev.owner = THIS_MODULE;
    cdev_init(&gpioled.cdev, &gpioled_fops);

    /* 3. Add cdev */
    cdev_add(&gpioled.cdev, gpioled.devid, GPIOLED_CNT);
    if (ret < 0)
        goto del_unregister;

    /* 4. Create class */
    gpioled.class = class_create(THIS_MODULE, GPIOLED_NAME);
    if (IS_ERR(gpioled.class)) {
        goto del_cdev;
    }

    /* 5. Create device */
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

/* Driver exit function */
static void __exit led_exit(void) {
    cdev_del(&gpioled.cdev); /* Delete cdev */
    unregister_chrdev_region(gpioled.devid, GPIOLED_CNT); /* Unregister device number */
    device_destroy(gpioled.class, gpioled.devid); /* Destroy device */
    class_destroy(gpioled.class); /* Destroy class */
    gpio_free(gpioled.led_gpio); /* Free GPIO */
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
