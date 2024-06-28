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
#include <linux/of_irq.h>
#include <linux/irq.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define KEY_CNT     1       /* Number of device IDs */
#define KEY_NAME    "key"   /* Device name */

/* Define key status */
enum key_status {
    KEY_PRESS = 0,      // Key pressed
    KEY_RELEASE,        // Key released
    KEY_KEEP,           // Key status unchanged
};

/* Structure for the key device */
struct key_dev {
    dev_t devid;            /* Device ID */
    struct cdev cdev;       /* Character device structure */
    struct class *class;    /* Device class */
    struct device *device;  /* Device */
    struct device_node *nd; /* Device node */
    int key_gpio;           /* GPIO number used by the key */
    struct timer_list timer; /* Timer for key value */
    int irq_num;            /* IRQ number */
    
    atomic_t status;        /* Key status */
    wait_queue_head_t r_wait;   /* Read wait queue */
};

static struct key_dev key;          /* Key device */

static irqreturn_t key_interrupt(int irq, void *dev_id)
{
    /* Key debounce handling, start timer with 15ms delay */
    mod_timer(&key.timer, jiffies + msecs_to_jiffies(15));
    return IRQ_HANDLED;
}

/*
 * @description : Initialize key IO, called when open function is called
 *                to initialize GPIO pins used by the key.
 * @param       : None
 * @return      : None
 */
static int key_parse_dt(void)
{
    int ret;
    const char *str;
    
    /* Set GPIO used by the key */
    /* 1. Get device node: /key */
    key.nd = of_find_node_by_path("/key");
    if (key.nd == NULL) {
        printk("key node not found!\r\n");
        return -EINVAL;
    }

    /* 2. Read status property */
    ret = of_property_read_string(key.nd, "status", &str);
    if (ret < 0)
        return -EINVAL;

    if (strcmp(str, "okay"))
        return -EINVAL;
    
    /* 3. Get compatible property value and match */
    ret = of_property_read_string(key.nd, "compatible", &str);
    if (ret < 0) {
        printk("key: Failed to get compatible property\n");
        return -EINVAL;
    }

    if (strcmp(str, "alientek,key")) {
        printk("key: Compatible match failed\n");
        return -EINVAL;
    }

    /* 4. Get gpio property from device tree to obtain GPIO number used by KEY0 */
    key.key_gpio = of_get_named_gpio(key.nd, "key-gpio", 0);
    if (key.key_gpio < 0) {
        printk("can't get key-gpio");
        return -EINVAL;
    }

    /* 5. Get GPIO corresponding interrupt number */
    key.irq_num = irq_of_parse_and_map(key.nd, 0);
    if (!key.irq_num) {
        return -EINVAL;
    }

    printk("key-gpio num = %d\r\n", key.key_gpio);
    return 0;
}

static int key_gpio_init(void)
{
    int ret;
    unsigned long irq_flags;
    
    ret = gpio_request(key.key_gpio, "KEY0");
    if (ret) {
        printk(KERN_ERR "key: Failed to request key-gpio\n");
        return ret;
    }   
    
    /* Set GPIO as input mode */
    gpio_direction_input(key.key_gpio);

   /* Get interrupt trigger type specified in device tree */
    irq_flags = irq_get_trigger_type(key.irq_num);
    if (IRQF_TRIGGER_NONE == irq_flags)
        irq_flags = IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING;
        
    /* Request interrupt */
    ret = request_irq(key.irq_num, key_interrupt, irq_flags, "Key0_IRQ", NULL);
    if (ret) {
        gpio_free(key.key_gpio);
        return ret;
    }

    return 0;
}

static void key_timer_function(struct timer_list *arg)
{
    static int last_val = 1;
    int current_val;

    /* Read key value and determine current key status */
    current_val = gpio_get_value(key.key_gpio);
    if (0 == current_val && last_val) {
        atomic_set(&key.status, KEY_PRESS);   // Key pressed
        wake_up_interruptible(&key.r_wait);  // Wake up all queues in r_wait
    }
    else if (1 == current_val && !last_val) {
        atomic_set(&key.status, KEY_RELEASE); // Key released
        wake_up_interruptible(&key.r_wait);  // Wake up all queues in r_wait
    }
    else
        atomic_set(&key.status, KEY_KEEP);     // Status unchanged

    last_val = current_val;
}

/*
 * @description     : Open device
 * @param - inode   : Passed to driver's inode
 * @param - filp    : Device file, file structure has a member variable
 *                    private_data generally pointed to device structure during open.
 * @return          : 0 on success, otherwise failure
 */
static int key_open(struct inode *inode, struct file *filp)
{
    return 0;
}

/*
 * @description     : Read data from device
 * @param – filp    : Device file to open (file descriptor)
 * @param – buf     : Data buffer returned to user space
 * @param – cnt     : Length of data to read
 * @param – offt    : Offset relative to file start address
 * @return          : Number of bytes read, negative value indicates read failure
 */
static ssize_t key_read(struct file *filp, char __user *buf,
            size_t cnt, loff_t *offt)
{
    int ret;

    /* Add to wait queue, only wake up when key press or release action occurs */
    ret = wait_event_interruptible(key.r_wait, KEY_KEEP != atomic_read(&key.status));
    if (ret)
        return ret;
        
    /* Send key status information to application */
    ret = copy_to_user(buf, &key.status, sizeof(int));
    
    /* Reset status */
    atomic_set(&key.status, KEY_KEEP);

    return ret;
}

/*
 * @description     : Write data to device
 * @param - filp    : Device file, represents opened file descriptor
 * @param - buf     : Data to write to device
 * @param - cnt     : Length of data to write
 * @param - offt    : Offset relative to file start address
 * @return          : Number of bytes written, negative value indicates write failure
 */
static ssize_t key_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
    return 0;
}

/*
 * @description     : Close/release device
 * @param - filp    : Device file to close (file descriptor)
 * @return          : 0 on success, otherwise failure
 */
static int key_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/* Device operation functions */
static struct file_operations key_fops = {
    .owner = THIS_MODULE,
    .open = key_open,
    .read = key_read,
    .write = key_write,
    .release = key_release,
};

/*
 * @description     : Driver entry function
 * @param           : None
 * @return          : None
 */
static int __init mykey_init(void)
{
    int ret;
    
    /* Initialize wait queue head */
    init_waitqueue_head(&key.r_wait);
    
    /* Initialize key status */
    atomic_set(&key.status, KEY_KEEP);

    /* Device tree parsing */
    ret = key_parse_dt();
    if (ret)
        return ret;
        
    /* GPIO interrupt initialization */
    ret = key_gpio_init();
    if (ret)
        return ret;
        
    /* Register character device driver */
    /* 1. Create device number */
    ret = alloc_chrdev_region(&key.devid, 0, KEY_CNT, KEY_NAME); /* Allocate device number */
    if (ret < 0) {
        pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", KEY_NAME, ret);
        goto free_gpio;
    }
    
    /* 2. Initialize cdev */
    key.cdev.owner = THIS_MODULE;
    cdev_init(&key.cdev, &key_fops);
    
    /* 3. Add cdev */
    ret = cdev_add(&key.cdev, key.devid, KEY_CNT);
    if (ret < 0)
        goto del_unregister;
        
    /* 4. Create class */
    key.class = class_create(THIS_MODULE, KEY_NAME);
    if (IS_ERR(key.class)) {
        goto del_cdev;
    }

    /* 5. Create device */
    key.device = device_create(key.class, NULL, key.devid, NULL, KEY_NAME);
    if (IS_ERR(key.device)) {
        goto destroy_class;
    }
    
    /* 6. Initialize timer, set timer handler function, but not activate the timer yet */
    timer_setup(&key.timer, key_timer_function, 0);
    
    return 0;

destroy_class:
    device_destroy(key.class, key.devid);
del_cdev:
    cdev_del(&key.cdev);
del_unregister:
    unregister_chrdev_region(key.devid, KEY_CNT);
free_gpio:
    free_irq(key.irq_num, NULL);
    gpio_free(key.key_gpio);
    return -EIO;
}

/*
 * @description     : Driver exit function
 * @param           : None
 * @return          : None
 */
static void __exit mykey_exit(void)
{
    /* Unregister character device driver */
    cdev_del(&key.cdev);    /* Delete cdev */
    unregister_chrdev_region(key.devid, KEY_CNT); /* Unregister device number */
    del_timer_sync(&key.timer); /* Delete timer */
    device_destroy(key.class, key.devid);   /* Unregister device */
    class_destroy(key.class);   /* Destroy class */
    free_irq(key.irq_num, NULL);    /* Free interrupt */
    gpio_free(key.key_gpio);    /* Free IO */
}

module_init(mykey_init);
module_exit(mykey_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");

