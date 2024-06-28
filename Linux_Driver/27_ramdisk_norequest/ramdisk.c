#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/hdreg.h>
#include <linux/kdev_t.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blk-mq.h>
#include <linux/buffer_head.h>
#include <linux/bio.h>

#define RAMDISK_SIZE    (2 * 1024 * 1024)   /* Size is 2MB */
#define RAMDISK_NAME    "ramdisk"           /* Name */
#define RADMISK_MINOR   3                   /* Number of disk partitions, not minor number 3 */

/* ramdisk device structure */
struct ramdisk_dev {
    int major;                          /* Major device number */
    unsigned char *ramdiskbuf;          /* Memory space for ramdisk, simulating block device */
    struct gendisk *gendisk;            /* gendisk */
    struct request_queue *queue;        /* Request queue */
    spinlock_t lock;                    /* Spinlock */
};

struct ramdisk_dev *ramdisk = NULL;     /* Pointer to ramdisk device */

/* Open block device */
int ramdisk_open(struct block_device *dev, fmode_t mode)
{
    printk("ramdisk open\n");
    return 0;
}

/* Release block device */
void ramdisk_release(struct gendisk *disk, fmode_t mode)
{
    printk("ramdisk release\n");
}

/* Get disk geometry */
int ramdisk_getgeo(struct block_device *dev, struct hd_geometry *geo)
{
    geo->heads = 2;                             /* Heads */
    geo->cylinders = 32;                        /* Cylinders */
    geo->sectors = RAMDISK_SIZE / (2 * 32 * 512);/* Sectors per track */
    return 0;
}

/* Block device operations */
static struct block_device_operations ramdisk_fops =
{
    .owner  = THIS_MODULE,
    .open   = ramdisk_open,
    .release = ramdisk_release,
    .getgeo  = ramdisk_getgeo,
};

/* "Make request" function */
static blk_qc_t ramdisk_make_request_fn(struct request_queue *q, struct bio *bio)
{
    int offset;
    struct bio_vec bvec;
    struct bvec_iter iter;
    unsigned long len = 0;
    struct ramdisk_dev *dev = q->queuedata;

    offset = (bio->bi_iter.bi_sector) << 9;    /* Get the offset address of the device to operate */
    spin_lock(&dev->lock);
    /* Process each segment in bio */
    bio_for_each_segment(bvec, bio, iter) {
        char *ptr = page_address(bvec.bv_page) + bvec.bv_offset;
        len = bvec.bv_len;

        if (bio_data_dir(bio) == READ)      /* Read data */
            memcpy(ptr, dev->ramdiskbuf + offset, len);
        else if (bio_data_dir(bio) == WRITE)/* Write data */
            memcpy(dev->ramdiskbuf + offset, ptr, len);
        offset += len;
    }
    spin_unlock(&dev->lock);
    bio_endio(bio);
    return BLK_QC_T_NONE;
}

/* Initialize queue operations */
static struct request_queue *create_req_queue(struct ramdisk_dev *set)
{
    struct request_queue *q;

    q = blk_alloc_queue(GFP_KERNEL);

    blk_queue_make_request(q, ramdisk_make_request_fn);

    q->queuedata = set;
    return q;
}

/* Create block device, providing interface for user space */
static int create_req_gendisk(struct ramdisk_dev *set)
{
    struct ramdisk_dev *dev = set;

    /* 1. Allocate and initialize gendisk */
    dev->gendisk = alloc_disk(RADMISK_MINOR);
    if (dev == NULL)
        return -ENOMEM;

    /* 2. Add (register) disk */
    dev->gendisk->major = ramdisk->major;         /* Major device number */
    dev->gendisk->first_minor = 0;                /* Starting minor number */
    dev->gendisk->fops = &ramdisk_fops;           /* Operations */
    dev->gendisk->private_data = set;             /* Private data */
    dev->gendisk->queue = dev->queue;             /* Request queue */
    sprintf(dev->gendisk->disk_name, RAMDISK_NAME); /* Name */
    set_capacity(dev->gendisk, RAMDISK_SIZE / 512);/* Device capacity (in sectors) */
    add_disk(dev->gendisk);
    return 0;
}

/* Module initialization function */
static int __init ramdisk_init(void)
{
    int ret = 0;
    struct ramdisk_dev *dev;
    printk("ramdisk init\n");

    /* 1. Allocate memory */
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (dev == NULL) {
        return -ENOMEM;
    }

    dev->ramdiskbuf = kmalloc(RAMDISK_SIZE, GFP_KERNEL);
    if (dev->ramdiskbuf == NULL) {
        printk(KERN_WARNING "dev->ramdiskbuf: vmalloc failure.\n");
        return -ENOMEM;
    }
    ramdisk = dev;

    /* 2. Initialize spinlock */
    spin_lock_init(&dev->lock);

    /* 3. Register block device */
    dev->major = register_blkdev(0, RAMDISK_NAME); /* Automatically assign major number by the system */
    if (dev->major < 0) {
        goto register_blkdev_fail;
    }

    /* 4. Create multiple queues */
    dev->queue = create_req_queue(dev);
    if (dev->queue == NULL) {
        goto create_queue_fail;
    }

    /* 5. Create block device */
    ret = create_req_gendisk(dev);
    if (ret < 0)
        goto create_gendisk_fail;

    return 0;

create_gendisk_fail:
    blk_cleanup_queue(dev->queue);
create_queue_fail:
    unregister_blkdev(dev->major, RAMDISK_NAME);
register_blkdev_fail:
    kfree(dev->ramdiskbuf);
    kfree(dev);
    return -ENOMEM;
}

/* Module exit function */
static void __exit ramdisk_exit(void)
{
    printk("ramdisk exit\n");
    /* Release gendisk */
    del_gendisk(ramdisk->gendisk);
    put_disk(ramdisk->gendisk);

    /* Clean up request queue */
    blk_cleanup_queue(ramdisk->queue);

    /* Unregister block device */
    unregister_blkdev(ramdisk->major, RAMDISK_NAME);

    /* Free memory */
    kfree(ramdisk->ramdiskbuf);
    kfree(ramdisk);
}

module_init(ramdisk_init);
module_exit(ramdisk_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
