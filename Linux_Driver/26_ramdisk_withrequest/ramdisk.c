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

#define RAMDISK_SIZE    (2 * 1024 * 1024)   /* Size of RAM disk: 2MB */
#define RAMDISK_NAME    "ramdisk"           /* Name */
#define RADMISK_MINOR   3                   /* There are three disk partitions! Not the minor device number is 3! */

/* ramdisk device structure */
struct ramdisk_dev{
    int major;                          /* Major device number */
    unsigned char *ramdiskbuf;          /* RAM disk memory space for emulating block device */
    struct gendisk *gendisk;            /* gendisk */
    struct request_queue *queue;        /* Request queue */
    struct blk_mq_tag_set tag_set;      /* blk_mq_tag_set */
    spinlock_t lock;                    /* Spinlock */
};

struct ramdisk_dev *ramdisk = NULL;     /* ramdisk device pointer */

/*
 * @description : Process the transfer request
 * @param - req : Request
 * @return      : 0 for success, other values for failure
 */
static int ramdisk_transfer(struct request *req)
{   
    unsigned long start = blk_rq_pos(req) << 9;   /* blk_rq_pos gets the sector address, left shift by 9 converts to byte address */
    unsigned long len  = blk_rq_cur_bytes(req);   /* Size */

    /* Data buffer in bio:
     * Read: Data read from disk is stored in buffer
     * Write: Data to be written to disk is in buffer
     */
    void *buffer = bio_data(req->bio);      
    
    if(rq_data_dir(req) == READ)        /* Read data */    
        memcpy(buffer, ramdisk->ramdiskbuf + start, len);
    else if(rq_data_dir(req) == WRITE)  /* Write data */
        memcpy(ramdisk->ramdiskbuf + start, buffer, len);
        
    return 0;
}

/*
 * @description : Start processing the data transfer queue
 * @hctx        : Hardware-related queue structure
 * @bd          : Data-related structure
 * @return      : 0 for success, other values for failure
 */
static blk_status_t _queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data* bd)
{
    struct request *req = bd->rq; /* Get request from bd */
    struct ramdisk_dev *dev = req->rq_disk->private_data;
    int ret;
    
    blk_mq_start_request(req);      /* Start processing the queue */
    spin_lock(&dev->lock);          
    ret = ramdisk_transfer(req);    /* Transfer data */
    blk_mq_end_request(req, ret);   /* End processing the queue */
    spin_unlock(&dev->lock);
    
    return BLK_STS_OK;
}

/* Queue operation functions */
static struct blk_mq_ops mq_ops = {
    .queue_rq = _queue_rq,
};

/*
 * @description    : Open block device
 * @param - dev    : Block device
 * @param - mode   : Open mode
 * @return         : 0 for success, other values for failure
 */
int ramdisk_open(struct block_device *dev, fmode_t mode)
{
    printk("ramdisk open\n");
    return 0;
}

/*
 * @description    : Release block device
 * @param - disk   : gendisk
 * @param - mode   : Mode
 * @return         : 0 for success, other values for failure
 */
void ramdisk_release(struct gendisk *disk, fmode_t mode)
{
    printk("ramdisk release\n");
}

/*
 * @description    : Get disk geometry
 * @param - dev    : Block device
 * @param - geo    : Mode
 * @return         : 0 for success, other values for failure
 */
int ramdisk_getgeo(struct block_device *dev, struct hd_geometry *geo)
{
    /* Concept relative to mechanical hard disk */
    geo->heads = 2;             /* Heads */
    geo->cylinders = 32;        /* Cylinders */
    geo->sectors = RAMDISK_SIZE / (2 * 32 * 512); /* Number of sectors per track */
    return 0;
}

/* Block device operations */
static struct block_device_operations ramdisk_fops =
{
    .owner   = THIS_MODULE,
    .open    = ramdisk_open,
    .release = ramdisk_release,
    .getgeo  = ramdisk_getgeo,
};

/*
 * @description : Initialize queue-related operations
 * @set         : blk_mq_tag_set object
 * @return      : Address of request_queue
 */
static struct request_queue * create_req_queue(struct blk_mq_tag_set *set)
{
    struct request_queue *q;
    int ret;
    
    memset(set, 0, sizeof(*set));
    set->ops = &mq_ops;         // Operations
    set->nr_hw_queues = 2;      // Hardware queues
    set->queue_depth = 2;       // Queue depth
    set->numa_node = NUMA_NO_NODE;  // NUMA node
    set->flags =  BLK_MQ_F_SHOULD_MERGE; // Flag to merge bio dispatch
    
    ret = blk_mq_alloc_tag_set(set); // Allocate tag set
    if (ret) {
        printk(KERN_WARNING "sblkdev: unable to allocate tag set\n");
        return ERR_PTR(ret);
    }
    
    q = blk_mq_init_queue(set); // Allocate request queue
    if(IS_ERR(q)) {
        blk_mq_free_tag_set(set);
        return q;
    }

    return q;
}

/*
 * @description : Create block device, providing interface for user space.
 * @set         : ramdisk_dev object
 * @return      : 0 for success, other values for failure
 */
static int create_req_gendisk(struct ramdisk_dev *set)
{
    struct ramdisk_dev *dev = set;

    /* 1. Allocate and initialize gendisk */
    dev->gendisk = alloc_disk(RADMISK_MINOR);
    if(dev->gendisk == NULL)
        return -ENOMEM;
    
    /* 2. Add (register) disk */
    dev->gendisk->major = ramdisk->major; /* Major device number */
    dev->gendisk->first_minor = 0;        /* Starting minor device number */
    dev->gendisk->fops = &ramdisk_fops;   /* Operations */
    dev->gendisk->private_data = set;     /* Private data */
    dev->gendisk->queue = dev->queue;     /* Request queue */
    sprintf(dev->gendisk->disk_name, RAMDISK_NAME); /* Name */
    set_capacity(dev->gendisk, RAMDISK_SIZE/512);   /* Device capacity (in sectors) */
    add_disk(dev->gendisk);
    return 0;
}

/*
 * @description : Driver entry function
 * @return      : 0
 */
static int __init ramdisk_init(void)
{
    int ret = 0;
    struct ramdisk_dev * dev;
    printk("ramdisk init\n");
    
    /* 1. Allocate memory */
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if(dev == NULL) {
        return -ENOMEM;
    }
    
    dev->ramdiskbuf = kmalloc(RAMDISK_SIZE, GFP_KERNEL);
    if(dev->ramdiskbuf == NULL) {
        printk(KERN_WARNING "dev->ramdiskbuf: vmalloc failure.\n");
        return -ENOMEM;
    }
    ramdisk = dev;
    
    /* 2. Initialize spinlock */
    spin_lock_init(&dev->lock);

    /* 3. Register block device */
    dev->major = register_blkdev(0, RAMDISK_NAME); /* Automatically allocate major device number */
    if(dev->major < 0) {
        goto register_blkdev_fail;
    }
    
    /* 4. Create multiple queues */
    dev->queue = create_req_queue(&dev->tag_set);
    if(dev->queue == NULL) {
        goto create_queue_fail;
    }
    
    /* 5. Create block device */
    ret = create_req_gendisk(dev);
    if(ret < 0)
        goto create_gendisk_fail;
    
    return 0;

create_gendisk_fail:
    blk_cleanup_queue(dev->queue);
    blk_mq_free_tag_set(&dev->tag_set);
create_queue_fail:
    unregister_blkdev(dev->major, RAMDISK_NAME);
register_blkdev_fail:
    kfree(dev->ramdiskbuf);
    kfree(dev);
    return -ENOMEM;
}

/*
 * @description : Driver exit function
 * @return      : None
 */
static void __exit ramdisk_exit(void)
{
    
    printk("ramdisk exit\n");
    /* Release gendisk */
    if (ramdisk && ramdisk->gendisk) {
        del_gendisk(ramdisk->gendisk);
        put_disk(ramdisk->gendisk);
    }

    /* Clean up request queue */
    if (ramdisk && ramdisk->queue) {
        blk_cleanup_queue(ramdisk->queue);
    }
    
    /* Free blk_mq_tag_set */
    if (ramdisk) {
        blk_mq_free_tag_set(&ramdisk->tag_set);
    }
    
    /* Unregister block device */
    if (ramdisk) {
        unregister_blkdev(ramdisk->major, RAMDISK_NAME);
    }

    /* Free memory */
    if (ramdisk) {
        kfree(ramdisk->ramdiskbuf);
        kfree(ramdisk);
    }
}

module_init(ramdisk_init);
module_exit(ramdisk_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JetWen");
MODULE_INFO(intree, "Y");
