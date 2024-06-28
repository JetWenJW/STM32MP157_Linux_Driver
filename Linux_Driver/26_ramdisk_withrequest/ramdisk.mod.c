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
	{ 0x38f37a40, "put_disk" },
	{ 0xde2e3aa1, "del_gendisk" },
	{ 0x37a0cba, "kfree" },
	{ 0xb5a459dc, "unregister_blkdev" },
	{ 0xa2596e99, "device_add_disk" },
	{ 0xd7bce7c, "blk_cleanup_queue" },
	{ 0x89ae04bb, "__alloc_disk_node" },
	{ 0xd6d67e4b, "blk_mq_free_tag_set" },
	{ 0x300ac5f3, "blk_mq_init_queue" },
	{ 0x24db15a2, "blk_mq_alloc_tag_set" },
	{ 0x5f754e5a, "memset" },
	{ 0x71a50dbc, "register_blkdev" },
	{ 0x77f6f183, "kmalloc_order_trace" },
	{ 0x7a4fec84, "kmem_cache_alloc_trace" },
	{ 0xf23ce8b, "kmalloc_caches" },
	{ 0x12585e08, "_raw_spin_unlock" },
	{ 0x93afc38, "blk_mq_end_request" },
	{ 0x9d669763, "memcpy" },
	{ 0xef7b6567, "page_address" },
	{ 0xdb9ca3c5, "_raw_spin_lock" },
	{ 0xd3ebab19, "blk_mq_start_request" },
	{ 0xc5850110, "printk" },
	{ 0xefd6cf06, "__aeabi_unwind_cpp_pr0" },
};

MODULE_INFO(depends, "");

