#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/errno.h>

struct ds18b20_dev {
    struct miscdevice mdev;         /* MISC device */
    int gpio;                       /* GPIO number */
    unsigned char data[2];          /* Data buffer */
    struct timer_list timer;        /* Timer */
    struct work_struct work;        /* Work queue */
};

#define HIGH    1
#define LOW     0

struct ds18b20_dev ds18b20_device;

/* Set GPIO output value */
static void ds18b20_set_output(int value)
{
    if (value)
        gpio_direction_output(ds18b20_device.gpio, 1);
    else
        gpio_direction_output(ds18b20_device.gpio, 0);
}

/* Set GPIO to input mode */
static void ds18b20_set_input(void)
{
    gpio_direction_input(ds18b20_device.gpio);
}

/* Get GPIO value */
static int ds18b20_get_io(void)
{
    return gpio_get_value(ds18b20_device.gpio); 
}

/* Write a bit to DS18B20 */
static void ds18b20_write_bit(int bit)
{
    local_irq_disable();
    if (bit) {
        ds18b20_set_output(LOW);
        udelay(2);
        ds18b20_set_output(HIGH);
        udelay(60);
    } else {
        ds18b20_set_output(LOW);
        udelay(60);
        ds18b20_set_output(HIGH);
        udelay(2);
    }
    local_irq_enable();
}

/* Read a bit from DS18B20 */
static int ds18b20_read_bit(void)
{
    u8 bit = 0;
    local_irq_disable();
    ds18b20_set_output(LOW);
    udelay(1);

    ds18b20_set_output(HIGH);
    udelay(1);
    
    ds18b20_set_input();

    if (ds18b20_get_io())
        bit = 1;
    udelay(50);
    local_irq_enable();
    return bit;
}

/* Write a byte to DS18B20 */
static void ds18b20_write_byte(u8 byte)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (byte & 0x01)
            ds18b20_write_bit(1);
        else
            ds18b20_write_bit(0);
        byte >>= 1;
    }
}

/* Read a byte from DS18B20 */
static char ds18b20_read_byte(void)
{
    int i;
    u8 byte = 0;
    for (i = 0; i < 8; i++) {
        if (ds18b20_read_bit())
            byte |= (1 << i);
        else
            byte &= ~(1 << i);
    }
    return byte;
}

/* Initialize DS18B20 sensor */
static int ds18b20_init(void)
{
    int ret = -1;
    ds18b20_set_output(HIGH);
    udelay(1);
    ds18b20_set_output(LOW);
    udelay(500);
    ds18b20_set_output(HIGH);
    udelay(60);
    ds18b20_set_input();
    ret = ds18b20_get_io();
    udelay(240);
    return ret;      
}
 
/* Open function for DS18B20 device */
static int ds18b20_open(struct inode *inode, struct file *filp)
{
    return 0;
}

/* Read function for DS18B20 device */
static ssize_t ds18b20_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt) 
{
    int ret;
    ret = copy_to_user(buf, &ds18b20_device.data[0], 2);
    if (ret)
        return -ENOMEM;
    return ret;
}

static struct file_operations ds18b20_fops = {
    .owner  = THIS_MODULE,
    .open   = ds18b20_open,
    .read   = ds18b20_read,
};

/* Work queue callback function for DS18B20 */
static void ds18b20_work_callback(struct work_struct *work)
{
    int ret = -1;
    ret = ds18b20_init();
    if (ret != 0)
        goto out1;
    
    ds18b20_write_byte(0XCC);
    ds18b20_write_byte(0X44);
    
    ret = ds18b20_init();
    if (ret != 0)
        goto out1;
    
    ds18b20_write_byte(0XCC);
    ds18b20_write_byte(0XBE);
    
    ds18b20_device.data[0] = ds18b20_read_byte();
    ds18b20_device.data[1] = ds18b20_read_byte();
out1:
    return;
}

/* Timer callback function for DS18B20 */
static void ds18b20_timer_callback(struct timer_list *arg)
{
    schedule_work(&ds18b20_device.work);
    mod_timer(&ds18b20_device.timer, jiffies + (1000 * HZ / 1000));
}

/* Request GPIO for DS18B20 */
static int ds18b20_request_gpio(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    int ret;
    
    ds18b20_device.gpio = of_get_named_gpio(dev->of_node, "ds18b20-gpio", 0);
    if (!gpio_is_valid(ds18b20_device.gpio)) {
        dev_err(dev, "Failed to get gpio");
        return -EINVAL;
    }

    ret = devm_gpio_request(dev, ds18b20_device.gpio, "DS18B20 Gpio");
    if (ret) {
        dev_err(dev, "Failed to request gpio");
        return ret;
    }

    return 0;
}

/* Probe function for DS18B20 driver */
static int ds18b20_probe(struct platform_device *pdev)
{
    struct miscdevice *mdev;
    int ret;

    dev_info(&pdev->dev, "ds18b20 device and driver matched successfully!\n");
    
    ret = ds18b20_request_gpio(pdev);
    if (ret)
        return ret;

    mdev = &ds18b20_device.mdev;
    mdev->name = "ds18b20";
    mdev->minor = MISC_DYNAMIC_MINOR;
    mdev->fops = &ds18b20_fops;

    timer_setup(&ds18b20_device.timer, ds18b20_timer_callback, 0);
    ds18b20_device.timer.expires = jiffies + msecs_to_jiffies(1000);
    add_timer(&ds18b20_device.timer);

    INIT_WORK(&ds18b20_device.work, ds18b20_work_callback);

    return misc_register(mdev);
}

/* Remove function for DS18B20 driver */
static int ds18b20_remove(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "DS18B20 driver has been removed!\n");

    misc_deregister(&ds18b20_device.mdev);
    del_timer(&ds18b20_device.timer);
    cancel_work_sync(&ds18b20_device.work);
    return 0;
}

/* Device tree compatible ID table */
static const struct of_device_id ds18b20_of_match[] = {
    { .compatible = "alientek,ds18b20" },
    { /* Sentinel */ }
};

/* Platform driver structure */
static struct platform_driver ds18b20_driver = {
    .driver = {
        .name           = "ds18b20",
        .of_match_table = ds18b20_of_match,
    },
    .probe      = ds18b20_probe,
    .remove     = ds18b20_remove,
};

module_platform_driver(ds18b20_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
