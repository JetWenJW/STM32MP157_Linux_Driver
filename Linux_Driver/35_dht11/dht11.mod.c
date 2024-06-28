#include <linux/build-salt.h>
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(.gnu.linkonce.this_module) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section(__versions) = {
	{ 0x3e549f1d, "module_layout" },
	{ 0xb013481f, "platform_driver_unregister" },
	{ 0xd7362d1, "__platform_driver_register" },
	{ 0x189c5980, "arm_copy_to_user" },
	{ 0xdecd0b29, "__stack_chk_fail" },
	{ 0x419fdde, "gpiod_direction_input" },
	{ 0x8e865d3c, "arm_delay_ops" },
	{ 0xd10ac5b, "_dev_err" },
	{ 0x465b34e3, "misc_register" },
	{ 0x24d273d1, "add_timer" },
	{ 0xc6f46339, "init_timer_key" },
	{ 0x3d6d5526, "devm_gpio_request" },
	{ 0x8693c29e, "of_get_named_gpio_flags" },
	{ 0x507c67b4, "gpiod_direction_output_raw" },
	{ 0xc38c83b8, "mod_timer" },
	{ 0x526c3a6c, "jiffies" },
	{ 0xb2d48a2e, "queue_work_on" },
	{ 0x2d3385d3, "system_wq" },
	{ 0xa480411c, "gpiod_get_raw_value" },
	{ 0x1bdb3cda, "_dev_info" },
	{ 0x4205ad24, "cancel_work_sync" },
	{ 0x2b68bd2f, "del_timer" },
	{ 0x4dbee2ba, "misc_deregister" },
	{ 0x39dc96e9, "gpiod_set_raw_value" },
	{ 0x9a36bdf0, "gpio_to_desc" },
	{ 0xefd6cf06, "__aeabi_unwind_cpp_pr0" },
};

MODULE_INFO(depends, "");

