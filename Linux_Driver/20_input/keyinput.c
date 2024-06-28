#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/input.h>
#include <linux/timer.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>

#define KEYINPUT_NAME		"keyinput"	/* Name of the input device */

/* Structure for the key device */
struct key_dev {
	struct input_dev *idev;  /* Pointer to input_dev associated with the key */
	struct timer_list timer; /* Debounce timer */
	int gpio_key;            /* GPIO number for the key */
	int irq_key;             /* Interrupt number for the key */
};

static struct key_dev key;  /* Key device instance */

/*
 * @description		: Interrupt service routine for the key
 * @param - irq		: Interrupt number associated with the interrupt event
 * @param - dev_id	: Pointer to private data passed when requesting the interrupt
 * @return			: IRQ handling result
 */
static irqreturn_t key_interrupt(int irq, void *dev_id)
{
	if (key.irq_key != irq)
		return IRQ_NONE;
	
	/* Debounce handling: start a timer to handle debounce for 15ms */
	disable_irq_nosync(irq); /* Disable the key interrupt */
	mod_timer(&key.timer, jiffies + msecs_to_jiffies(15));
	
    return IRQ_HANDLED;
}

/*
 * @description			: Initialization function for the key GPIO and interrupt
 * @param - nd			: Pointer to the device node structure
 * @return				: 0 on success, negative error code on failure
 */
static int key_gpio_init(struct device_node *nd)
{
	int ret;
    unsigned long irq_flags;
	
	/* Get GPIO number from device tree */
	key.gpio_key = of_get_named_gpio(nd, "key-gpio", 0);
	if (!gpio_is_valid(key.gpio_key)) {
		printk("key: Failed to get key-gpio\n");
		return -EINVAL;
	}
	
	/* Request GPIO */
	ret = gpio_request(key.gpio_key, "KEY0");
    if (ret) {
        printk(KERN_ERR "key: Failed to request key-gpio\n");
        return ret;
	}	
	
	/* Set GPIO direction to input */
    gpio_direction_input(key.gpio_key);
	
	/* Get interrupt number associated with GPIO */
	key.irq_key = irq_of_parse_and_map(nd, 0);
	if (!key.irq_key) {
        return -EINVAL;
    }

    /* Get interrupt trigger type from device tree */
	irq_flags = irq_get_trigger_type(key.irq_key);
	if (IRQF_TRIGGER_NONE == irq_flags)
		irq_flags = IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING;
		
	/* Request interrupt */
	ret = request_irq(key.irq_key, key_interrupt, irq_flags, "Key0_IRQ", NULL);
	if (ret) {
        gpio_free(key.gpio_key);
        return ret;
    }

	return 0;
}

/*
 * @description		: Timer function for debounce handling
 * @param - arg		: Pointer to the timer_list structure
 * @return			: None
 */
static void key_timer_function(struct timer_list *arg)
{
	int val;
	
	/* Read key value and report input event */
	val = gpio_get_value(key.gpio_key);
	input_report_key(key.idev, KEY_0, !val);
	input_sync(key.idev);
	
	enable_irq(key.irq_key);
}

/*
 * @description			: Probe function for the platform driver
 * @param - pdev			: Pointer to the platform device structure
 * @return				: 0 on success, negative error code on failure
 */
static int atk_key_probe(struct platform_device *pdev)
{
	int ret;
	
	/* Initialize GPIO and interrupt */
	ret = key_gpio_init(pdev->dev.of_node);
	if (ret < 0)
		return ret;
		
	/* Initialize timer */
	timer_setup(&key.timer, key_timer_function, 0);
	
	/* Allocate input device */
	key.idev = input_allocate_device();
	key.idev->name = KEYINPUT_NAME;
	
	key.idev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
	input_set_capability(key.idev, EV_KEY, KEY_0);

	/* Register input device */
	ret = input_register_device(key.idev);
	if (ret) {
		printk("register input device failed!\r\n");
		goto free_gpio;
	}
	
	return 0;

free_gpio:
	free_irq(key.irq_key, NULL);
	gpio_free(key.gpio_key);
	del_timer_sync(&key.timer);
	return -EIO;
}

/*
 * @description			: Remove function for the platform driver
 * @param - pdev			: Pointer to the platform device structure
 * @return				: 0 on success, negative error code on failure
 */
static int atk_key_remove(struct platform_device *pdev)
{
	free_irq(key.irq_key, NULL);        /* Free interrupt */
	gpio_free(key.gpio_key);            /* Free GPIO */
	del_timer_sync(&key.timer);         /* Delete timer */
	input_unregister_device(key.idev);  /* Unregister input_dev */
	
	return 0;
}

static const struct of_device_id key_of_match[] = {
	{ .compatible = "alientek,key" },
	{ /* Sentinel */ }
};

static struct platform_driver atk_key_driver = {
	.driver = {
		.name = "stm32mp1-key",
		.of_match_table = key_of_match,
	},
	.probe	= atk_key_probe,
	.remove = atk_key_remove,
};

module_platform_driver(atk_key_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
