#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/uio_driver.h>
#include <linux/genhd.h>
#include <linux/fs.h>
#include <linux/blkdev.h>


#define DRIVER_VERSION	"0.0.1"
#define DRIVER_AUTHOR	"William Speirs <bill.speirs@gmail.com>"
#define DRIVER_DESC	"A block device controlled by a user space program"

/**
 * Block device code tutorial: https://linux-kernel-labs.github.io/master/labs/block_device_drivers.html
 * Block device code example:  https://github.com/martinezjavier/ldd3/
 */

/**
 * Constants/parameters
 */
#define USER_SPACE_BLKDEV_MAJOR     243
#define USER_SPACE_BLKDEV_MINORS    1
#define USER_SPACE_BLKDEV_NAME      "usbd"
#define SECTOR_SHIFT                9
#define SECTOR_SIZE                 (1 << SECTOR_SHIFT)

static int DEVICE_SIZE = 1024; // size in sectors
module_param(DEVICE_SIZE, int, 0444); // make world-readable


/**
 * Representation of the device
 */
static struct usbd_dev {
    int open_count;
    spinlock_t lock;
    struct request_queue *queue;
    struct gendisk *gd;
} dev;

/**
 * Handle transfering data to/from the device
 */
static blk_qc_t usbd_make_request(struct request_queue *q, struct bio *bio)
{
//	struct usbd_dev *dev = q->queuedata;
	struct bio_vec bvec;
    struct bvec_iter iter;
	sector_t sector = bio->bi_iter.bi_sector;

    // switch on the operation (enum req_opf)
    switch(bio_op(bio)) {
    case REQ_OP_READ:
    case REQ_OP_WRITE:
        // loop through each segment
        bio_for_each_segment(bvec, bio, iter) {
            char *buffer = kmap_atomic(bvec.bv_page) + bvec.bv_offset; // map in our buffer
            unsigned nsectors = bio_cur_bytes(bio) >> SECTOR_SHIFT; // get the number of sectors

            if(bio_data_dir(bio) == WRITE) {
                printk(KERN_INFO "Writing %u sectors starting at %lu", nsectors, sector);
            } else {
                printk(KERN_INFO "Reading %u sectors starting at %lu", nsectors, sector);
                memset(buffer, 0xBB, bio_cur_bytes(bio));
            }

            sector += nsectors; // update the current sector

            kunmap_atomic(buffer);  // unmap the buffer
        }
        break;

    default:
        break;
    }

	bio_endio(bio);

    return 0;
}

/**
 * Open function     for the block device
 */
static int usbd_open(struct block_device *blk_dev, fmode_t mode)
{
	struct usbd_dev *dev = blk_dev->bd_disk->private_data;

    // acquire lock, bump count, unlock
    spin_lock(&dev->lock);
    dev->open_count += 1;
    spin_unlock(&dev->lock);

    // this could be wrong as we don't have the lock
    printk(KERN_INFO "Opened usbd, count %d", dev->open_count);

    return 0;
}

/**
 * Release function for the block device
 */
static void usbd_release(struct gendisk *gd, fmode_t mode)
{
    struct usbd_dev *dev = gd->private_data;

    // acquire lock, bump count, unlock
    spin_lock(&dev->lock);
    dev->open_count -= 1;
    spin_unlock(&dev->lock);

    // this could be wrong as we don't have the lock
    printk(KERN_INFO "Released usbd, count %d", dev->open_count);
}

struct block_device_operations user_space_block_ops = {
    .owner = THIS_MODULE,
    .open = usbd_open,
    .release = usbd_release
};

/**
 * Sets up the block device
 */
static int create_block_device(struct usbd_dev *dev)
{
    int status;

    // register the device
    status = register_blkdev(USER_SPACE_BLKDEV_MAJOR, USER_SPACE_BLKDEV_NAME);

    if(status < 0) {
        printk(KERN_ERR "Unable to register user space block device\n");
        return -EBUSY;
    }

    // setup the I/O queue without queueing
    dev->queue = blk_alloc_queue(GFP_KERNEL);

    if (dev->queue == NULL)
        goto out_err;

    blk_queue_make_request(dev->queue, usbd_make_request);


    // set the logical block size to 512, same as the kernel block size
    blk_queue_logical_block_size(dev->queue, SECTOR_SIZE);
    dev->queue->queuedata = dev;

    // allocate the disk
    dev->gd = alloc_disk(USER_SPACE_BLKDEV_MINORS);

    if (!dev->gd) {
        printk (KERN_NOTICE "alloc_disk failure\n");
        goto out_err;
    }

    // configure the gendisk struct
    dev->gd->major = USER_SPACE_BLKDEV_MAJOR;
    dev->gd->first_minor = 0;
    dev->gd->fops = &user_space_block_ops;
    dev->gd->queue = dev->queue;
    dev->gd->private_data = dev;
    snprintf (dev->gd->disk_name, 32, "usbd");
    set_capacity(dev->gd, DEVICE_SIZE); // set the capacity, in sectors of the device

    // set the open count to zero
    dev->open_count = 0;

    // add the disk
    add_disk(dev->gd);

    return 0;

// handle any errors by unregistering the block device, and returning ENOMEM
out_err:
	unregister_blkdev(USER_SPACE_BLKDEV_MAJOR, USER_SPACE_BLKDEV_NAME);
    return -ENOMEM;
}


static void delete_block_device(struct usbd_dev *dev)
{
    // clean up our gendisk
    if (dev->gd) {
        del_gendisk(dev->gd);
        put_disk(dev->gd);
    }

    // clean up our request queue
    if(dev->queue)
        kobject_put(&dev->queue->kobj);

    // unregister the device
    unregister_blkdev(USER_SPACE_BLKDEV_MAJOR, USER_SPACE_BLKDEV_NAME);
}

/*
 * Module init function.
 */
static int __init usbd_init(void)
{
    int status;

    printk(KERN_INFO "Init usbd\n");

    if((status = create_block_device(&dev)) < 0)
        return status;

    return 0;
}

/*
 * Module exit function
 */
static void usbd_exit(void)
{
    printk(KERN_INFO "Cleanup usbd\n");
    delete_block_device(&dev);
}

module_init(usbd_init);
module_exit(usbd_exit);


MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
