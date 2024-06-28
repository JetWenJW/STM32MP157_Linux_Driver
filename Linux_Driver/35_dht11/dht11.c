#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of_gpio.h>

struct dht11_dev {
    struct miscdevice mdev;     /* MISC device */
    int gpio;                   /* GPIO number */
    struct timer_list timer;    /* Timer */
    struct work_struct work;    /* Work queue */
    u8 data[5];                 /* Data buffer */
};

#define HIGH    1
#define LOW     0

struct dht11_dev dht11_device;

/* Set GPIO output value */
static void dht11_set_output(int val)
{
    if (val)
        gpio_direction_output(dht11_device.gpio, 1);
    else
        gpio_direction_output(dht11_device.gpio, 0);
}

/* Set GPIO to input mode */
static void dht11_set_input(void)
{
    gpio_direction_input(dht11_device.gpio);
}

/* Get GPIO value */
static unsigned char dht11_get_io(void)
{
    return gpio_get_value(dht11_device.gpio);
}

/* Read a byte of data from DHT11 */
static unsigned char dht11_read_byte(void)
{
    unsigned char i, time = 0, data = 0;
    local_irq_disable();

    for (i = 0; i < 8; i++) {
        time = 0;
        while (dht11_get_io() == 0) {
            udelay(1);
            time++;
            if (time > 100)
                return -EINVAL;
        }
        udelay(45);     /* Delay for 45us */
        if (dht11_get_io() == 1) {  /* If high level is detected, the data is 1, otherwise it's 0 */
            data |= 1 << (7 - i);
            time = 0;
            while (dht11_get_io() == 1) {
                udelay(1);
                time++;
                if (time > 100)
                    return -EINVAL;
            }
        }
    }
    local_irq_enable();
    return data;
}

/* Initialize DHT11 sensor */
static int dht11_init(void)
{
    dht11_set_output(HIGH); /* Pull up */
    udelay(30);             /* Pull up for 30us */

    dht11_set_output(LOW);  /* Pull down */
    mdelay(20);             /* Pull down for 20ms */

    dht11_set_output(HIGH); /* Pull up */
    udelay(30);             /* Pull up for 30us */

    dht11_set_input();      /* Set pin to input mode */
    udelay(200);            /* Delay for 200us */

    if (!dht11_get_io()) {  /* If not high, DHT11 doesn't respond */
        return -ENODEV;
    }
    return 0;
}

/* Open device */
static int dht11_open(struct inode *inode, struct file *filp)
{
    return 0;
}

/* Read data from device */
static ssize_t dht11_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
    int ret = 0;
    ret = copy_to_user(buf, &dht11_device.data[0], 5);
    return ret;
}

static struct file_operations dht11_fops = {
    .owner  = THIS_MODULE,
    .open   = dht11_open,
    .read   = dht11_read,
};

/* Callback function for work queue to fetch raw temperature data */
static void dht11_work_callback(struct work_struct *work)
{
    int i = 0;
    unsigned char buff[5];

    if (!dht11_init()) {
        for (i = 0; i < 5; i++) {
            buff[i] = dht11_read_byte();    /* Read data */
        }
        /* Verify data */
        if ((buff[0] + buff[1] + buff[2] + buff[3]) == buff[4]) {
            memcpy(&dht11_device.data[0], &buff[0], 5);
        }
    }
}

/* Timer callback function, fetch data every 1s */
static void dht11_timer_callback(struct timer_list *arg)
{
    schedule_work(&dht11_device.work);
    mod_timer(&dht11_device.timer, jiffies + (1500 * HZ / 1000));
}

/* Initialize GPIO */
static int dht11_request_gpio(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    int ret;

    dht11_device.gpio = of_get_named_gpio(dev->of_node, "dht11-gpio", 0);
    if (!gpio_is_valid(dht11_device.gpio)) {
        dev_err(dev, "Failed to get gpio");
        return -EINVAL;
    }

    ret = devm_gpio_request(dev, dht11_device.gpio, "DHT11 Gpio");
    if (ret) {
        dev_err(dev, "Failed to request gpio");
        return ret;
    }

    return 0;
}

/* Probe function for the driver */
static int dht11_probe(struct platform_device *pdev)
{
    struct miscdevice *mdev;
    int ret;

    dev_info(&pdev->dev, "dht11 device and driver matched successfully!\n");

    ret = dht11_request_gpio(pdev);
    if (ret)
        return ret;

    /* Initialize MISC device */
    mdev = &dht11_device.mdev;
    mdev->name  = "dht11";
    mdev->minor = MISC_DYNAMIC_MINOR;
    mdev->fops  = &dht11_fops;

    /* Initialize timer */
    timer_setup(&dht11_device.timer, dht11_timer_callback, 0);
    dht11_device.timer.expires = jiffies + msecs_to_jiffies(1500);
    add_timer(&dht11_device.timer);

    /* Initialize work queue */
    INIT_WORK(&dht11_device.work, dht11_work_callback);

    /* Register MISC device */
    return misc_register(mdev);
}

/* Remove function for the driver */
static int dht11_remove(struct platform_device *pdev)
{
    gpio_set_value(dht11_device.gpio, 0);

    /* Unregister MISC device */
    misc_deregister(&dht11_device.mdev);

    /* Remove timer */
    del_timer(&dht11_device.timer);

    /* Cancel work queue */
    cancel_work_sync(&dht11_device.work);

    dev_info(&pdev->dev, "DHT11 driver has been removed!\n");
    return 0;
}

/* Device tree compatible ID table */
static const struct of_device_id dht11_of_match[] = {
    { .compatible = "alientek,dht11" },
    { /* Sentinel */ }
};

/* Platform driver structure */
static struct platform_driver dht11_driver = {
    .driver = {
        .owner          = THIS_MODULE,
        .name           = "dht11",
        .of_match_table = dht11_of_match,
    },
    .probe      = dht11_probe,
    .remove     = dht11_remove,
};

module_platform_driver(dht11_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
