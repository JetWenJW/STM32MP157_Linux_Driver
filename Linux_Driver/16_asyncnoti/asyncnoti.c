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
#include <linux/wait.h>
#include <linux/poll.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/fcntl.h>

#define KEY_CNT         1       /* Number of devices */
#define KEY_NAME        "key"   /* Device name */

enum key_status {
    KEY_PRESS = 0,      // Key pressed
    KEY_RELEASE,        // Key released
    KEY_KEEP,           // Key state unchanged
};

struct key_dev {
    dev_t devid;                /* Device ID */
    struct cdev cdev;           /* Character device structure */
    struct class *class;        /* Device class */
    struct device *device;      /* Device structure */
    struct device_node *nd;     /* Device node */
    int key_gpio;               /* GPIO number for the key */
    struct timer_list timer;    /* Timer for key value */
    int irq_num;                /* Interrupt number */
    atomic_t status;            /* Key status */
    wait_queue_head_t r_wait;   /* Read wait queue */
    struct fasync_struct *async_queue;   /* Asynchronous notification structure */
};

static struct key_dev key;       /* Key device structure */

static irqreturn_t key_interrupt(int irq, void *dev_id)
{
    /* Debounce the key press by starting a timer with 15ms delay */
    mod_timer(&key.timer, jiffies + msecs_to_jiffies(15));
    return IRQ_HANDLED;
}

/*
 * @description : Parse device tree to get key information
 * @param       : None
 * @return      : 0 if success, -EINVAL if error
 */
static int key_parse_dt(void)
{
    int ret;
    const char *str;
    
    /* Find the device node "/key" */
    key.nd = of_find_node_by_path("/key");
    if (key.nd == NULL) {
        printk("key node not found!\r\n");
        return -EINVAL;
    }

    /* Read the "status" property */
    ret = of_property_read_string(key.nd, "status", &str);
    if (ret < 0)
        return -EINVAL;

    /* Check if the status property is "okay" */
    if (strcmp(str, "okay"))
        return -EINVAL;

    /* Read the "compatible" property */
    ret = of_property_read_string(key.nd, "compatible", &str);
    if (ret < 0) {
        printk("key: Failed to get compatible property\n");
        return -EINVAL;
    }

    /* Check if the compatible property matches "alientek,key" */
    if (strcmp(str, "alientek,key")) {
        printk("key: Compatible match failed\n");
        return -EINVAL;
    }

    /* Get the GPIO number for the key */
    key.key_gpio = of_get_named_gpio(key.nd, "key-gpio", 0);
    if (key.key_gpio < 0) {
        printk("can't get key-gpio");
        return -EINVAL;
    }

    /* Get the interrupt number corresponding to the GPIO */
    key.irq_num = irq_of_parse_and_map(key.nd, 0);
    if (!key.irq_num) {
        return -EINVAL;
    }

    printk("key-gpio num = %d\r\n", key.key_gpio);
    return 0;
}

/*
 * @description : Initialize GPIO for key
 * @param       : None
 * @return      : 0 if success, error code if fail
 */
static int key_gpio_init(void)
{
    int ret;
    unsigned long irq_flags;

    /* Request GPIO for the key */
    ret = gpio_request(key.key_gpio, "KEY0");
    if (ret) {
        printk(KERN_ERR "key: Failed to request key-gpio\n");
        return ret;
    }

    /* Set GPIO direction to input */
    gpio_direction_input(key.key_gpio);

    /* Get the interrupt trigger type */
    irq_flags = irq_get_trigger_type(key.irq_num);
    if (IRQF_TRIGGER_NONE == irq_flags)
        irq_flags = IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING;

    /* Request the interrupt */
    ret = request_irq(key.irq_num, key_interrupt, irq_flags, "Key0_IRQ", NULL);
    if (ret) {
        gpio_free(key.key_gpio);
        return ret;
    }

    return 0;
}

/*
 * @description : Timer function for key debounce and state change detection
 * @param       : arg - pointer to the timer_list structure
 * @return      : None
 */
static void key_timer_function(struct timer_list *arg)
{
    static int last_val = 1;
    int current_val;

    /* Read the current value of the key */
    current_val = gpio_get_value(key.key_gpio);

    /* Detect key press */
    if (0 == current_val && last_val) {
        atomic_set(&key.status, KEY_PRESS);
        wake_up_interruptible(&key.r_wait);
        if (key.async_queue)
            kill_fasync(&key.async_queue, SIGIO, POLL_IN);
    } 
    /* Detect key release */
    else if (1 == current_val && !last_val) {
        atomic_set(&key.status, KEY_RELEASE);
        wake_up_interruptible(&key.r_wait);
        if (key.async_queue)
            kill_fasync(&key.async_queue, SIGIO, POLL_IN);
    } 
    /* Key state unchanged */
    else {
        atomic_set(&key.status, KEY_KEEP);
    }

    last_val = current_val;
}

/*
 * @description : Open function for the key device
 * @param       : inode - pointer to inode structure
 *                filp - pointer to file structure
 * @return      : 0 if success
 */
static int key_open(struct inode *inode, struct file *filp)
{
    return 0;
}

/*
 * @description : Read function for the key device
 * @param       : filp - pointer to file structure
 *                buf - user buffer to store data
 *                cnt - length of data to read
 *                offt - file offset
 * @return      : number of bytes read, or negative error code
 */
static ssize_t key_read(struct file *filp, char __user *buf,
            size_t cnt, loff_t *offt)
{
    int ret;
    
    /* Non-blocking read */
    if (filp->f_flags & O_NONBLOCK) {
        if (KEY_KEEP == atomic_read(&key.status))
            return -EAGAIN;
    } 
    /* Blocking read */
    else {
        ret = wait_event_interruptible(key.r_wait, KEY_KEEP != atomic_read(&key.status));
        if (ret)
            return ret;
    }

    /* Copy key status to user buffer */
    ret = copy_to_user(buf, &key.status, sizeof(int));
    atomic_set(&key.status, KEY_KEEP);

    return ret;
}

/*
 * @description : Fasync function for asynchronous notification
 * @param       : fd - file descriptor
 *                filp - pointer to file structure
 *                on - mode
 * @return      : negative error code if fail, otherwise success
 */
static int key_fasync(int fd, struct file *filp, int on)
{
    return fasync_helper(fd, filp, on, &key.async_queue);
}

/*
 * @description : Write function for the key device
 * @param       : filp - pointer to file structure
 *                buf - user buffer containing data to write
 *                cnt - length of data to write
 *                offt - file offset
 * @return      : 0 (write function not supported for this device)
 */
static ssize_t key_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
    return 0;
}

/*
 * @description : Release function for the key device
 * @param       : inode - pointer to inode structure
 *                filp - pointer to file structure
 * @return      : 0 if success
 */
static int key_release(struct inode *inode, struct file *filp)
{
    return key_fasync(-1, filp, 0);
}

/*
 * @description : Poll function for the key device
 * @param       : filp - pointer to file structure
 *                wait - poll_table structure
 * @return      : device or resource status
 */
static unsigned int key_poll(struct file *filp, struct poll_table_struct *wait)
{
    unsigned int mask = 0;

    poll_wait(filp, &key.r_wait, wait);

    if (KEY_KEEP != atomic_read(&key.status))
	    mask = POLLIN | POLLRDNORM;

    return mask;
}

/* File operations structure for the key device */
static struct file_operations key_fops = {
    .owner = THIS_MODULE,
    .open = key_open,
    .read = key_read,
    .write = key_write,
    .release = key_release,
    .poll = key_poll,
    .fasync = key_fasync,
};

/*
 * @description : Module initialization function
 * @return      : 0 if success, error code if fail
 */
static int __init mykey_init(void)
{
    int ret;
    
    /* Initialize wait queue and set initial key status */
    init_waitqueue_head(&key.r_wait);
    atomic_set(&key.status, KEY_KEEP);

    /* Parse device tree to get key information */
    ret = key_parse_dt();
    if (ret)
        return ret;

    /* Initialize GPIO for key */
    ret = key_gpio_init();
    if (ret)
        return ret;

    /* Allocate character device region */
    ret = alloc_chrdev_region(&key.devid, 0, KEY_CNT, KEY_NAME);
    if (ret < 0) {
        pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", KEY_NAME, ret);
        goto free_gpio;
    }

    /* Initialize character device structure */
    key.cdev.owner = THIS_MODULE;
    cdev_init(&key.cdev, &key_fops);

    /* Add character device to the system */
    ret = cdev_add(&key.cdev, key.devid, KEY_CNT);
    if (ret < 0)
        goto del_unregister;

    /* Create device class */
    key.class = class_create(THIS_MODULE, KEY_NAME);
    if (IS_ERR(key.class)) {
        goto del_cdev;
    }

    /* Create device node */
    key.device = device_create(key.class, NULL, key.devid, NULL, KEY_NAME);
    if (IS_ERR(key.device)) {
        goto destroy_class;
    }

    /* Initialize timer for key debounce */
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
 * @description : Module exit function
 * @return      : None
 */
static void __exit mykey_exit(void)
{
    /* Delete character device */
    cdev_del(&key.cdev);

    /* Unregister character device region */
    unregister_chrdev_region(key.devid, KEY_CNT);

    /* Delete timer */
    del_timer_sync(&key.timer);

    /* Destroy device node */
    device_destroy(key.class, key.devid);

    /* Destroy device class */
    class_destroy(key.class);

    /* Free interrupt and GPIO resources */
    free_irq(key.irq_num, NULL);
    gpio_free(key.key_gpio);
}

/* Module initialization and exit macros */
module_init(mykey_init);
module_exit(mykey_exit);

/* Module licensing and information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");

