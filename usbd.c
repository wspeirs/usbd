#include <linux/module.h>
#include <linux/kernel.h>

#define DRIVER_VERSION	"0.0.1"
#define DRIVER_AUTHOR	"William Speirs <bill.speirs@gmail.com>"
#define DRIVER_DESC	"A block device controlled by a user space program"


int init_module(void)
{
        printk(KERN_INFO "Hello world 1.\n");

        return 0;
}

void cleanup_module(void)
{
        printk(KERN_INFO "Goodbye world 1.\n");
}

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v3");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
