#include <linux/spi/spi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include "icm20608reg.h"
#include <linux/gpio.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>

#define ICM20608_CNT    1
#define ICM20608_NAME   "icm20608"

struct icm20608_dev {
    struct spi_device *spi;
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    signed int gyro_x_adc;
    signed int gyro_y_adc;
    signed int gyro_z_adc;
    signed int accel_x_adc;
    signed int accel_y_adc;
    signed int accel_z_adc;
    signed int temp_adc;
};

/**
 * icm20608_read_regs - Read multiple registers from the ICM20608
 * @dev: Pointer to the ICM20608 device structure
 * @reg: Register address to start reading from
 * @buf: Buffer to store the read data
 * @len: Number of bytes to read
 * 
 * Returns: 0 on success, negative error code on failure
 */
static int icm20608_read_regs(struct icm20608_dev *dev, u8 reg, void *buf, int len)
{
    int ret = -1;
    unsigned char txdata[1];
    unsigned char *rxdata;
    struct spi_message m;
    struct spi_transfer *t;
    struct spi_device *spi = (struct spi_device *)dev->spi;

    t = kzalloc(sizeof(struct spi_transfer), GFP_KERNEL);
    if (!t) {
        return -ENOMEM;
    }
    rxdata = kzalloc(sizeof(char) * len, GFP_KERNEL);
    if (!rxdata) {
        goto out1;
    }

    txdata[0] = reg | 0x80;  // Set the highest bit for reading
    t->tx_buf = txdata;
    t->rx_buf = rxdata;
    t->len = len + 1;
    spi_message_init(&m);
    spi_message_add_tail(t, &m);
    ret = spi_sync(spi, &m);
    if (ret) {
        goto out2;
    }

    memcpy(buf, rxdata + 1, len);

out2:
    kfree(rxdata);
out1:
    kfree(t);

    return ret;
}

/**
 * icm20608_write_regs - Write multiple registers to the ICM20608
 * @dev: Pointer to the ICM20608 device structure
 * @reg: Register address to start writing to
 * @buf: Buffer containing the data to write
 * @len: Number of bytes to write
 * 
 * Returns: 0 on success, negative error code on failure
 */
static s32 icm20608_write_regs(struct icm20608_dev *dev, u8 reg, u8 *buf, u8 len)
{
    int ret = -1;
    unsigned char *txdata;
    struct spi_message m;
    struct spi_transfer *t;
    struct spi_device *spi = (struct spi_device *)dev->spi;

    t = kzalloc(sizeof(struct spi_transfer), GFP_KERNEL);
    if (!t) {
        return -ENOMEM;
    }

    txdata = kzalloc(sizeof(char) + len, GFP_KERNEL);
    if (!txdata) {
        goto out1;
    }

    *txdata = reg & ~0x80;  // Clear the highest bit for writing
    memcpy(txdata + 1, buf, len);
    t->tx_buf = txdata;
    t->len = len + 1;
    spi_message_init(&m);
    spi_message_add_tail(t, &m);
    ret = spi_sync(spi, &m);
    if (ret) {
        goto out2;
    }

out2:
    kfree(txdata);
out1:
    kfree(t);
    return ret;
}

/**
 * icm20608_read_onereg - Read a single register from the ICM20608
 * @dev: Pointer to the ICM20608 device structure
 * @reg: Register address to read
 * 
 * Returns: The value read from the register
 */
static unsigned char icm20608_read_onereg(struct icm20608_dev *dev, u8 reg)
{
    u8 data = 0;
    icm20608_read_regs(dev, reg, &data, 1);
    return data;
}

/**
 * icm20608_write_onereg - Write a single register to the ICM20608
 * @dev: Pointer to the ICM20608 device structure
 * @reg: Register address to write
 * @value: Value to write to the register
 */
static void icm20608_write_onereg(struct icm20608_dev *dev, u8 reg, u8 value)
{
    u8 buf = value;
    icm20608_write_regs(dev, reg, &buf, 1);
}

/**
 * icm20608_readdata - Read all sensor data from the ICM20608
 * @dev: Pointer to the ICM20608 device structure
 */
void icm20608_readdata(struct icm20608_dev *dev)
{
    unsigned char data[14];
    icm20608_read_regs(dev, ICM20_ACCEL_XOUT_H, data, 14);

    dev->accel_x_adc = (signed short)((data[0] << 8) | data[1]);
    dev->accel_y_adc = (signed short)((data[2] << 8) | data[3]);
    dev->accel_z_adc = (signed short)((data[4] << 8) | data[5]);
    dev->temp_adc    = (signed short)((data[6] << 8) | data[7]);
    dev->gyro_x_adc  = (signed short)((data[8] << 8) | data[9]);
    dev->gyro_y_adc  = (signed short)((data[10] << 8) | data[11]);
    dev->gyro_z_adc  = (signed short)((data[12] << 8) | data[13]);
}

/**
 * icm20608_open - Open function for the ICM20608 device
 * @inode: Pointer to the inode structure
 * @filp: Pointer to the file structure
 * 
 * Returns: 0 on success
 */
static int icm20608_open(struct inode *inode, struct file *filp)
{
    return 0;
}

/**
 * icm20608_read - Read function for the ICM20608 device
 * @filp: Pointer to the file structure
 * @buf: User buffer to store the read data
 * @cnt: Number of bytes to read
 * @off: Pointer to the offset value
 * 
 * Returns: Number of bytes read on success, negative error code on failure
 */
static ssize_t icm20608_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
    signed int data[7];
    long err = 0;
    struct cdev *cdev = filp->f_path.dentry->d_inode->i_cdev;
    struct icm20608_dev *dev = container_of(cdev, struct icm20608_dev, cdev);

    icm20608_readdata(dev);
    data[0] = dev->gyro_x_adc;
    data[1] = dev->gyro_y_adc;
    data[2] = dev->gyro_z_adc;
    data[3] = dev->accel_x_adc;
    data[4] = dev->accel_y_adc;
    data[5] = dev->accel_z_adc;
    data[6] = dev->temp_adc;
    err = copy_to_user(buf, data, sizeof(data));
    return 0;
}

/**
 * icm20608_release - Release function for the ICM20608 device
 * @inode: Pointer to the inode structure
 * @filp: Pointer to the file structure
 * 
 * Returns: 0 on success
 */
static int icm20608_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static const struct file_operations icm20608_ops = {
    .owner = THIS_MODULE,
    .open = icm20608_open,
    .read = icm20608_read,
    .release = icm20608_release,
};

/**
 * icm20608_reginit - Initialize the registers of the ICM20608
 * @dev: Pointer to the ICM20608 device structure
 */
void icm20608_reginit(struct icm20608_dev *dev)
{
    u8 value = 0;

    // Reset the device
    icm20608_write_onereg(dev, ICM20_PWR_MGMT_1, 0x80);
    mdelay(50);
    icm20608_write_onereg(dev, ICM20_PWR_MGMT_1, 0x01);
    mdelay(50);

    // Read the WHO_AM_I register
    value = icm20608_read_onereg(dev, ICM20_WHO_AM_I);
    printk("ICM20608 ID = %#X\r\n", value);

    // Set sample rate divider to 0 (output rate = internal sample rate)
    icm20608_write_onereg(dev, ICM20_SMPLRT_DIV, 0x00);
    // Set DLPF_CFG to 3 (44Hz, 4.9ms delay)
    icm20608_write_onereg(dev, ICM20_CONFIG, 0x03);
    // Set Gyroscope to ±2000 degrees/sec
    icm20608_write_onereg(dev, ICM20_GYRO_CONFIG, 0x18);
    // Set Accelerometer to ±16g
    icm20608_write_onereg(dev, ICM20_ACCEL_CONFIG, 0x18);
    // Set Accelerometer to 44Hz bandwidth, 4.9ms delay
    icm20608_write_onereg(dev, ICM20_ACCEL_CONFIG2, 0x03);
    // Disable FIFO
    icm20608_write_onereg(dev, ICM20_FIFO_EN, 0x00);
    // Disable I2C interface
    icm20608_write_onereg(dev, ICM20_USER_CTRL, 0x10);
    // Enable Sensor
    icm20608_write_onereg(dev, ICM20_PWR_MGMT_2, 0x00);
}

/**
 * icm20608_probe - Probe function for the SPI driver
 * @spi: Pointer to the SPI device structure
 * 
 * Returns: 0 on success, negative error code on failure
 */
static int icm20608_probe(struct spi_device *spi)
{
    int ret = 0;
    struct icm20608_dev *icm20608dev;

    // Allocate memory for the device structure
    icm20608dev = devm_kzalloc(&spi->dev, sizeof(struct icm20608_dev), GFP_KERNEL);
    if (!icm20608dev) {
        return -ENOMEM;
    }

    // Initialize the device structure
    icm20608dev->spi = spi;
    icm20608dev->nd = spi->dev.of_node;

    // Allocate a character device number
    alloc_chrdev_region(&icm20608dev->devid, 0, ICM20608_CNT, ICM20608_NAME);

    // Initialize the character device
    icm20608dev->cdev.owner = THIS_MODULE;
    cdev_init(&icm20608dev->cdev, &icm20608_ops);
    cdev_add(&icm20608dev->cdev, icm20608dev->devid, ICM20608_CNT);

    // Create a device class
    icm20608dev->class = class_create(THIS_MODULE, ICM20608_NAME);
    if (IS_ERR(icm20608dev->class)) {
        ret = PTR_ERR(icm20608dev->class);
        goto destroy_class;
    }

    // Create a device node
    icm20608dev->device = device_create(icm20608dev->class, NULL, icm20608dev->devid, NULL, ICM20608_NAME);
    if (IS_ERR(icm20608dev->device)) {
        ret = PTR_ERR(icm20608dev->device);
        goto destroy_device;
    }

    // Initialize the ICM20608 registers
    icm20608_reginit(icm20608dev);

    // Set the driver data
    spi_set_drvdata(spi, icm20608dev);

    return 0;

destroy_device:
    device_destroy(icm20608dev->class, icm20608dev->devid);
destroy_class:
    class_destroy(icm20608dev->class);
    cdev_del(&icm20608dev->cdev);
    unregister_chrdev_region(icm20608dev->devid, ICM20608_CNT);

    return ret;
}

/**
 * icm20608_remove - Remove function for the SPI driver
 * @spi: Pointer to the SPI device structure
 * 
 * Returns: 0 on success
 */
static int icm20608_remove(struct spi_device *spi)
{
    struct icm20608_dev *icm20608dev = spi_get_drvdata(spi);

    cdev_del(&icm20608dev->cdev);
    unregister_chrdev_region(icm20608dev->devid, ICM20608_CNT);
    device_destroy(icm20608dev->class, icm20608dev->devid);
    class_destroy(icm20608dev->class);

    return 0;
}

static const struct spi_device_id icm20608_id[] = {
    {"alientek,icm20608", 0},
    {}
};

static const struct of_device_id icm20608_of_match[] = {
    { .compatible = "alientek,icm20608" },
    { }
};

static struct spi_driver icm20608_driver = {
    .probe = icm20608_probe,
    .remove = icm20608_remove,
    .driver = {
        .owner = THIS_MODULE,
        .name = "icm20608",
        .of_match_table = icm20608_of_match,
    },
    .id_table = icm20608_id,
};

static int __init icm20608_init(void)
{
    return spi_register_driver(&icm20608_driver);
}

static void __exit icm20608_exit(void)
{
    spi_unregister_driver(&icm20608_driver);
}

module_init(icm20608_init);
module_exit(icm20608_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
