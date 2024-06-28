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
#include <linux/regmap.h>

#define AP3216C_CNT    1
#define AP3216C_NAME   "ap3216c"

/* ap3216c device structure */
struct ap3216c_dev {
    struct i2c_client *client;
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    unsigned short ir, als, ps;  /* Sensor data */
    struct regmap *regmap;
    struct regmap_config regmap_config;
};

/*
 * @description: Reads a specific register value from the ap3216c device
 * @param - dev:  ap3216c device
 * @param - reg:  Register to read
 * @return:       Value read from the register
 */
static unsigned char ap3216c_read_reg(struct ap3216c_dev *dev, u8 reg)
{
    u8 ret;
    unsigned int data;

    ret = regmap_read(dev->regmap, reg, &data);
    return (u8)data;
}

/*
 * @description: Writes a specific value to a specific register of the ap3216c device
 * @param - dev:  ap3216c device
 * @param - reg:  Register to write to
 * @param - data: Value to write to the register
 */
static void ap3216c_write_reg(struct ap3216c_dev *dev, u8 reg, u8 data)
{
    regmap_write(dev->regmap, reg, data);
}

/*
 * @description: Reads data from the AP3216C sensors (ALS, PS, and IR)
 * @param - dev:  ap3216c device
 */
void ap3216c_readdata(struct ap3216c_dev *dev)
{
    unsigned char i = 0;
    unsigned char buf[6];
    
    /* Loop to read data from all sensors */
    for(i = 0; i < 6; i++) {
        buf[i] = ap3216c_read_reg(dev, AP3216C_IRDATALOW + i);    
    }

    /* Process IR data */
    if(buf[0] & 0X80)
        dev->ir = 0;                    
    else
        dev->ir = ((unsigned short)buf[1] << 2) | (buf[0] & 0X03);             
    
    /* Process ALS data */
    dev->als = ((unsigned short)buf[3] << 8) | buf[2];    
    
    /* Process PS data */
    if(buf[4] & 0x40)
        dev->ps = 0;                                                    
    else
        dev->ps = ((unsigned short)(buf[5] & 0X3F) << 4) | (buf[4] & 0X0F); 
}

/*
 * @description: Opens the device
 * @param - inode:  Inode pointer
 * @param - filp:   File pointer
 * @return:         0 on success; other values on failure
 */
static int ap3216c_open(struct inode *inode, struct file *filp)
{
    /* Get the device structure from the file pointer */
    struct cdev *cdev = filp->f_path.dentry->d_inode->i_cdev;
    struct ap3216c_dev *ap3216cdev = container_of(cdev, struct ap3216c_dev, cdev);

    /* Initialize AP3216C */
    ap3216c_write_reg(ap3216cdev, AP3216C_SYSTEMCONG, 0x04);  /* Reset AP3216C */
    mdelay(50);  /* AP3216C reset delay */
    ap3216c_write_reg(ap3216cdev, AP3216C_SYSTEMCONG, 0X03);  /* Enable ALS, PS, and IR */
    return 0;
}

/*
 * @description: Reads data from the device
 * @param - filp:   File pointer
 * @param - buf:    Buffer to return data to user space
 * @param - cnt:    Data length to read
 * @param - off:    Offset relative to file start address
 * @return:         Number of bytes read; negative value on failure
 */
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

/*
 * @description: Closes/Releases the device
 * @param - filp:   File pointer
 * @return:         0 on success; other values on failure
 */
static int ap3216c_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/* AP3216C file operations */
static const struct file_operations ap3216c_ops = {
    .owner = THIS_MODULE,
    .open = ap3216c_open,
    .read = ap3216c_read,
    .release = ap3216c_release,
};

/*
 * @description: i2c driver probe function, executed when driver matches the device
 * @param - client: i2c device
 * @param - id:     i2c device ID
 * @return:         0 on success; negative values on failure
 */
static int ap3216c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret;
    struct ap3216c_dev *ap3216cdev;
    ap3216cdev = devm_kzalloc(&client->dev, sizeof(*ap3216cdev), GFP_KERNEL);
    if(!ap3216cdev)
        return -ENOMEM;

    /* Initialize regmap configuration */
    ap3216cdev->regmap_config.reg_bits = 8;  /* Register length: 8 bits */
    ap3216cdev->regmap_config.val_bits = 8;  /* Value length: 8 bits */

    /* Initialize I2C regmap */
    ap3216cdev->regmap = regmap_init_i2c(client, &ap3216cdev->regmap_config);
    if (IS_ERR(ap3216cdev->regmap)) {
        return PTR_ERR(ap3216cdev->regmap);
    }    
    
    /* Register character device driver */
    /* 1. Create device number */
    ret = alloc_chrdev_region(&ap3216cdev->devid, 0, AP3216C_CNT, AP3216C_NAME);
    if(ret < 0) {
        pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", AP3216C_NAME, ret);
        goto del_regmap;
    }

    /* 2. Initialize cdev */
    ap3216cdev->cdev.owner = THIS_MODULE;
    cdev_init(&ap3216cdev->cdev, &ap3216c_ops);
    
    /* 3. Add cdev */
    ret = cdev_add(&ap3216cdev->cdev, ap3216cdev->devid, AP3216C_CNT);
    if(ret < 0) {
        goto del_unregister;
    }
    
    /* 4. Create class */
    ap3216cdev->class = class_create(THIS_MODULE, AP3216C_NAME);
    if (IS_ERR(ap3216cdev->class)) {
        goto del_cdev;
    }

    /* 5. Create device */
    ap3216cdev->device = device_create(ap3216cdev->class, NULL, ap3216cdev->devid, NULL, AP3216C_NAME);
    if (IS_ERR(ap3216cdev->device)) {
        goto destroy_class;
    }
    ap3216cdev->client = client;
    /* Save ap3216cdev structure */
    i2c_set_clientdata(client, ap3216cdev);

    return 0;
destroy_class:
    device_destroy(ap3216cdev->class, ap3216cdev->devid);
del_cdev:
    cdev_del(&ap3216cdev->cdev);
del_unregister:
    unregister_chrdev_region(ap3216cdev->devid, AP3216C_CNT);
del_regmap:
    regmap_exit(ap3216cdev->regmap);
    return -EIO;
}

/*
 * @description: i2c driver remove function, executed when driver and device unbound
 * @param - client: i2c device
 * @return:         0 on success; negative values on failure
 */
static int ap3216c_remove(struct i2c_client *client)
{
    struct ap3216c_dev *ap3216cdev = i2c_get_clientdata(client);
    cdev_del(&ap3216cdev->cdev); /* Delete cdev */
    unregister_chrdev_region(ap3216cdev->devid, AP3216C_CNT); /* Unregister device number */
    device_destroy(ap3216cdev->class, ap3216cdev->devid);
    class_destroy(ap3216cdev->class); /* Destroy class */
    regmap_exit(ap3216cdev->regmap);
    return 0;
}

/* Match list for i2c device ID */
static const struct i2c_device_id ap3216c_id[] = {
    {"alientek,ap3216c", 0},  
    {}
};

/* Match list for device tree */
static const struct of_device_id ap3216c_of_match[] = {
    { .compatible = "alientek,ap3216c" },
    { /* Sentinel */ }
};

/* i2c driver structure */
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

/*
 * @description: Driver initialization function
 * @param: None
 * @return: 0 on success; negative values on failure
 */
static int __init ap3216c_init(void)
{
    int ret = 0;

    /* Register i2c driver */
    ret = i2c_add_driver(&ap3216c_driver);
    return ret;
}

/*
 * @description: Driver exit function
 * @param: None
 * @return: None
 */
static void __exit ap3216c_exit(void)
{
    /* Unregister i2c driver */
    i2c_del_driver(&ap3216c_driver);
}

module_init(ap3216c_init);
module_exit(ap3216c_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
