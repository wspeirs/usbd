#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/uio_driver.h>
#include <linux/genhd.h>
#include <linux/fs.h>
#include <linux/blkdev.h>


#define DRIVER_VERSION	"0.0.1"
#define DRIVER_AUTHOR	"William Speirs <bill.speirs@gmail.com>"
#define DRIVER_DESC	"A block device controlled by a user space program"

/*
 * Block device code example: https://linux-kernel-labs.github.io/master/labs/block_device_drivers.html
 */

#define USER_SPACE_BLKDEV_MAJOR        243
#define USER_SPACE_BLKDEV_MINORS       1
#define USER_SPACE_BLKDEV_NAME         "usbd"


static struct user_space_block_dev {
        spinlock_t lock;
        struct request_queue *queue;
        struct gendisk *gd;
} dev;

struct block_device_operations {
    int (*open) (struct block_device *, fmode_t);
    int (*release) (struct gendisk *, fmode_t);
    int (*locked_ioctl) (struct block_device *, fmode_t, unsigned, unsigned long);
    int (*ioctl) (struct block_device *, fmode_t, unsigned, unsigned long);
    int (*compat_ioctl) (struct block_device *, fmode_t, unsigned, unsigned long);
    int (*direct_access) (struct block_device *, sector_t, void **, unsigned long *);
    int (*media_changed) (struct gendisk *);
    int (*revalidate_disk) (struct gendisk *);
    int (*getgeo)(struct block_device *, struct hd_geometry *);
    struct module *owner;
}

/**
 * Open fucntion for the block device
 */
static int usbd_open(struct block_device *, fmode_t mode)
{
        printk(KERN_INFO "Opened usbd");
}

/**
 * Release fucntion for the block device
 */
static int usbd_release(struct gendisk *, fmode_t mode)
{
        printk(KERN_INFO "Released usbd");
}

static void usbd_request(struct request_queue *q);

struct block_device_operations user_space_block_ops = {
        .owner = THIS_MODULE,
        .open = usbd_open,
        .release = usbd_release
};

static int create_block_device(struct user_space_block_dev *dev)
{
        dev->gd = alloc_disk(USER_SPACE_BLKDEV_MINORS);

        if (!dev->gd) {
                printk (KERN_NOTICE "alloc_disk failure\n");
                goto out_err;
        }

        spin_lock_init(&dev->lock);
        dev->queue = blk_init_queue(usbd_request, &dev->lock);

        if(dev->queue == NULL)
                goto out_err;

        blk_queue_logical_block_size(dev->queue, KERNEL_SECTOR_SIZE);
        dev->queue->queuedata = dev;

        dev->gd->major = USER_SPACE_BLKDEV_MAJOR;
        dev->gd->first_minor = 0;
        dev->gd->fops = &user_space_block_ops;
        dev->gd->queue = dev->queue;
        dev->gd->private_data = dev;
        snprintf (dev->gd->disk_name, 32, "usbd");
        set_capacity(dev->gd, NR_SECTORS);

        add_disk(dev->gd);

out_err:
        return -ENOMEM;
}


int init_module(void)
{
        printk(KERN_INFO "Init usbd\n");

        int status;

        status = register_blkdev(USER_SPACE_BLKDEV_MAJOR, USER_SPACE_BLKDEV_NAME);

        if(status < 0) {
                printk(KERN_ERR "Unable to register user space block device\n");
                return -EBUSY;
        }

        status = create_block_device(&dev);

        if(status < 0)
                return status;

        return 0;
}

static void delete_block_device(struct user_space_block_dev *dev)
{
        if (dev->gd)
                del_gendisk(dev->gd);

        if(dev->queue)
                blk_cleanup_queue(dev->queue);
        
        unregister_blkdev(USER_SPACE_BLKDEV_MAJOR, USER_SPACE_BLKDEV_NAME);
}


void cleanup_module(void)
{
        printk(KERN_INFO "Cleanup usbd\n");
        delete_block_device(&dev);
}

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v3");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
