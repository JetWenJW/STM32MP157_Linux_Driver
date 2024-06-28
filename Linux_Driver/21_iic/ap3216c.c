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
#include <linux/of_gpio.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include "ap3216creg.h"

#define AP3216C_CNT     1
#define AP3216C_NAME    "ap3216c"

// Structure for the AP3216C device
struct ap3216c_dev {
    struct i2c_client *client;    /* i2c device */
    dev_t devid;                  /* device number */
    struct cdev cdev;             /* cdev structure */
    struct class *class;          /* device class */
    struct device *device;        /* device */
    struct device_node *nd;       /* device node */
    unsigned short ir, als, ps;   /* data from three light sensors */
};

// Function to read registers from the AP3216C device
static int ap3216c_read_regs(struct ap3216c_dev *dev, u8 reg, void *val, int len)
{
    int ret;
    struct i2c_msg msg[2];
    struct i2c_client *client = (struct i2c_client *)dev->client;

    // First message to set the register address
    msg[0].addr = client->addr;
    msg[0].flags = 0;
    msg[0].buf = &reg;
    msg[0].len = 1;

    // Second message to read the data
    msg[1].addr = client->addr;
    msg[1].flags = I2C_M_RD;
    msg[1].buf = val;
    msg[1].len = len;

    // Perform the I2C transfer
    ret = i2c_transfer(client->adapter, msg, 2);
    if (ret == 2) {
        ret = 0;
    } else {
        printk("i2c rd failed=%d reg=%06x len=%d\n", ret, reg, len);
        ret = -EREMOTEIO;
    }
    return ret;
}

// Function to write registers to the AP3216C device
static s32 ap3216c_write_regs(struct ap3216c_dev *dev, u8 reg, u8 *buf, u8 len)
{
    u8 b[256];
    struct i2c_msg msg;
    struct i2c_client *client = (struct i2c_client *)dev->client;

    b[0] = reg;
    memcpy(&b[1], buf, len);

    msg.addr = client->addr;
    msg.flags = 0;
    msg.buf = b;
    msg.len = len + 1;

    return i2c_transfer(client->adapter, &msg, 1);
}

// Function to read a single register from the AP3216C device
static unsigned char ap3216c_read_reg(struct ap3216c_dev *dev, u8 reg)
{
    u8 data = 0;

    ap3216c_read_regs(dev, reg, &data, 1);
    return data;
}

// Function to write a single register to the AP3216C device
static void ap3216c_write_reg(struct ap3216c_dev *dev, u8 reg, u8 data)
{
    u8 buf = data;
    ap3216c_write_regs(dev, reg, &buf, 1);
}

// Function to read data from the AP3216C sensors
void ap3216c_readdata(struct ap3216c_dev *dev)
{
    unsigned char i = 0;
    unsigned char buf[6];

    // Read the data from the sensor registers
    for (i = 0; i < 6; i++) {
        buf[i] = ap3216c_read_reg(dev, AP3216C_IRDATALOW + i);
    }

    // Process the IR data
    if (buf[0] & 0X80)
        dev->ir = 0;
    else
        dev->ir = ((unsigned short)buf[1] << 2) | (buf[0] & 0X03);

    // Process the ALS data
    dev->als = ((unsigned short)buf[3] << 8) | buf[2];

    // Process the PS data
    if (buf[4] & 0x40)
        dev->ps = 0;
    else
        dev->ps = ((unsigned short)(buf[5] & 0X3F) << 4) | (buf[4] & 0X0F);
}

// Function to handle the device open operation
static int ap3216c_open(struct inode *inode, struct file *filp)
{
    struct cdev *cdev = filp->f_path.dentry->d_inode->i_cdev;
    struct ap3216c_dev *ap3216cdev = container_of(cdev, struct ap3216c_dev, cdev);

    ap3216c_write_reg(ap3216cdev, AP3216C_SYSTEMCONG, 0x04);
    mdelay(50);
    ap3216c_write_reg(ap3216cdev, AP3216C_SYSTEMCONG, 0X03);
    return 0;
}

// Function to handle the device read operation
static ssize_t ap3216c_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
    short data[3];
    long err = 0;

    struct cdev *cdev = filp->f_path.dentry->d_inode->i_cdev;
    struct ap3216c_dev *dev = container_of(cdev, struct ap3216c_dev, cdev);

    ap3216c_readdata(dev);

    data[0] = dev->ir;
    data[1] = dev->als;
    data[2] = dev->ps;
    err = copy_to_user(buf, data, sizeof(data));
    return 0;
}

// Function to handle the device release operation
static int ap3216c_release(struct inode *inode, struct file *filp)
{
    return 0;
}

// File operations structure for the AP3216C device
static const struct file_operations ap3216c_ops = {
    .owner = THIS_MODULE,
    .open = ap3216c_open,
    .read = ap3216c_read,
    .release = ap3216c_release,
};

// Probe function for the AP3216C device
static int ap3216c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret;
    struct ap3216c_dev *ap3216cdev;

    // Allocate memory for the device structure
    ap3216cdev = devm_kzalloc(&client->dev, sizeof(*ap3216cdev), GFP_KERNEL);
    if (!ap3216cdev)
        return -ENOMEM;

    // Allocate a device number
    ret = alloc_chrdev_region(&ap3216cdev->devid, 0, AP3216C_CNT, AP3216C_NAME);
    if (ret < 0) {
        pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", AP3216C_NAME, ret);
        return -ENOMEM;
    }

    // Initialize the cdev structure
    ap3216cdev->cdev.owner = THIS_MODULE;
    cdev_init(&ap3216cdev->cdev, &ap3216c_ops);

    // Add the cdev to the system
    ret = cdev_add(&ap3216cdev->cdev, ap3216cdev->devid, AP3216C_CNT);
    if (ret < 0) {
        goto del_unregister;
    }

    // Create the device class
    ap3216cdev->class = class_create(THIS_MODULE, AP3216C_NAME);
    if (IS_ERR(ap3216cdev->class)) {
        goto del_cdev;
    }

    // Create the device
    ap3216cdev->device = device_create(ap3216cdev->class, NULL, ap3216cdev->devid, NULL, AP3216C_NAME);
    if (IS_ERR(ap3216cdev->device)) {
        goto destroy_class;
    }

    // Set the client data
    ap3216cdev->client = client;
    i2c_set_clientdata(client, ap3216cdev);

    return 0;

destroy_class:
    device_destroy(ap3216cdev->class, ap3216cdev->devid);
del_cdev:
    cdev_del(&ap3216cdev->cdev);
del_unregister:
    unregister_chrdev_region(ap3216cdev->devid, AP3216C_CNT);
    return -EIO;
}

// Remove function for the AP3216C device
static int ap3216c_remove(struct i2c_client *client)
{
    struct ap3216c_dev *ap3216cdev = i2c_get_clientdata(client);

    cdev_del(&ap3216cdev->cdev);
    unregister_chrdev_region(ap3216cdev->devid, AP3216C_CNT);
    device_destroy(ap3216cdev->class, ap3216cdev->devid);
    class_destroy(ap3216cdev->class);
    return 0;
}

// I2C device ID table
static const struct i2c_device_id ap3216c_id[] = {
    {"alientek,ap3216c", 0},
    {}
};

// Device tree match table
static const struct of_device_id ap3216c_of_match[] = {
    { .compatible = "alientek,ap3216c" },
    { /* Sentinel */ }
};

// I2C driver structure for the AP3216C device
static struct i2c_driver ap3216c_driver = {
    .probe = ap3216c_probe,
    .remove = ap3216c_remove,
    .driver = {
        .owner = THIS_MODULE,
        .name = "ap3216c",
        .of_match_table = ap3216c_of_match,
    },
    .id_table = ap3216c_id,
};

// Module initialization function
static int __init ap3216c_init(void)
{
    int ret = 0;

    ret = i2c_add_driver(&ap3216c_driver);
    return ret;
}

// Module exit function
static void __exit ap3216c_exit(void)
{
    i2c_del_driver(&ap3216c_driver);
}

module_init(ap3216c_init);
module_exit(ap3216c_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
