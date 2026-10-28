// File: translator/src/pe_writer.rs
// Purpose: Serialize a PeImage into the bytes of a PE32+ console executable
// Author: Akilesh
// Date: 2026-10-28

use crate::error::TranslateError;
use crate::pe_model::{
    align_up, PeImage, PeSection, IMAGE_BASE, PAGE_SIZE, SCN_CODE, SCN_INITIALIZED_DATA,
};

const DOS_HEADER_SIZE: usize = 64;
const E_LFANEW_OFFSET: usize = 0x3C;
const MACHINE_AMD64: u16 = 0x8664;
const FILE_EXECUTABLE_IMAGE: u16 = 0x0002;
const FILE_LARGE_ADDRESS_AWARE: u16 = 0x0020;
const OPTIONAL_HEADER_SIZE: u16 = 240;
const PE32_PLUS_MAGIC: u16 = 0x20B;
const SUBSYSTEM_CONSOLE: u16 = 3;
const DLL_DYNAMIC_BASE: u16 = 0x0040;
const DLL_NX_COMPAT: u16 = 0x0100;
const STACK_RESERVE: u64 = 0x10_0000;
const STACK_COMMIT: u64 = 0x1000;
const DATA_DIRECTORY_COUNT: u32 = 16;
const IMPORT_DIRECTORY_INDEX: usize = 1;
const BASE_RELOC_DIRECTORY_INDEX: usize = 5;

struct Bytes(Vec<u8>);

impl Bytes {
    fn u16(&mut self, v: u16) {
        self.0.extend_from_slice(&v.to_le_bytes());
    }
    fn u32(&mut self, v: u32) {
        self.0.extend_from_slice(&v.to_le_bytes());
    }
    fn u64(&mut self, v: u64) {
        self.0.extend_from_slice(&v.to_le_bytes());
    }
}

pub fn write(image: &PeImage) -> Result<Vec<u8>, TranslateError> {
    let mut out = Bytes(Vec::new());
    write_dos_header(&mut out);
    out.0.extend_from_slice(b"PE\0\0");
    write_coff_header(&mut out, image.sections.len() as u16);
    write_optional_header(&mut out, image);
    for section in &image.sections {
        write_section_header(&mut out, section);
    }
    if out.0.len() > PAGE_SIZE as usize {
        return Err(TranslateError::Unsupported("too many sections for the PE header page".into()));
    }
    out.0.resize(PAGE_SIZE as usize, 0);
    for section in &image.sections {
        out.0.resize(section.rva as usize, 0);
        out.0.extend_from_slice(&section.data);
        out.0.resize(section.rva as usize + raw_size(section), 0);
    }
    Ok(out.0)
}

fn raw_size(section: &PeSection) -> usize {
    align_up(section.data.len() as u64, PAGE_SIZE) as usize
}

fn write_dos_header(out: &mut Bytes) {
    out.0.extend_from_slice(b"MZ");
    out.0.resize(E_LFANEW_OFFSET, 0);
    out.u32(DOS_HEADER_SIZE as u32);
}

fn write_coff_header(out: &mut Bytes, section_count: u16) {
    out.u16(MACHINE_AMD64);
    out.u16(section_count);
    out.u32(0);
    out.u32(0);
    out.u32(0);
    out.u16(OPTIONAL_HEADER_SIZE);
    out.u16(FILE_EXECUTABLE_IMAGE | FILE_LARGE_ADDRESS_AWARE);
}

fn size_of_image(image: &PeImage) -> u32 {
    let end = image
        .sections
        .iter()
        .map(|s| align_up(u64::from(s.rva) + u64::from(s.virtual_size), PAGE_SIZE))
        .max()
        .unwrap_or(PAGE_SIZE);
    end as u32
}

fn sum_sections(image: &PeImage, flag: u32) -> u32 {
    image
        .sections
        .iter()
        .filter(|s| s.characteristics & flag != 0)
        .map(|s| align_up(u64::from(s.virtual_size), PAGE_SIZE) as u32)
        .sum()
}

fn write_optional_header(out: &mut Bytes, image: &PeImage) {
    out.u16(PE32_PLUS_MAGIC);
    out.u16(14); // linker version (major.minor), informational
    out.u32(sum_sections(image, SCN_CODE));
    out.u32(sum_sections(image, SCN_INITIALIZED_DATA));
    out.u32(0);
    out.u32(image.entry_rva);
    out.u32(PAGE_SIZE as u32); // BaseOfCode
    out.u64(IMAGE_BASE);
    out.u32(PAGE_SIZE as u32); // SectionAlignment
    out.u32(PAGE_SIZE as u32); // FileAlignment
    for version in [6u16, 0, 0, 0, 6, 0] {
        out.u16(version); // OS, image, subsystem versions
    }
    out.u32(0);
    out.u32(size_of_image(image));
    out.u32(PAGE_SIZE as u32); // SizeOfHeaders
    out.u32(0); // CheckSum
    out.u16(SUBSYSTEM_CONSOLE);
    out.u16(DLL_DYNAMIC_BASE | DLL_NX_COMPAT);
    for size in [STACK_RESERVE, STACK_COMMIT, STACK_RESERVE, STACK_COMMIT] {
        out.u64(size);
    }
    out.u32(0);
    out.u32(DATA_DIRECTORY_COUNT);
    write_data_directories(out, image);
}

fn write_data_directories(out: &mut Bytes, image: &PeImage) {
    for index in 0..DATA_DIRECTORY_COUNT as usize {
        let directory = match index {
            IMPORT_DIRECTORY_INDEX => image.import_directory,
            BASE_RELOC_DIRECTORY_INDEX => image.base_reloc_directory,
            _ => Default::default(),
        };
        out.u32(directory.rva);
        out.u32(directory.size);
    }
}

fn write_section_header(out: &mut Bytes, section: &PeSection) {
    let mut name = [0u8; 8];
    let bytes = section.name.as_bytes();
    name[..bytes.len().min(8)].copy_from_slice(&bytes[..bytes.len().min(8)]);
    out.0.extend_from_slice(&name);
    out.u32(section.virtual_size);
    out.u32(section.rva);
    out.u32(raw_size(section) as u32);
    out.u32(if section.data.is_empty() { 0 } else { section.rva });
    out.u32(0);
    out.u32(0);
    out.u16(0);
    out.u16(0);
    out.u32(section.characteristics);
}
