extern crate byteorder;
extern crate memmap;

use byteorder::{ReadBytesExt, WriteBytesExt, LE};
use memmap::MmapOptions;

use std::collections::HashMap;
use std::fs::OpenOptions;
use std::io::{Error as IOError, Cursor, Read, Write};
use std::path::PathBuf;

const ZERO_BLOCK: &'static [u8] = &[0; 4096];

fn main() -> Result<(), IOError>
{
    let mut blocks = HashMap::<u64, [u8; 4096]>::new();

    let mut fd = OpenOptions::new()
        .read(true)
        .write(true)
        .create(false)
        .open(PathBuf::from("/proc/usbd_io"))?;

    let mut mmap = unsafe {
        MmapOptions::new().len(4096).map_mut(&fd)?
    };

    loop { // just keep handling requests
        let mut request_buffer = vec![0; 24];

        fd.read_exact(&mut request_buffer)?;

        let mut cursor = Cursor::new(request_buffer);

        let req_type = cursor.read_u64::<LE>()?;
        let req_amt = cursor.read_u64::<LE>()?;
        let req_lba = cursor.read_u64::<LE>()?;

        println!("Request: type: {} amt: {} LBA: {}", req_type, req_amt, req_lba);

        // check if we have a read or a write
        if req_type == 0x0 {
            // read type
            if let Some(block) = blocks.get(&req_lba) {
                // write to our buffer
                mmap.copy_from_slice(block);
            } else {
                // write all zeros
                mmap.copy_from_slice(ZERO_BLOCK);
            }
        } else if req_type == 0x1 {
            // write type
            let mut block = [0; 4096];

            block.copy_from_slice(&mmap[0..4096]);

            blocks.insert(req_lba, block);
        }

        // write back that it all worked
        cursor.set_position(0);
        cursor.write_u64::<LE>(0x00)?;
        cursor.write_u64::<LE>(0xFF)?;
        cursor.write_u64::<LE>(0x00)?;

        fd.write_all(&cursor.into_inner())?;
    }

//    Ok( () )
}
