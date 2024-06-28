#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/mach/map.h>
#include <linux/semaphore.h>

#define KEY_CNT		1             /* Number of device instances */
#define KEY_NAME	"key"         /* Device name */

enum key_status {
    KEY_PRESS = 0,                /* Key pressed */
    KEY_RELEASE,                  /* Key released */
    KEY_KEEP                      /* Key state maintained */
};

struct key_dev {
	dev_t devid;                   /* Device ID */
	struct cdev cdev;              /* Character device structure */
	struct class *class;           /* Device class */
	struct device *device;         /* Device instance */
	struct device_node *nd;        /* Device node */
	int key_gpio;                  /* GPIO number for key */
	struct timer_list timer;       /* Timer for debounce */
	int irq_num;                   /* IRQ number */
	spinlock_t spinlock;           /* Spinlock for critical sections */
};

static struct key_dev key;         /* Key device instance */
static int status = KEY_KEEP;      /* Current key status */

/* IRQ handler for key interrupt */
static irqreturn_t key_interrupt(int irq, void *dev_id)
{
	mod_timer(&key.timer, jiffies + msecs_to_jiffies(15)); /* Debounce with 15ms delay */
    return IRQ_HANDLED;
}

/* Initialize key GPIO from device tree */
static int key_parse_dt(void)
{
	int ret;
	const char *str;
	
	key.nd = of_find_node_by_path("/key"); /* Find device node */
	if (key.nd == NULL)
		return -EINVAL;

	ret = of_property_read_string(key.nd, "status", &str); /* Read 'status' property */
	if (ret < 0 || strcmp(str, "okay") != 0)
	    return -EINVAL;

	ret = of_property_read_string(key.nd, "compatible", &str); /* Read 'compatible' property */
	if (ret < 0 || strcmp(str, "alientek,key") != 0)
		return -EINVAL;

	key.key_gpio = of_get_named_gpio(key.nd, "key-gpio", 0); /* Read 'key-gpio' property */
	if (key.key_gpio < 0)
		return -EINVAL;

    key.irq_num = irq_of_parse_and_map(key.nd, 0); /* Map IRQ from device tree */
    if (!key.irq_num)
	{
        return -EINVAL;
	}

	return 0;
}

/* Initialize key GPIO and IRQ */
static int key_gpio_init(void)
{
	int ret;
    unsigned long irq_flags;
	
	ret = gpio_request(key.key_gpio, "KEY0"); /* Request GPIO for key */
    if (ret)
	{
        return ret;
	}

	gpio_direction_input(key.key_gpio); /* Set GPIO direction to input */

	irq_flags = irq_get_trigger_type(key.irq_num); /* Get IRQ trigger type */
	if (irq_flags == IRQF_TRIGGER_NONE)
		irq_flags = IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING;

	ret = request_irq(key.irq_num, key_interrupt, irq_flags, "Key0_IRQ", NULL); /* Request IRQ */
    if (ret) {
        gpio_free(key.key_gpio);
        return ret;
    }

	return 0;
}

/* Timer function for handling key status */
static void key_timer_function(struct timer_list *arg)
{
    static int last_val = 1;
    unsigned long flags;
    int current_val;

    spin_lock_irqsave(&key.spinlock, flags); /* Lock critical section with spinlock */

    current_val = gpio_get_value(key.key_gpio); /* Read current key value */
    if (current_val == 0 && last_val)         /* Key pressed */
        status = KEY_PRESS;
    else if (current_val == 1 && !last_val)   /* Key released */
        status = KEY_RELEASE;
    else
        status = KEY_KEEP;                    /* Key state maintained */

    last_val = current_val;

    spin_unlock_irqrestore(&key.spinlock, flags); /* Unlock critical section */
}

/* Open function for key device */
static int key_open(struct inode *inode, struct file *filp)
{
	return 0;
}

/* Read function for key device */
static ssize_t key_read(struct file *filp, char __user *buf,
            size_t cnt, loff_t *offt)
{
    unsigned long flags;
    int ret;

    spin_lock_irqsave(&key.spinlock, flags); /* Lock critical section with spinlock */

    ret = copy_to_user(buf, &status, sizeof(int)); /* Copy key status to user */

    status = KEY_KEEP; /* Reset key status */

    spin_unlock_irqrestore(&key.spinlock, flags); /* Unlock critical section */

    return ret;
}

/* Write function for key device */
static ssize_t key_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
	return 0; /* Write function not implemented */
}

/* Release function for key device */
static int key_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/* Device operations structure */
static struct file_operations key_fops = {
	.owner = THIS_MODULE,
	.open = key_open,
	.read = key_read,
	.write = key_write,
	.release = key_release,
};

/* Module initialization function */
static int __init mykey_init(void)
{
	int ret;
	
	spin_lock_init(&key.spinlock); /* Initialize spinlock */

	ret = key_parse_dt(); /* Parse device tree */
	if (ret)
		return ret;

	ret = key_gpio_init(); /* Initialize GPIO and IRQ */
	if (ret)
		return ret;

	/* Register character device */
	ret = alloc_chrdev_region(&key.devid, 0, KEY_CNT, KEY_NAME);
	if (ret < 0)
		goto free_gpio;

	key.cdev.owner = THIS_MODULE;
	cdev_init(&key.cdev, &key_fops);

	ret = cdev_add(&key.cdev, key.devid, KEY_CNT);
	if (ret < 0)
		goto del_unregister;

	key.class = class_create(THIS_MODULE, KEY_NAME);
	if (IS_ERR(key.class)) {
		goto del_cdev;
	}

	key.device = device_create(key.class, NULL, key.devid, NULL, KEY_NAME);
	if (IS_ERR(key.device)) {
		goto destroy_class;
	}

	timer_setup(&key.timer, key_timer_function, 0); /* Initialize timer */

	return 0;

destroy_class:
	class_destroy(key.class);
del_cdev:
	cdev_del(&key.cdev);
del_unregister:
	unregister_chrdev_region(key.devid, KEY_CNT);
free_gpio:
	free_irq(key.irq_num, NULL);
	gpio_free(key.key_gpio);
	return -EIO;
}

/* Module exit function */
static void __exit mykey_exit(void)
{
	cdev_del(&key.cdev); /* Delete cdev */
	unregister_chrdev_region(key.devid, KEY_CNT); /* Unregister device number */
	del_timer_sync(&key.timer); /* Delete timer */
	device_destroy(key.class, key.devid); /* Destroy device */
	class_destroy(key.class); /* Destroy class */
	free_irq(key.irq_num, NULL); /* Free IRQ */
	gpio_free(key.key_gpio); /* Free GPIO */
}

module_init(mykey_init);
module_exit(mykey_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
