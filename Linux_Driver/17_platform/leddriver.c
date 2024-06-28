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
#include <linux/irq.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/platform_device.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define LEDDEV_CNT		1			/* Number of devices */
#define LEDDEV_NAME		"platled"	/* Device name */
#define LEDOFF 			0
#define LEDON 			1

/* Mapped virtual addresses for registers */
static void __iomem *MPU_AHB4_PERIPH_RCC_PI;
static void __iomem *GPIOI_MODER_PI;
static void __iomem *GPIOI_OTYPER_PI;
static void __iomem *GPIOI_OSPEEDR_PI;
static void __iomem *GPIOI_PUPDR_PI;
static void __iomem *GPIOI_BSRR_PI;

/* LED device structure */
struct leddev_dev {
	dev_t devid;
	struct cdev cdev;
	struct class *class;
	struct device *device;
};

struct leddev_dev leddev;

/* Function to switch LED on/off */
void led_switch(u8 sta)
{
	u32 val = 0;
	if (sta == LEDON) {
		val = readl(GPIOI_BSRR_PI);
		val |= (1 << 16);
		writel(val, GPIOI_BSRR_PI);
	} else if (sta == LEDOFF) {
		val = readl(GPIOI_BSRR_PI);
		val |= (1 << 0);
		writel(val, GPIOI_BSRR_PI);
	}
}

/* Function to unmap resources */
void led_unmap(void)
{
	iounmap(MPU_AHB4_PERIPH_RCC_PI);
	iounmap(GPIOI_MODER_PI);
	iounmap(GPIOI_OTYPER_PI);
	iounmap(GPIOI_OSPEEDR_PI);
	iounmap(GPIOI_PUPDR_PI);
	iounmap(GPIOI_BSRR_PI);
}

/* Open function */
static int led_open(struct inode *inode, struct file *filp)
{
	return 0;
}

/* Write function */
static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
	int retvalue;
	unsigned char databuf[1];
	unsigned char ledstat;
	
	retvalue = copy_from_user(databuf, buf, cnt);
	if (retvalue < 0) {
		printk("Kernel write failed!\r\n");
		return -EFAULT;
	}

	ledstat = databuf[0]; /* Get status value */
	if (ledstat == LEDON) {
		led_switch(LEDON); /* Turn on LED */
	} else if (ledstat == LEDOFF) {
		led_switch(LEDOFF); /* Turn off LED */
	}

	return 0;
}

/* Device operations */
static struct file_operations led_fops = {
	.owner = THIS_MODULE,
	.open = led_open,
	.write = led_write,
};

/* Probe function for platform driver */
static int led_probe(struct platform_device *dev)
{
	int i = 0, ret;
	int ressize[6];
	u32 val = 0;
	struct resource *ledsource[6];

	printk("LED driver and device have matched!\r\n");

	/* 1. Get resources */
	for (i = 0; i < 6; i++) {
		ledsource[i] = platform_get_resource(dev, IORESOURCE_MEM, i);
		if (!ledsource[i]) {
			dev_err(&dev->dev, "No MEM resource for always on\n");
			return -ENXIO;
		}
		ressize[i] = resource_size(ledsource[i]);
	}

	/* 2. Initialize LED */
	/* Map register addresses */
	MPU_AHB4_PERIPH_RCC_PI = ioremap(ledsource[0]->start, ressize[0]);
	GPIOI_MODER_PI = ioremap(ledsource[1]->start, ressize[1]);
	GPIOI_OTYPER_PI = ioremap(ledsource[2]->start, ressize[2]);
	GPIOI_OSPEEDR_PI = ioremap(ledsource[3]->start, ressize[3]);
	GPIOI_PUPDR_PI = ioremap(ledsource[4]->start, ressize[4]);
	GPIOI_BSRR_PI = ioremap(ledsource[5]->start, ressize[5]);

	/* 3. Enable PI clock */
	val = readl(MPU_AHB4_PERIPH_RCC_PI);
	val &= ~(0X1 << 8); /* Clear previous settings */
	val |= (0X1 << 8);  /* Set new value */
	writel(val, MPU_AHB4_PERIPH_RCC_PI);

	/* 4. Set PI0 general output mode */
	val = readl(GPIOI_MODER_PI);
	val &= ~(0X3 << 0); /* Clear bit0:1 */
	val |= (0X1 << 0);  /* Set bit0:1 to 01 */
	writel(val, GPIOI_MODER_PI);

	/* 5. Set PI0 to push-pull mode */
	val = readl(GPIOI_OTYPER_PI);
	val &= ~(0X1 << 0); /* Clear bit0 */
	writel(val, GPIOI_OTYPER_PI);

	/* 6. Set PI0 to high speed */
	val = readl(GPIOI_OSPEEDR_PI);
	val &= ~(0X3 << 0); /* Clear bit0:1 */
	val |= (0x2 << 0); /* Set bit0:1 to 10 */
	writel(val, GPIOI_OSPEEDR_PI);

	/* 7. Set PI0 to pull-up */
	val = readl(GPIOI_PUPDR_PI);
	val &= ~(0X3 << 0); /* Clear bit0:1 */
	val |= (0x1 << 0); /* Set bit0:1 to 01 */
	writel(val, GPIOI_PUPDR_PI);

	/* 8. Turn off LED by default */
	val = readl(GPIOI_BSRR_PI);
	val |= (0x1 << 0);
	writel(val, GPIOI_BSRR_PI);

	/* Register character device driver */
	/* 1. Allocate device number */
	ret = alloc_chrdev_region(&leddev.devid, 0, LEDDEV_CNT, LEDDEV_NAME);
	if (ret < 0)
		goto fail_map;

	/* 2. Initialize cdev */
	leddev.cdev.owner = THIS_MODULE;
	cdev_init(&leddev.cdev, &led_fops);

	/* 3. Add cdev */
	ret = cdev_add(&leddev.cdev, leddev.devid, LEDDEV_CNT);
	if (ret < 0)
		goto del_unregister;

	/* 4. Create class */
	leddev.class = class_create(THIS_MODULE, LEDDEV_NAME);
	if (IS_ERR(leddev.class)) {
		goto del_cdev;
	}

	/* 5. Create device */
	leddev.device = device_create(leddev.class, NULL, leddev.devid, NULL, LEDDEV_NAME);
	if (IS_ERR(leddev.device)) {
		goto destroy_class;
	}
	return 0;

destroy_class:
	class_destroy(leddev.class);
del_cdev:
	cdev_del(&leddev.cdev);
del_unregister:
	unregister_chrdev_region(leddev.devid, LEDDEV_CNT);
fail_map:
	led_unmap();
	return -EIO;
}

/* Remove function for platform driver */
static int led_remove(struct platform_device *dev)
{
	led_unmap(); /* Unmap resources */
	cdev_del(&leddev.cdev); /* Delete cdev */
	unregister_chrdev_region(leddev.devid, LEDDEV_CNT); /* Unregister device number */
	device_destroy(leddev.class, leddev.devid); /* Destroy device */
	class_destroy(leddev.class); /* Destroy class */
	return 0;
}

/* Platform driver structure */
static struct platform_driver led_driver = {
	.driver		= {
		.name	= "stm32mp1-led", /* Driver name used for matching with devices */
	},
	.probe		= led_probe,
	.remove		= led_remove,
};

/* Module initialization function */
static int __init leddriver_init(void)
{
	return platform_driver_register(&led_driver);
}

/* Module exit function */
static void __exit leddriver_exit(void)
{
	platform_driver_unregister(&led_driver);
}

module_init(leddriver_init);
module_exit(leddriver_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
