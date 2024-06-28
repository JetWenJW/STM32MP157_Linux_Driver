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
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define NEWCHRLED_CNT		1
#define NEWCHRLED_NAME		"newchrled"
#define LEDOFF 				0
#define LEDON 				1

#define PERIPH_BASE     		(0x40000000)
#define MPU_AHB4_PERIPH_BASE	(PERIPH_BASE + 0x10000000)
#define RCC_BASE        		(MPU_AHB4_PERIPH_BASE + 0x0000)	
#define RCC_MP_AHB4ENSETR		(RCC_BASE + 0XA28)
#define GPIOI_BASE				(MPU_AHB4_PERIPH_BASE + 0xA000)	
#define GPIOI_MODER     	 	(GPIOI_BASE + 0x0000)	
#define GPIOI_OTYPER      		(GPIOI_BASE + 0x0004)	
#define GPIOI_OSPEEDR      		(GPIOI_BASE + 0x0008)	
#define GPIOI_PUPDR      		(GPIOI_BASE + 0x000C)	
#define GPIOI_BSRR      		(GPIOI_BASE + 0x0018)

static void __iomem *MPU_AHB4_PERIPH_RCC_PI;
static void __iomem *GPIOI_MODER_PI;
static void __iomem *GPIOI_OTYPER_PI;
static void __iomem *GPIOI_OSPEEDR_PI;
static void __iomem *GPIOI_PUPDR_PI;
static void __iomem *GPIOI_BSRR_PI;

struct newchrled_dev 
{
	dev_t devid;
	struct cdev cdev;
	struct class *class;
	struct device *device;
	int major;
	int minor;
};

struct newchrled_dev newchrled;

void led_switch(u8 sta)
{
	u32 val = 0;
	if (sta == LEDON) 
	{
		val = readl(GPIOI_BSRR_PI);
		val |= (1 << 16);	// Set GPIOI_BSRR_PI bit 16 for LED ON
		writel(val, GPIOI_BSRR_PI);
	} 
	else if (sta == LEDOFF) 
	{
		val = readl(GPIOI_BSRR_PI);
		val |= (1 << 0);	// Set GPIOI_BSRR_PI bit 0 for LED OFF
		writel(val, GPIOI_BSRR_PI);
	}	
}

void led_unmap(void)
{
	iounmap(MPU_AHB4_PERIPH_RCC_PI);
	iounmap(GPIOI_MODER_PI);
	iounmap(GPIOI_OTYPER_PI);
	iounmap(GPIOI_OSPEEDR_PI);
	iounmap(GPIOI_PUPDR_PI);
	iounmap(GPIOI_BSRR_PI);
}

static int led_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &newchrled; // Set private data of the file to point to newchrled structure
	return 0;
}

static ssize_t led_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
	return 0; // Reading operation not implemented for this driver
}

static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
	int retvalue;
	unsigned char databuf[1];
	unsigned char ledstat;

	retvalue = copy_from_user(databuf, buf, cnt);
	if (retvalue < 0) 
	{
		printk("kernel write failed!\r\n");
		return -EFAULT;
	}

	ledstat = databuf[0];

	if (ledstat == LEDON) 
	{	
		led_switch(LEDON);	// Turn on LED
	} 
	else if (ledstat == LEDOFF) 
	{
		led_switch(LEDOFF);	// Turn off LED
	}
	return 0;
}

static int led_release(struct inode *inode, struct file *filp)
{
	return 0; // Release operation not needed for this driver
}

static struct file_operations newchrled_fops = 
{
	.owner = THIS_MODULE,
	.open = led_open,
	.read = led_read,
	.write = led_write,
	.release = led_release,
};

static int __init led_init(void)
{
	u32 val = 0;
	int ret;

	// Memory map GPIO registers
	MPU_AHB4_PERIPH_RCC_PI = ioremap(RCC_MP_AHB4ENSETR, 4);
	GPIOI_MODER_PI = ioremap(GPIOI_MODER, 4);
	GPIOI_OTYPER_PI = ioremap(GPIOI_OTYPER, 4);
	GPIOI_OSPEEDR_PI = ioremap(GPIOI_OSPEEDR, 4);
	GPIOI_PUPDR_PI = ioremap(GPIOI_PUPDR, 4);
	GPIOI_BSRR_PI = ioremap(GPIOI_BSRR, 4);

	// Enable GPIO clock for GPIOI
	val = readl(MPU_AHB4_PERIPH_RCC_PI);
	val &= ~(0X1 << 8);
	val |= (0X1 << 8);
	writel(val, MPU_AHB4_PERIPH_RCC_PI);

	// Configure GPIOI_PIN_0 as output
	val = readl(GPIOI_MODER_PI);
	val &= ~(0X3 << 0);
	val |= (0X1 << 0);
	writel(val, GPIOI_MODER_PI);

	// Configure GPIOI_PIN_0 as push-pull
	val = readl(GPIOI_OTYPER_PI);
	val &= ~(0X1 << 0);
	writel(val, GPIOI_OTYPER_PI);

	// Set GPIOI_PIN_0 to high speed
	val = readl(GPIOI_OSPEEDR_PI);
	val &= ~(0X3 << 0);
	val |= (0x2 << 0);
	writel(val, GPIOI_OSPEEDR_PI);

	// Enable pull-up for GPIOI_PIN_0
	val = readl(GPIOI_PUPDR_PI);
	val &= ~(0X3 << 0);
	val |= (0x1 << 0);
	writel(val, GPIOI_PUPDR_PI);

	// Turn off LED initially
	val = readl(GPIOI_BSRR_PI);
	val |= (0x1 << 0);
	writel(val, GPIOI_BSRR_PI);

	// Register character device
	if (newchrled.major) 
	{
		newchrled.devid = MKDEV(newchrled.major, 0);
		ret = register_chrdev_region(newchrled.devid, NEWCHRLED_CNT, NEWCHRLED_NAME);
		if (ret < 0) 
		{
			pr_err("cannot register %s char driver [ret=%d]\n", NEWCHRLED_NAME, NEWCHRLED_CNT);
			goto fail_map;
		}
	} 
	else 
	{
		ret = alloc_chrdev_region(&newchrled.devid, 0, NEWCHRLED_CNT, NEWCHRLED_NAME);
		if (ret < 0) 
		{
			pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", NEWCHRLED_NAME, ret);
			goto fail_map;
		}
		newchrled.major = MAJOR(newchrled.devid);
		newchrled.minor = MINOR(newchrled.devid);
	}
	printk("newcheled major=%d,minor=%d\r\n", newchrled.major, newchrled.minor);	
	
	newchrled.cdev.owner = THIS_MODULE;
	cdev_init(&newchrled.cdev, &newchrled_fops);
	
	ret = cdev_add(&newchrled.cdev, newchrled.devid, NEWCHRLED_CNT);
	if (ret < 0)
	{
		goto del_unregister;
	}
	
	newchrled.class = class_create(THIS_MODULE, NEWCHRLED_NAME);
	if (IS_ERR(newchrled.class))
	{
		goto del_cdev;
	}

	newchrled.device = device_create(newchrled.class, NULL, newchrled.devid, NULL, NEWCHRLED_NAME);
	if (IS_ERR(newchrled.device)) 
	{
		goto destroy_class;
	}
	
	return 0;

destroy_class:
	class_destroy(newchrled.class);
del_cdev:
	cdev_del(&newchrled.cdev);
del_unregister:
	unregister_chrdev_region(newchrled.devid, NEWCHRLED_CNT);
fail_map:
	led_unmap();
	return -EIO;

}

static void __exit led_exit(void)
{
	led_unmap();
	cdev_del(&newchrled.cdev);
	unregister_chrdev_region(newchrled.devid, NEWCHRLED_CNT);

	device_destroy(newchrled.class, newchrled.devid);
	class_destroy(newchrled.class);
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
