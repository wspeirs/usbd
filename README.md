# usbd
A block device where reads and writes are serviced by a user space program. `usbd` maps a page of memory that is used
for all reads/writes from/to the block device. The block device can be found at `/dev/usbd`. The user space proc maps
the file at `/proc/usbd_io`.

## Communication Protocol
The driver communicates reads/writes from/to the user space process via mapped page of memory, and normal `read` and `write` calls.
The protocol is as follows:

### Reading from /dev/usbd
When a program attempts to read data from `/dev/usbd`, the following steps occur:

1. The type (read) and LBA (logical block address) are returned from a read call to `/proc/usbd_io` (see diagram below for layout).
1. The user space program performs the read, and copies the requested data into the map.
1. The user space program writes the `io_request_response_t` to `/proc/usbd_io` setting the type to 0 for no error.
   * Note that if the block doesn't exist, you should simply return a block of zeros. 
1. The user space program goes back to blocking on a read of `/proc/usbd_io`
1. The driver returns the data in the map back to the program performing the read of `/dev/usbd`

### Writing to /dev/usbd
When a program attempts to write data to `/dev/usbd`, the following steps occur:

1. The type (write) and LBA (logical block address) are returned from a read call to `/proc/usbd_io`.
1. The user space program reads the data from the map, and peforms the write.
1. The user space program writes the `io_request_response_t` to `/proc/usbd_io` setting the type to 0 for no error.
   * Note that if the block doesn't exist, you should simply return a block of zeros. 
1. The user space program goes back to blocking on a read of `/proc/usbd_io`
1. The driver returns to the program performing the write of `/dev/usbd`

### Request Response Struct
The following is the layout of the struct `io_request_response_t`

```
+----------------------+
| 8-byte type          |
+----------------------+
| 8-byte LBA           |
+----------------------+
```

The type field contains _basically_ the same values found in the `req_opf` enum in `blk_types.h` in the Linux Kernel.
The type values listed below are used by usbd:

```
REQ_OP_READ           = 0, /* read sectors from the device */
REQ_OP_WRITE          = 1, /* write sectors to the device */
```
The values below are not yet supported by either the driver, or the sample user space program.
```	
REQ_OP_FLUSH          = 2, /* flush the volatile write cache */
REQ_OP_DISCARD        = 3, /* discard sectors */
REQ_OP_SECURE_ERASE   = 5, /* securely erase sectors */	
REQ_OP_WRITE_SAME     = 7, /* write the same sector many times */
REQ_OP_WRITE_ZEROES   = 9, /* write the zero filled sector many times */
```

## Code

There are 2 parts to this repo: kernel module, a sample user space program.

### usbd Kernel Module

The `usbd` kernel module consist of a single file: `usbd.c`. To compile the module, simply type `make` in the root 
directory of this repo. The kernel module has **only** been tested (and lightly tested at that) with v4.15.0 of the 
Linux kernel. Other version of the kernel _might_ not compile. If it compiles though, it _should_ work.

### User Space Program

The user space program, usbd-test, can be found in the `usbd-test` directory, and is written in Rust. To compile the
test program, simply type `cargo build` in the `usbd-test` directory. The test program has **only** been tested with 
Rust 1.29.0.
