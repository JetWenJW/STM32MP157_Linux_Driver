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

#define KEY_CNT 1    // Number of devices
#define KEY_NAME "key"  // Device name

// Define key values
#define KEY0VALUE 0XF0  // Key value when pressed
#define INVAKEY 0X00    // Invalid key value

// Key device structure
struct key_dev {
    dev_t devid;          // Device number
    struct cdev cdev;     // Character device structure
    struct class *class;  // Device class
    struct device *device; // Device structure
    int major;            // Major device number
    int minor;            // Minor device number
    struct device_node *nd; // Device node
    int key_gpio;         // GPIO number for the key
    atomic_t keyvalue;    // Key value
};

static struct key_dev keydev;  // Key device

/*
 * @description  : Initialize the key IO. This function is called when the driver is opened.
 * @param        : None
 * @return       : 0 on success; other values on failure
 */
static int keyio_init(void) {
    int ret;
    const char *str;

    // Get the device node: keydev
    keydev.nd = of_find_node_by_path("/key");
    if (keydev.nd == NULL) {
        printk("keydev node not found!\r\n");
        return -EINVAL;
    }

    // Read the status property
    ret = of_property_read_string(keydev.nd, "status", &str);
    if (ret < 0) 
        return -EINVAL;

    if (strcmp(str, "okay"))
        return -EINVAL;
    
    // Get and match the compatible property value
    ret = of_property_read_string(keydev.nd, "compatible", &str);
    if (ret < 0) {
        printk("keydev: Failed to get compatible property\n");
        return -EINVAL;
    }

    if (strcmp(str, "alientek,key")) {
        printk("keydev: Compatible match failed\n");
        return -EINVAL;
    }

    // Get the GPIO number used by KEY0 from the device tree
    keydev.key_gpio = of_get_named_gpio(keydev.nd, "key-gpio", 0);
    if (keydev.key_gpio < 0) {
        printk("can't get key-gpio");
        return -EINVAL;
    }
    printk("key-gpio num = %d\r\n", keydev.key_gpio);

    // Request the GPIO
    ret = gpio_request(keydev.key_gpio, "KEY0");
    if (ret) {
        printk(KERN_ERR "keydev: Failed to request key-gpio\n");
        return ret;
    }

    // Set the GPIO direction to input
    ret = gpio_direction_input(keydev.key_gpio);
    if (ret < 0) {
        printk("can't set gpio!\r\n");
        return ret;
    }
    return 0;
}

/*
 * @description  : Open the device
 * @param - inode: Inode passed to the driver
 * @param - filp : Device file, file structure has a member variable called private_data
 *                 Usually, we set private_data to point to the device structure during open.
 * @return       : 0 on success; other values on failure
 */
static int key_open(struct inode *inode, struct file *filp) {
    int ret = 0;
    filp->private_data = &keydev;  // Set private data

    ret = keyio_init();  // Initialize key IO
    if (ret < 0) {
        return ret;
    }

    return 0;
}

/*
 * @description  : Read data from the device
 * @param - filp : Device file (file descriptor)
 * @param - buf  : Data buffer returned to user space
 * @param - cnt  : Number of bytes to read
 * @param - offt : Offset relative to the file's starting address
 * @return       : Number of bytes read, negative value indicates read failure
 */
static ssize_t key_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt) {
    int ret = 0;
    int value;
    struct key_dev *dev = filp->private_data;

    if (gpio_get_value(dev->key_gpio) == 0) {  // Key0 pressed
        while(!gpio_get_value(dev->key_gpio)); // Wait for key release
        atomic_set(&dev->keyvalue, KEY0VALUE);  
    } else {    
        atomic_set(&dev->keyvalue, INVAKEY);  // Invalid key value
    }

    value = atomic_read(&dev->keyvalue);
    ret = copy_to_user(buf, &value, sizeof(value));
    return ret;
}

/*
 * @description  : Write data to the device
 * @param - filp : Device file, file descriptor
 * @param - buf  : Data to write to the device
 * @param - cnt  : Number of bytes to write
 * @param - offt : Offset relative to the file's starting address
 * @return       : Number of bytes written, negative value indicates write failure
 */
static ssize_t key_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt) {
    return 0;
}

/*
 * @description  : Close/Release the device
 * @param - filp : Device file (file descriptor)
 * @return       : 0 on success; other values on failure
 */
static int key_release(struct inode *inode, struct file *filp) {
    struct key_dev *dev = filp->private_data;
    gpio_free(dev->key_gpio);
    
    return 0;
}

// Device operations function structure
static struct file_operations key_fops = {
    .owner = THIS_MODULE,
    .open = key_open,
    .read = key_read,
    .write = key_write,
    .release = key_release,
};

/*
 * @description : Driver entry function
 * @param       : None
 * @return      : 0 on success; other values on failure
 */
static int __init mykey_init(void) {
    int ret;
    keydev.keyvalue = (atomic_t)ATOMIC_INIT(0);  // Initialize atomic variable
    atomic_set(&keydev.keyvalue, INVAKEY);  // Initial value is INVAKEY

    // Register character device driver
    if (keydev.major) {  // Defined device number
        keydev.devid = MKDEV(keydev.major, 0);
        ret = register_chrdev_region(keydev.devid, KEY_CNT, KEY_NAME);
        if(ret < 0) {
            pr_err("cannot register %s char driver [ret=%d]\n", KEY_NAME, KEY_CNT);
            return -EIO;
        }
    } else {  // Undefined device number
        ret = alloc_chrdev_region(&keydev.devid, 0, KEY_CNT, KEY_NAME);  // Allocate device number
        if(ret < 0) {
            pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", KEY_NAME, ret);
            return -EIO;
        }
        keydev.major = MAJOR(keydev.devid);  // Get the major device number
        keydev.minor = MINOR(keydev.devid);  // Get the minor device number
    }
    printk("keydev major=%d,minor=%d\r\n", keydev.major, keydev.minor);    
    
    keydev.cdev.owner = THIS_MODULE;
    cdev_init(&keydev.cdev, &key_fops);
    
    cdev_add(&keydev.cdev, keydev.devid, KEY_CNT);
    if(ret < 0)
        goto del_unregister;
        
    keydev.class = class_create(THIS_MODULE, KEY_NAME);
    if (IS_ERR(keydev.class)) {
        goto del_cdev;
    }

    keydev.device = device_create(keydev.class, NULL, keydev.devid, NULL, KEY_NAME);
    if (IS_ERR(keydev.device)) {
        goto destroy_class;
    }
    return 0;

destroy_class:
    device_destroy(keydev.class, keydev.devid);
del_cdev:
    cdev_del(&keydev.cdev);
del_unregister:
    unregister_chrdev_region(keydev.devid, KEY_CNT);
    return -EIO;
}

/*
 * @description : Driver exit function
 * @param       : None
 * @return      : None
 */
static void __exit mykey_exit(void) {
    cdev_del(&keydev.cdev);  // Delete cdev
    unregister_chrdev_region(keydev.devid, KEY_CNT);  // Unregister device number

    device_destroy(keydev.class, keydev.devid);
    class_destroy(keydev.class);
}

module_init(mykey_init);
module_exit(mykey_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
