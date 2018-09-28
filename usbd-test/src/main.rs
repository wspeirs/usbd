extern crate byteorder;
extern crate memmap;

use byteorder::{ReadBytesExt, WriteBytesExt, LE};
use memmap::MmapOptions;

use std::collections::HashMap;
use std::fs::OpenOptions;
use std::io::{Error as IOError, Cursor, Read, Write};
use std::path::PathBuf;

const ZERO_BLOCK: &'static [u8] = &[0; 4096];

pub fn buf2string(buf: &[u8]) -> String {
    let mut ret = String::new();

    for &b in buf {
        ret.push_str(format!("{:02X} ", b).as_str());
    }

    ret
}


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
        let mut request_buffer = vec![0; 16];

        fd.read_exact(&mut request_buffer)?;

        let mut cursor = Cursor::new(request_buffer);

        let req_type = cursor.read_u64::<LE>()?;
        let req_lba = cursor.read_u64::<LE>()?;

        println!("Request: type: {} LBA: {}", req_type, req_lba);

        // check if we have a read or a write
        if req_type == 0x0 {
            // read type
            if let Some(block) = blocks.get(&req_lba) {
                println!("Reading: {}{}", buf2string(&block[0..8]), buf2string(&block[4088..]));

                // write to our buffer
                mmap.copy_from_slice(block);
            } else {
                println!("Reading: ZERO_BLOCK");

                // write all zeros
                mmap.copy_from_slice(ZERO_BLOCK);
            }
        } else if req_type == 0x1 {
            // write type
            let mut block = [0; 4096];

            block.copy_from_slice(&mmap[0..4096]);

            println!("Writing: {}{}", buf2string(&block[0..8]), buf2string(&block[4088..]));

            blocks.insert(req_lba, block);
        }

        // write back that it all worked
        cursor.set_position(0);
        cursor.write_u64::<LE>(0x00)?; // type; 0 = no error
        cursor.write_u64::<LE>(0x00)?; // LBA - unused

        fd.write_all(&cursor.into_inner())?;
    }

//    Ok( () )
}
