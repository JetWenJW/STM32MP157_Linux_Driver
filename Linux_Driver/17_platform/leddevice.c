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

#define PERIPH_BASE     		     	(0x40000000)
#define MPU_AHB4_PERIPH_BASE			(PERIPH_BASE + 0x10000000)
#define RCC_BASE        		    	(MPU_AHB4_PERIPH_BASE + 0x0000)	
#define RCC_MP_AHB4ENSETR				(RCC_BASE + 0XA28)
#define GPIOI_BASE						(MPU_AHB4_PERIPH_BASE + 0xA000)	
#define GPIOI_MODER      			    (GPIOI_BASE + 0x0000)	
#define GPIOI_OTYPER      			    (GPIOI_BASE + 0x0004)	
#define GPIOI_OSPEEDR      			    (GPIOI_BASE + 0x0008)	
#define GPIOI_PUPDR      			    (GPIOI_BASE + 0x000C)	
#define GPIOI_BSRR      			    (GPIOI_BASE + 0x0018)
#define REGISTER_LENGTH					4

/* Function to release platform device resources */
static void led_release(struct device *dev)
{
	printk("led device released!\r\n");	
}

/* Define resources used by the LED device */
static struct resource led_resources[] = {
	[0] = {
		.start 	= RCC_MP_AHB4ENSETR,
		.end 	= (RCC_MP_AHB4ENSETR + REGISTER_LENGTH - 1),
		.flags 	= IORESOURCE_MEM,
	},	
	[1] = {
		.start	= GPIOI_MODER,
		.end	= (GPIOI_MODER + REGISTER_LENGTH - 1),
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= GPIOI_OTYPER,
		.end	= (GPIOI_OTYPER + REGISTER_LENGTH - 1),
		.flags	= IORESOURCE_MEM,
	},
	[3] = {
		.start	= GPIOI_OSPEEDR,
		.end	= (GPIOI_OSPEEDR + REGISTER_LENGTH - 1),
		.flags	= IORESOURCE_MEM,
	},
	[4] = {
		.start	= GPIOI_PUPDR,
		.end	= (GPIOI_PUPDR + REGISTER_LENGTH - 1),
		.flags	= IORESOURCE_MEM,
	},
	[5] = {
		.start	= GPIOI_BSRR,
		.end	= (GPIOI_BSRR + REGISTER_LENGTH - 1),
		.flags	= IORESOURCE_MEM,
	},
};

/* Define the platform device structure */
static struct platform_device leddevice = {
	.name = "stm32mp1-led",
	.id = -1,
	.dev = {
		.release = &led_release,
	},
	.num_resources = ARRAY_SIZE(led_resources),
	.resource = led_resources,
};

/* Initialize function for the module */
static int __init leddevice_init(void)
{
	return platform_device_register(&leddevice);
}

/* Exit function for the module */
static void __exit leddevice_exit(void)
{
	platform_device_unregister(&leddevice);
}

module_init(leddevice_init);
module_exit(leddevice_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
