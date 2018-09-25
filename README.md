# usbd
A block device where reads and writes are serviced by a user space program. `usbd` leverages the UIO framework in the Linux kernel to allow for reading and writing from a user space program through `/dev/uioX`. A normal block device is configured at `/dev/usbd`.

## Communcation Protocol
The driver communicates reads/writes from/to the block device via 3 mapped sections of memory and interrupts. The protocol works as follows.

### Reading from /dev/usbd
When a program attempts to read data from `/dev/usbd`, the following steps occur:

1. The type (read), sector, and length are set in the control map (see diagram below for layout).
1. An interrupt is triggered.
1. The user space program, blocked on a read of `/dev/uioX`, returns.
1. The type, sector, and length are read from the control map.
1. The user space program performs the read, and copies the requested data into the read map.
1. The user space program writes to `/dev/uioX` to let the driver know the requested data is avaliable.
1. The user space program goes back to blocking on a read of `/dev/uioX`
1. The driver returns the data in the read map back to the program performing the read of `/dev/usbd`

### Writing to /dev/usbd
When a program attempts to write data to `/dev/usbd`, the following steps occur:

1. The type (write), sector, and length of the data being written are set in the control map (see diagram below for layout).
1. An interrupt is triggered.
1. The user space program, blocked on a read of `/dev/uioX`, returns.
1. The user space program reads the data from the write map, and peforms the write.
1. The user space program goes back to blocking on a read of `/dev/uioX`

### Control Map Layout
The following is the layout of the control map:

```
+----------------------+
| 4-byte type          |
+----------------------+
| 8-byte sector number |
+----------------------+
| 4-byte length        |
+----------------------+
```

The type field contains the same values found in the `req_opf` enum in `blk_types.h` in the Linux Kernel. They are listed below:

```
REQ_OP_READ		      = 0, /* read sectors from the device */
REQ_OP_WRITE		    = 1, /* write sectors to the device */	
REQ_OP_FLUSH		    = 2, /* flush the volatile write cache */
REQ_OP_DISCARD		  = 3, /* discard sectors */
REQ_OP_ZONE_REPORT	= 4, (NOT USED)
REQ_OP_SECURE_ERASE	= 5, /* securely erase sectors */	
REQ_OP_ZONE_RESET	  = 6, (NOT USED)	
REQ_OP_WRITE_SAME	  = 7, /* write the same sector many times */
REQ_OP_WRITE_ZEROES	= 9, /* write the zero filled sector many times */
REQ_OP_SCSI_IN		  = 32, (NOT USED)
REQ_OP_SCSI_OUT		  = 33, (NOT USED)
REQ_OP_DRV_IN		    = 34, (NOT USED)
REQ_OP_DRV_OUT		  = 35, (NOT USED)
```

