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
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define DTSLED_CNT		1
#define DTSLED_NAME		"dtsled"
#define LEDOFF 			0
#define LEDON 			1

/* Mapped register virtual addresses */
static void __iomem *MPU_AHB4_PERIPH_RCC_PI;
static void __iomem *GPIOI_MODER_PI;
static void __iomem *GPIOI_OTYPER_PI;
static void __iomem *GPIOI_OSPEEDR_PI;
static void __iomem *GPIOI_PUPDR_PI;
static void __iomem *GPIOI_BSRR_PI;

/* dtsled device structure */
struct dtsled_dev {
	dev_t devid;
	struct cdev cdev;
	struct class *class;
	struct device *device;
	int major;
	int minor;
	struct device_node *nd;
};

struct dtsled_dev dtsled; /* LED device */

/* Function to switch the LED on/off */
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

/* Function to unmap GPIO registers */
void led_unmap(void)
{
	iounmap(MPU_AHB4_PERIPH_RCC_PI);
	iounmap(GPIOI_MODER_PI);
	iounmap(GPIOI_OTYPER_PI);
	iounmap(GPIOI_OSPEEDR_PI);
	iounmap(GPIOI_PUPDR_PI);
	iounmap(GPIOI_BSRR_PI);
}

/* Device open function */
static int led_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &dtsled; /* Set private data */
	return 0;
}

/* Device read function */
static ssize_t led_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
	return 0; /* Not implemented for this device */
}

/* Device write function */
static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
	int retvalue;
	unsigned char databuf[1];
	unsigned char ledstat;

	retvalue = copy_from_user(databuf, buf, cnt);
	if (retvalue < 0) {
		printk("kernel write failed!\r\n");
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

/* Device release function */
static int led_release(struct inode *inode, struct file *filp)
{
	return 0; /* No cleanup needed on release */
}

/* Device operations structure */
static struct file_operations dtsled_fops = {
	.owner = THIS_MODULE,
	.open = led_open,
	.read = led_read,
	.write = led_write,
	.release = led_release,
};

/* Initialization function */
static int __init led_init(void)
{
	u32 val = 0;
	int ret;
	u32 regdata[12];
	const char *str;
	struct property *proper;

	/* Find device tree node */
	dtsled.nd = of_find_node_by_path("/stm32mp1_led");
	if (dtsled.nd == NULL) {
		printk("stm32mp1_led node not found!\r\n");
		return -EINVAL;
	}
	
	proper = of_find_property(dtsled.nd, "compatible", NULL);
	if(proper == NULL) {
		printk("compatible property find failed\r\n");
	} else {
		printk("compatible = %s\r\n", (char*)proper->value);
	}

	/* Read status property */
	ret = of_property_read_string(dtsled.nd, "status", &str);
	if (ret < 0) {
		printk("status read failed!\r\n");
	}

	/* Read reg property */
	ret = of_property_read_u32_array(dtsled.nd, "reg", regdata, 12);
	if (ret < 0) {
		printk("reg property read failed!\r\n");
	}

	/* Map GPIO registers */
	MPU_AHB4_PERIPH_RCC_PI = of_iomap(dtsled.nd, 0);
	GPIOI_MODER_PI = of_iomap(dtsled.nd, 1);
	GPIOI_OTYPER_PI = of_iomap(dtsled.nd, 2);
	GPIOI_OSPEEDR_PI = of_iomap(dtsled.nd, 3);
	GPIOI_PUPDR_PI = of_iomap(dtsled.nd, 4);
	GPIOI_BSRR_PI = of_iomap(dtsled.nd, 5);

	/* Enable PI clock */
	val = readl(MPU_AHB4_PERIPH_RCC_PI);
	val &= ~(0X1 << 8);
	val |= (0X1 << 8);
	writel(val, MPU_AHB4_PERIPH_RCC_PI);

	/* Configure PI0 as general output mode */
	val = readl(GPIOI_MODER_PI);
	val &= ~(0X3 << 0);
	val |= (0X1 << 0);
	writel(val, GPIOI_MODER_PI);

	/* Configure PI0 as push-pull mode */
	val = readl(GPIOI_OTYPER_PI);
	val &= ~(0X1 << 0);
	writel(val, GPIOI_OTYPER_PI);

	/* Configure PI0 as high-speed */
	val = readl(GPIOI_OSPEEDR_PI);
	val &= ~(0X3 << 0);
	val |= (0x2 << 0);
	writel(val, GPIOI_OSPEEDR_PI);

	/* Configure PI0 as pull-up */
	val = readl(GPIOI_PUPDR_PI);
	val &= ~(0X3 << 0);
	val |= (0x1 << 0);
	writel(val, GPIOI_PUPDR_PI);

	/* Turn off LED by default */
	val = readl(GPIOI_BSRR_PI);
	val |= (0x1 << 0);
	writel(val, GPIOI_BSRR_PI);

	/* Register character device driver */
	if (dtsled.major) {
		dtsled.devid = MKDEV(dtsled.major, 0);
		ret = register_chrdev_region(dtsled.devid, DTSLED_CNT, DTSLED_NAME);
		if (ret < 0) {
			pr_err("cannot register %s char driver [ret=%d]\n", DTSLED_NAME, DTSLED_CNT);
			goto fail_map;
		}
	} else {
		ret = alloc_chrdev_region(&dtsled.devid, 0, DTSLED_CNT, DTSLED_NAME);
		if (ret < 0) {
			pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", DTSLED_NAME, ret);
			goto fail_map;
		}
		dtsled.major = MAJOR(dtsled.devid);
		dtsled.minor = MINOR(dtsled.devid);
	}

	/* Initialize cdev */
	dtsled.cdev.owner = THIS_MODULE;
	cdev_init(&dtsled.cdev, &dtsled_fops);

	/* Add cdev */
	ret = cdev_add(&dtsled.cdev, dtsled.devid, DTSLED_CNT);
	if (ret < 0)
		goto del_unregister;

	/* Create class */
	dtsled.class = class_create(THIS_MODULE, DTSLED_NAME);
	if (IS_ERR(dtsled.class)) {
		goto del_cdev;
	}

	/* Create device */
	dtsled.device = device_create(dtsled.class, NULL, dtsled.devid, NULL, DTSLED_NAME);
	if (IS_ERR(dtsled.device)) {
		goto destroy_class;
	}

	return 0;

destroy_class:
	class_destroy(dtsled.class);
del_cdev:
	cdev_del(&dtsled.cdev);
del_unregister:
	unregister_chrdev_region(dtsled.devid, DTSLED_CNT);
fail_map:
	led_unmap();
	return -EIO;
}

/* Exit function */
static void __exit led_exit(void)
{
	/* Unmap GPIO registers */
	led_unmap();

	/* Unregister character device driver */
	cdev_del(&dtsled.cdev);
	unregister_chrdev_region(dtsled.devid, DTSLED_CNT);
	device_destroy(dtsled.class, dtsled.devid);
	class_destroy(dtsled.class);
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
