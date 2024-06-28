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

#define KEY_CNT		1		/* Number of devices */
#define KEY_NAME	"key"	/* Device name */

/* Key status definitions */
enum key_status {
    KEY_PRESS = 0,      // Key pressed
    KEY_RELEASE,        // Key released
    KEY_KEEP,           // Key state maintained
};

/* Key device structure */
struct key_dev {
    dev_t devid;            /* Device ID */
    struct cdev cdev;       /* Character device structure */
    struct class *class;    /* Device class */
    struct device *device;  /* Device */
    struct device_node *nd; /* Device node */
    int key_gpio;           /* GPIO number for key */
    struct timer_list timer;/* Timer for key value */
    int irq_num;            /* IRQ number */
    
    atomic_t status;        /* Key status */
    wait_queue_head_t r_wait; /* Read wait queue head */
};

static struct key_dev key;  /* Key device */

static irqreturn_t key_interrupt(int irq, void *dev_id)
{
    /* Debounce key, start timer with 15ms delay */
    mod_timer(&key.timer, jiffies + msecs_to_jiffies(15));
    return IRQ_HANDLED;
}

static int key_parse_dt(void)
{
    int ret;
    const char *str;
    
    /* Retrieve device node: /key */
    key.nd = of_find_node_by_path("/key");
    if (key.nd == NULL) {
        printk("key node not found!\r\n");
        return -EINVAL;
    }

    /* Read status property */
    ret = of_property_read_string(key.nd, "status", &str);
    if (ret < 0) 
        return -EINVAL;

    if (strcmp(str, "okay"))
        return -EINVAL;
    
    /* Read compatible property */
    ret = of_property_read_string(key.nd, "compatible", &str);
    if (ret < 0) {
        printk("key: Failed to get compatible property\n");
        return -EINVAL;
    }

    if (strcmp(str, "alientek,key")) {
        printk("key: Compatible match failed\n");
        return -EINVAL;
    }

    /* Retrieve GPIO property */
    key.key_gpio = of_get_named_gpio(key.nd, "key-gpio", 0);
    if (key.key_gpio < 0) {
        printk("can't get key-gpio");
        return -EINVAL;
    }

    /* Get GPIO interrupt number */
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
    
    /* Set GPIO direction to input */
    gpio_direction_input(key.key_gpio);

    /* Get interrupt trigger type from device tree */
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

    /* Read key value and determine current state */
    current_val = gpio_get_value(key.key_gpio);
    if (0 == current_val && last_val){
        atomic_set(&key.status, KEY_PRESS);
        wake_up_interruptible(&key.r_wait);
    }
    else if (1 == current_val && !last_val) {
        atomic_set(&key.status, KEY_RELEASE);
        wake_up_interruptible(&key.r_wait);
    }
    else
        atomic_set(&key.status, KEY_KEEP);

    last_val = current_val;
}

static int key_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t key_read(struct file *filp, char __user *buf,
            size_t cnt, loff_t *offt)
{
    int ret;
    
    if (filp->f_flags & O_NONBLOCK) {   // Non-blocking mode
        if(KEY_KEEP == atomic_read(&key.status))
            return -EAGAIN;
    } else {                            // Blocking mode
        /* Wait for key press or release action */
        ret = wait_event_interruptible(key.r_wait, KEY_KEEP != atomic_read(&key.status));
        if(ret)
            return ret;
    }
    /* Send key status information to user space */
    ret = copy_to_user(buf, &key.status, sizeof(int));

    /* Reset status */
    atomic_set(&key.status, KEY_KEEP);

    return ret;
}

static ssize_t key_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
    return 0;
}

static int key_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static unsigned int key_poll(struct file *filp, struct poll_table_struct *wait)
{
    unsigned int mask = 0;

    poll_wait(filp, &key.r_wait, wait);

    if(KEY_KEEP != atomic_read(&key.status)) // Key press or release action
        mask = POLLIN | POLLRDNORM;

    return mask;
}

static struct file_operations key_fops = {
    .owner = THIS_MODULE,
    .open = key_open,
    .read = key_read,
    .write = key_write,
    .release = key_release,
    .poll = key_poll,
};

static int __init mykey_init(void)
{
    int ret;
    
    /* Initialize wait queue head */
    init_waitqueue_head(&key.r_wait);
    
    /* Initialize key status */
    atomic_set(&key.status, KEY_KEEP);

    /* Device tree parsing */
    ret = key_parse_dt();
    if(ret)
        return ret;
        
    /* GPIO interrupt initialization */
    ret = key_gpio_init();
    if(ret)
        return ret;
        
    /* Register character device driver */
    /* 1. Create device number */
    ret = alloc_chrdev_region(&key.devid, 0, KEY_CNT, KEY_NAME);
    if(ret < 0) {
        pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", KEY_NAME, ret);
        goto free_gpio;
    }
    
    /* 2. Initialize cdev */
    key.cdev.owner = THIS_MODULE;
    cdev_init(&key.cdev, &key_fops);
    
    /* 3. Add cdev */
    ret = cdev_add(&key.cdev, key.devid, KEY_CNT);
    if(ret < 0)
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
    
    /* 6. Initialize timer, set timer function */
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

static void __exit mykey_exit(void)
{
    /* Unregister character device driver */
    cdev_del(&key.cdev);
    unregister_chrdev_region(key.devid, KEY_CNT);
    del_timer_sync(&key.timer);
    device_destroy(key.class, key.devid);
    class_destroy(key.class);
    free_irq(key.irq_num, NULL);
    gpio_free(key.key_gpio);
}

module_init(mykey_init);
module_exit(mykey_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
