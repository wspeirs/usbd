# usbd
A block device that is controlled from a user space program. `usbd` leverages the UIO framework in the Linux kernel to expose an entry point for the user space program at `/dev/uioX`. On the other side of the driver is a block device exposed via `/dev/usbdX`.
