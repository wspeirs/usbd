#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/uio_driver.h>
#include <linux/genhd.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/proc_fs.h>
#include <linux/blkdev.h>


#define DRIVER_VERSION	"0.0.1"
#define DRIVER_AUTHOR	"William Speirs <bill.speirs@gmail.com>"
#define DRIVER_DESC	"A block device controlled by a user space program"

/**
 * Block device code tutorial: https://linux-kernel-labs.github.io/master/labs/block_device_drivers.html
 * Block device code example:  https://github.com/martinezjavier/ldd3/
 * MMAP example: https://stackoverflow.com/a/45645732/3723253
 */

/**
 * Constants/parameters
 */
#define DRIVER_NAME     "usbd"
#define DRIVER_VERSION	"0.0.1"
#define DRIVER_AUTHOR	"William Speirs <bill.speirs@gmail.com>"
#define DRIVER_DESC	    "A block device controlled by a user space program"
#define USBD_MAJOR      243
#define USBD_MINOR      1
#define SECTOR_SHIFT    9
#define SECTOR_SIZE     (1 << SECTOR_SHIFT)

static int DEVICE_SIZE = 1024; // size in sectors
module_param(DEVICE_SIZE, int, 0444); // make world-readable

/**
 * The current state our device is in
 */
typedef enum driver_state {
    WAITING_ON_BLK_DEV_REQUEST,
    WAITING_ON_PROC_RESPONSE
} driver_state_t;

/**
 * Struct to hold information about the block device and IO file
 */
static struct usbd_t {
    spinlock_t lock;  // lock for modifying the struct
    int dev_open_count;
    int io_open_count;
    struct request_queue *queue;
    struct gendisk *gd;
    wait_queue_head_t wait_queue;
    driver_state_t driver_state;
    char *buffer;  // IO buffer between device and user proc
} usbd;

/**
 * Serialized struct that is passed via read/write calls from/to the IO file.
 */
struct io_request_response_t {
    u64 type;   // 1 = write; 0 = read
    u64 amount; // < 0 for error
    u64 lba;    // the block address

} request_response;


/**
 * Handle transferring data to/from the device
 */
static blk_qc_t dev_make_request(struct request_queue *q, struct bio *bio)
{
	struct bio_vec bvec;
    struct bvec_iter iter;
	sector_t lba = bio->bi_iter.bi_sector;

    // switch on the operation (enum req_opf)
    switch(bio_op(bio)) {
    case REQ_OP_READ:
    case REQ_OP_WRITE:
        // loop through each segment
        bio_for_each_segment(bvec, bio, iter) {
            char *buffer = kmap_atomic(bvec.bv_page) + bvec.bv_offset; // map in our buffer
            unsigned buffer_size = bio_cur_bytes(bio); // get the size of the buffer
            unsigned cur_offset = 0;

//            printk(KERN_INFO "BUFFER MAP: %p + %u\n", bvec.bv_page, bvec.bv_offset);

            // go through the buffer's size, a page at a time
            while(cur_offset < buffer_size) {
                // get the amount of data we're going to copy
                size_t amt = min(buffer_size - cur_offset, (unsigned) PAGE_SIZE);

                if(bio_data_dir(bio) == WRITE) {
                    printk(KERN_INFO "BLK DEV WRITE: %u bytes\tLBA: %lu\n", buffer_size, lba);

//                    printk(KERN_INFO "BLK DEV COPY: 0x%p -> 0x%p (%lu)\n", usbd.buffer, buffer, amt);

                    // copy the request into the IO buffer
                    memcpy(usbd.buffer, buffer, amt);

                    // fill out the io_request_response_t
                    request_response.amount = amt;
                    request_response.type = 1;
                    request_response.lba = lba;

                    // set the state of the driver
                    usbd.driver_state = WAITING_ON_PROC_RESPONSE;

                    // wake up the proc side
                    // use _sync call because we're just about to go to sleep
                    wake_up_interruptible_sync(&usbd.wait_queue);

                    // put ourself on the wait queue
                    wait_event_interruptible(usbd.wait_queue, usbd.driver_state == WAITING_ON_BLK_DEV_REQUEST);
                } else {
                    printk(KERN_INFO "BLK DEV READ: %u bytes\tLBA %lu\n", buffer_size, lba);

                    // set the state of the driver
                    usbd.driver_state = WAITING_ON_PROC_RESPONSE;

                    // wake up the proc side
                    // use _sync call because we're just about to go to sleep
                    wake_up_interruptible_sync(&usbd.wait_queue);

                    // put ourself on the wait queue
                    wait_event_interruptible(usbd.wait_queue, usbd.driver_state == WAITING_ON_BLK_DEV_REQUEST);

                    if(request_response.amount > 0) {
//                        printk(KERN_INFO "BLK DEV COPY: 0x%p -> 0x%p (%lu)\n", buffer, usbd.buffer, amt);

                        // read the response from the proc
                        memcpy(buffer, usbd.buffer, amt);
                    } else {
                        printk(KERN_ERR "Got an error response back: %llu\n", request_response.amount);
                    }
                }

                // update our offset
                cur_offset += amt;
            }

            // update the current lba
            lba += (buffer_size >> SECTOR_SHIFT);

            // unmap the buffer
            kunmap_atomic(buffer);
        }
        break;

    default:
        printk(KERN_INFO "Got an unknown blk dev command: %u", bio_op(bio));
        break;
    }

	bio_endio(bio);

    return 0;
}

/**
 * Open function for the block device
 */
static int dev_open(struct block_device *blk_dev, fmode_t mode)
{
    // acquire lock
    spin_lock(&usbd.lock);

    // make sure it's not already open
    if(usbd.dev_open_count > 0) {
        printk(KERN_NOTICE "Attempting to open usbd more than once: %u\n", usbd.dev_open_count);

        // unlock the struct
        spin_unlock(&usbd.lock);

        // return that they don't have permission
        return -EPERM;
    }

    // increment our count
    usbd.dev_open_count += 1;

    // set our state
    usbd.driver_state = WAITING_ON_BLK_DEV_REQUEST;

    printk(KERN_INFO "Opened usbd, count %d\n", usbd.dev_open_count);

    // unlock
    spin_unlock(&usbd.lock);

    return 0;
}

/**
 * Release function for the block device
 */
static void dev_release(struct gendisk *gd, fmode_t mode)
{
    // acquire lock
    spin_lock(&usbd.lock);

    // decrement our open count
    usbd.dev_open_count -= 1;

    printk(KERN_INFO "Released usbd, count %d\n", usbd.dev_open_count);

    // unlock
    spin_unlock(&usbd.lock);
}

struct block_device_operations blk_dev_ops = {
    .owner      = THIS_MODULE,
    .open       = dev_open,
    .release    = dev_release
};

/**
 * Sets up the block device
 */
static int create_block_device(void)
{
    int status;

    // register the device
    status = register_blkdev(USBD_MAJOR, DRIVER_NAME);

    if(status < 0) {
        printk(KERN_ERR "Unable to register user space block device\n");
        return -EBUSY;
    }

    // setup the block device queue without queueing
    usbd.queue = blk_alloc_queue(GFP_KERNEL);

    if (usbd.queue == NULL)
        goto out_err;

    // set the request handling function for the block device
    blk_queue_make_request(usbd.queue, dev_make_request);

    // set the logical block size to 512, same as the kernel block size
    blk_queue_logical_block_size(usbd.queue, SECTOR_SIZE);
    usbd.queue->queuedata = &usbd;

    // allocate the disk
    usbd.gd = alloc_disk(USBD_MINOR);

    if (!usbd.gd) {
        printk (KERN_NOTICE "alloc_disk failure\n");
        goto out_err;
    }

    // configure the gendisk struct
    usbd.gd->major = USBD_MAJOR;
    usbd.gd->first_minor = 0;
    usbd.gd->fops = &blk_dev_ops;
    usbd.gd->queue = usbd.queue;
    usbd.gd->private_data = &usbd;
    snprintf (usbd.gd->disk_name, 32, "usbd");
    set_capacity(usbd.gd, DEVICE_SIZE); // set the capacity, in sectors of the device

    // configure the queue to wait for handling requests
    init_waitqueue_head(&usbd.wait_queue);

    // set the state of the driver
    usbd.driver_state = WAITING_ON_BLK_DEV_REQUEST;

    // set the open count to zero
    usbd.dev_open_count = 0;

    // add the disk
    add_disk(usbd.gd);

    return 0;

// handle any errors by unregistering the block device, and returning ENOMEM
out_err:
	unregister_blkdev(USBD_MAJOR, DRIVER_NAME);
    return -ENOMEM;
}


static void delete_block_device(void)
{
    // clean up our gendisk
    if (usbd.gd) {
        del_gendisk(usbd.gd);
        put_disk(usbd.gd);
    }

    // clean up our request queue
    if(usbd.queue)
        kobject_put(&usbd.queue->kobj);

    // unregister the device
    unregister_blkdev(USBD_MAJOR, DRIVER_NAME);
}

static int fault(struct vm_fault *vmf)
{
    struct page *page;

    printk(KERN_INFO "vm_fault: 0x%lX\n", vmf->address);

    // convert the kernel VM buffer to a page
    page = virt_to_page(usbd.buffer);
    get_page(page);
    vmf-> page = page;

    return 0;
}

struct vm_operations_struct vm_ops = {
    .fault = fault
};

static int io_mmap(struct file *f, struct vm_area_struct *vma)
{
    printk(KERN_INFO "io_mmap\n");

    vma->vm_ops = &vm_ops;
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
    vma->vm_private_data = f->private_data;

    return 0;
}

static int io_open(struct inode *node, struct file *f)
{
    printk(KERN_INFO "io_open\n");

    // lock our structure
    spin_lock(&usbd.lock);

    // check to see if someone else has already opened this
    if(usbd.io_open_count > 0) {
        printk(KERN_NOTICE "Attempting to open the usbd_io file more tha once: %u\n", usbd.io_open_count);

        // unlock our structure
        spin_unlock(&usbd.lock);

        // return that they don't have access
        return -EACCES;
    }

    // bump our count
    usbd.io_open_count += 1;

    // set our IO struct to the private data
    f->private_data = &usbd;

    // unlock our struct
    spin_unlock(&usbd.lock);

    return 0;
}

static int io_release(struct inode *node, struct file *f)
{
    printk(KERN_INFO "io_release\n");

    // lock the structure
    spin_lock(&usbd.lock);

    f->private_data = NULL;

    // decrease our count
    usbd.io_open_count -= 1;

    // unlock the structure
    spin_unlock(&usbd.lock);

    return 0;
}

static ssize_t io_read(struct file *f, char __user *buff, size_t amt, loff_t *offset)
{
    printk(KERN_INFO "io_read: %zu at %lld\n", amt, *offset);

    // wait to process a response
    wait_event_interruptible(usbd.wait_queue, usbd.driver_state == WAITING_ON_PROC_RESPONSE);

    printk(KERN_INFO "io_read: type: 0x%0llX amt: %llu", request_response.type, request_response.amount);

    // copy the io_request_response_t into the buffer
    memcpy(buff, &request_response, sizeof(struct io_request_response_t));

    // return the size of the struct
    return sizeof(struct io_request_response_t);
}

static ssize_t io_write(struct file *f, const char __user *buff, size_t amt, loff_t *offset)
{
    printk(KERN_INFO "io_write: 0x%p %lu at %lld\n", buff, amt, *offset);

    // copy over the response
    memcpy(&request_response, buff, sizeof(struct io_request_response_t));

    // set our driver state as the proc returned from processing the request
    usbd.driver_state = WAITING_ON_BLK_DEV_REQUEST;

    // wake up the block device side
    wake_up_interruptible(&usbd.wait_queue);

    // simply return that we wrote whatever was sent to us
    return amt;
}

struct file_operations io_ops = {
    .owner = THIS_MODULE,
    .open = io_open,
    .release = io_release,
    .read = io_read,
    .write = io_write,
    .mmap = io_mmap,
    .llseek = no_llseek // we don't want to be able to seek in this file
};

static void create_io_file(void)
{
    printk(KERN_INFO "create_io_file\n");

    // lock our structure
    spin_lock(&usbd.lock);

    // allocate a buffer for it
    usbd.buffer = (char *)get_zeroed_page(GFP_KERNEL);

    // set the open count to zero
    usbd.io_open_count = 0;

    // unlock the structure
    spin_unlock(&usbd.lock);

    // create our IO file in /proc
    proc_create("usbd_io", 0, NULL, &io_ops);
}

static void delete_io_file(void)
{
    // remove the IO file from /proc
    remove_proc_entry("usbd_io", NULL);

    // lock our structure
    spin_lock(&usbd.lock);

    // release our IO page
    if(usbd.buffer != NULL) {
        free_page((unsigned long) usbd.buffer);
        usbd.buffer = NULL;
    }

    // unlock the structure
    spin_unlock(&usbd.lock);
}

/*
 * Module init function.
 */
static int __init usbd_init(void)
{
    int status;

    printk(KERN_INFO "Init usbd\n");

    create_io_file();

    if((status = create_block_device()) < 0)
        return status;

    return 0;
}

/*
 * Module exit function
 */
static void usbd_exit(void)
{
    printk(KERN_INFO "Cleanup usbd\n");
    delete_block_device();
    delete_io_file();
}

module_init(usbd_init);
module_exit(usbd_exit);


MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
