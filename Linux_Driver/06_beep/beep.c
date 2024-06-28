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

#define BEEP_CNT    1       /* Number of devices */
#define BEEP_NAME   "beep"  /* Device name */
#define BEEPOFF     0       /* Turn off the beep */
#define BEEPON      1       /* Turn on the beep */

/* Device structure */
struct beep_dev {
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *nd;
    int beep_gpio;
};

struct beep_dev beep;   /* Device instance */

/* Open function */
static int led_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &beep; /* Set private data */
    return 0;
}

/* Read function (not used in this driver) */
static ssize_t led_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
    return 0;
}

/* Write function */
static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
    int retvalue;
    unsigned char databuf[1];
    unsigned char ledstat;
    struct beep_dev *dev = filp->private_data;

    /* Copy data from user space */
    retvalue = copy_from_user(databuf, buf, cnt);
    if (retvalue < 0) {
        printk("kernel write failed!\r\n");
        return -EFAULT;
    }

    ledstat = databuf[0];   /* Get the status value */

    /* Control the beep according to the received status */
    if (ledstat == BEEPON) {
        gpio_set_value(dev->beep_gpio, 0);  /* Turn on the beep */
    } else if (ledstat == BEEPOFF) {
        gpio_set_value(dev->beep_gpio, 1);  /* Turn off the beep */
    }
    return 0;
}

/* Release function */
static int led_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/* File operations structure */
static struct file_operations beep_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .read = led_read,
    .write = led_write,
    .release = led_release,
};

/* Initialization function */
static int __init led_init(void)
{
    int ret = 0;
    const char *str;

    /* Retrieve the device node information */
    beep.nd = of_find_node_by_path("/beep");
    if (beep.nd == NULL) {
        printk("beep node not find!\r\n");
        return -EINVAL;
    }

    /* Read the 'status' property */
    ret = of_property_read_string(beep.nd, "status", &str);
    if (ret < 0)
        return -EINVAL;

    /* Check if the device is enabled */
    if (strcmp(str, "okay"))
        return -EINVAL;

    /* Read the 'compatible' property and match with expected value */
    ret = of_property_read_string(beep.nd, "compatible", &str);
    if (ret < 0) {
        printk("beep: Failed to get compatible property\n");
        return -EINVAL;
    }

    if (strcmp(str, "alientek,beep")) {
        printk("beep: Compatible match failed\n");
        return -EINVAL;
    }

    /* Get the GPIO number from device tree */
    beep.beep_gpio = of_get_named_gpio(beep.nd, "beep-gpio", 0);
    if (beep.beep_gpio < 0) {
        printk("can't get led-gpio");
        return -EINVAL;
    }
    printk("beep-gpio num = %d\r\n", beep.beep_gpio);

    /* Request and configure the GPIO */
    ret = gpio_request(beep.beep_gpio, "BEEP-GPIO");
    if (ret) {
        printk(KERN_ERR "beep: Failed to request beep-gpio\n");
        return ret;
    }

    /* Set the GPIO direction and initial value (off) */
    ret = gpio_direction_output(beep.beep_gpio, 1);
    if (ret < 0) {
        printk("can't set gpio!\r\n");
    }

    /* Register the character device */
    if (beep.major) {
        beep.devid = MKDEV(beep.major, 0);
        ret = register_chrdev_region(beep.devid, BEEP_CNT, BEEP_NAME);
        if (ret < 0) {
            pr_err("cannot register %s char driver [ret=%d]\n", BEEP_NAME, BEEP_CNT);
            goto free_gpio;
        }
    } else {
        ret = alloc_chrdev_region(&beep.devid, 0, BEEP_CNT, BEEP_NAME);
        if (ret < 0) {
            pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", BEEP_NAME, ret);
            goto free_gpio;
        }
        beep.major = MAJOR(beep.devid);
        beep.minor = MINOR(beep.devid);
    }
    printk("beep major=%d,minor=%d\r\n", beep.major, beep.minor);

    /* Initialize the cdev structure */
    beep.cdev.owner = THIS_MODULE;
    cdev_init(&beep.cdev, &beep_fops);

    /* Add the character device to the system */
    ret = cdev_add(&beep.cdev, beep.devid, BEEP_CNT);
    if (ret < 0)
        goto del_unregister;

    /* Create a device class */
    beep.class = class_create(THIS_MODULE, BEEP_NAME);
    if (IS_ERR(beep.class)) {
        goto del_cdev;
    }

    /* Create a device node */
    beep.device = device_create(beep.class, NULL, beep.devid, NULL, BEEP_NAME);
    if (IS_ERR(beep.device)) {
        goto destroy_class;
    }
    return 0;

destroy_class:
    class_destroy(beep.class);
del_cdev:
    cdev_del(&beep.cdev);
del_unregister:
    unregister_chrdev_region(beep.devid, BEEP_CNT);
free_gpio:
    gpio_free(beep.beep_gpio);
    return -EIO;
}

/* Exit function */
static void __exit led_exit(void)
{
    /* Unregister the character device */
    cdev_del(&beep.cdev);
    unregister_chrdev_region(beep.devid, BEEP_CNT);

    /* Destroy the device node */
    device_destroy(beep.class, beep.devid);

    /* Destroy the class */
    class_destroy(beep.class);

    /* Free the GPIO */
    gpio_free(beep.beep_gpio);
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
