#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define LED_MAJOR		200
#define LED_NAME		"led"

#define LEDOFF 	0
#define LEDON 	1

#define PERIPH_BASE     		     (0x40000000)
#define MPU_AHB4_PERIPH_BASE		 (PERIPH_BASE + 0x10000000)
#define RCC_BASE        		     (MPU_AHB4_PERIPH_BASE + 0x0000)
#define RCC_MP_AHB4ENSETR			 (RCC_BASE + 0XA28)
#define GPIOI_BASE					 (MPU_AHB4_PERIPH_BASE + 0xA000)
#define GPIOI_MODER      		     (GPIOI_BASE + 0x0000)
#define GPIOI_OTYPER      		     (GPIOI_BASE + 0x0004)
#define GPIOI_OSPEEDR      		     (GPIOI_BASE + 0x0008)
#define GPIOI_PUPDR      		     (GPIOI_BASE + 0x000C)
#define GPIOI_BSRR      		     (GPIOI_BASE + 0x0018)

/* Pointers to mapped register addresses */
static void __iomem *MPU_AHB4_PERIPH_RCC_PI;
static void __iomem *GPIOI_MODER_PI;
static void __iomem *GPIOI_OTYPER_PI;
static void __iomem *GPIOI_OSPEEDR_PI;
static void __iomem *GPIOI_PUPDR_PI;
static void __iomem *GPIOI_BSRR_PI;

/* Function to switch the LED on or off */
void led_switch(u8 sta)
{
	u32 val = 0;
	if(sta == LEDON) {
		val = readl(GPIOI_BSRR_PI);
		val |= (1 << 16);  // Set PI0 to high to turn on LED
		writel(val, GPIOI_BSRR_PI);
	} else if(sta == LEDOFF) {
		val = readl(GPIOI_BSRR_PI);
		val |= (1 << 0);   // Set PI0 to low to turn off LED
		writel(val, GPIOI_BSRR_PI);
	}
}

/* Function to unmap the GPIO registers */
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
	return 0;
}

/* Device read function (not implemented) */
static ssize_t led_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
	return 0;
}

/* Device write function */
static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
	int retvalue;
	unsigned char databuf[1];
	unsigned char ledstat;

	retvalue = copy_from_user(databuf, buf, cnt);
	if(retvalue < 0) {
		printk("kernel write failed!\r\n");
		return -EFAULT;
	}

	ledstat = databuf[0];  // Get the LED status from user space

	if(ledstat == LEDON) {
		led_switch(LEDON);  // Turn on LED
	} else if(ledstat == LEDOFF) {
		led_switch(LEDOFF);  // Turn off LED
	}
	return 0;
}

/* Device release/close function */
static int led_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/* Device operations structure */
static struct file_operations led_fops = {
	.owner = THIS_MODULE,
	.open = led_open,
	.read = led_read,
	.write = led_write,
	.release = led_release,
};

/* Initialization function */
static int __init led_init(void)
{
	int retvalue = 0;
	u32 val = 0;

	/* Map the GPIO registers */
	MPU_AHB4_PERIPH_RCC_PI = ioremap(RCC_MP_AHB4ENSETR, 4);
	GPIOI_MODER_PI = ioremap(GPIOI_MODER, 4);
	GPIOI_OTYPER_PI = ioremap(GPIOI_OTYPER, 4);
	GPIOI_OSPEEDR_PI = ioremap(GPIOI_OSPEEDR, 4);
	GPIOI_PUPDR_PI = ioremap(GPIOI_PUPDR, 4);
	GPIOI_BSRR_PI = ioremap(GPIOI_BSRR, 4);

	/* Enable clock for GPIO Port I */
	val = readl(MPU_AHB4_PERIPH_RCC_PI);
	val &= ~(0X1 << 8);  // Clear previous setting
	val |= (0X1 << 8);   // Set new value
	writel(val, MPU_AHB4_PERIPH_RCC_PI);

	/* Set PI0 to general purpose output mode */
	val = readl(GPIOI_MODER_PI);
	val &= ~(0X3 << 0);  // Clear bits 0-1
	val |= (0X1 << 0);   // Set bits 0-1 to 01 (output mode)
	writel(val, GPIOI_MODER_PI);

	/* Configure PI0 as push-pull output */
	val = readl(GPIOI_OTYPER_PI);
	val &= ~(0X1 << 0);  // Clear bit 0 (push-pull mode)
	writel(val, GPIOI_OTYPER_PI);

	/* Set PI0 to high-speed mode */
	val = readl(GPIOI_OSPEEDR_PI);
	val &= ~(0X3 << 0);  // Clear bits 0-1
	val |= (0x2 << 0);   // Set bits 0-1 to 10 (high-speed mode)
	writel(val, GPIOI_OSPEEDR_PI);

	/* Set PI0 to pull-up mode */
	val = readl(GPIOI_PUPDR_PI);
	val &= ~(0X3 << 0);  // Clear bits 0-1
	val |= (0x1 << 0);   // Set bits 0-1 to 01 (pull-up mode)
	writel(val, GPIOI_PUPDR_PI);

	/* Turn off LED by default */
	val = readl(GPIOI_BSRR_PI);
	val |= (0x1 << 0);   // Set bit 0 (PI0 low)
	writel(val, GPIOI_BSRR_PI);

	/* Register character device driver */
	retvalue = register_chrdev(LED_MAJOR, LED_NAME, &led_fops);
	if(retvalue < 0) {
		printk("register chrdev failed!\r\n");
		goto fail_map;
	}
	return 0;

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
	unregister_chrdev(LED_MAJOR, LED_NAME);
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
