// File: translator/tests/translate_hello_dyn.rs
// Purpose: Translate the real hello_dyn ELF and check the PE with an independent parser
// Author: Akilesh
// Date: 2026-10-28

use crossport_translator::analyze::analyze;
use crossport_translator::pe_writer;
use crossport_translator::relink::{relink, SHIM_DLL, START_SYMBOL};
use object::read::pe::{ImageNtHeaders, PeFile64};
use object::NameOrOrdinal;
use object::{Object, ObjectSection};

const FIXTURE: &[u8] = include_bytes!("fixtures/hello_dyn.elf");

fn translated() -> Vec<u8> {
    let info = analyze(FIXTURE).expect("fixture parses");
    let image = relink(&info, FIXTURE).expect("fixture relinks");
    pe_writer::write(&image).expect("image serializes")
}

#[test]
fn output_is_a_valid_pe32_plus_image() {
    let bytes = translated();
    let pe = PeFile64::parse(&*bytes).expect("independent parser accepts the PE");
    assert_eq!(
        pe.nt_headers().file_header().machine.get(object::LittleEndian),
        object::pe::IMAGE_FILE_MACHINE_AMD64
    );
    let stub = pe.section_by_name(".cport").expect("stub section");
    assert_eq!(pe.entry(), stub.address());
}

#[test]
fn imports_come_from_the_shim_dll() {
    let bytes = translated();
    let pe = PeFile64::parse(&*bytes).expect("parses");
    let imports = pe.imports().expect("import table parses");
    let names: Vec<String> = imports
        .map(|import| {
            let import = import.expect("import entry parses");
            assert_eq!(import.library(), SHIM_DLL.as_bytes());
            match import.name() {
                NameOrOrdinal::Name(name) => String::from_utf8_lossy(name).into_owned(),
                NameOrOrdinal::Ordinal(_) => panic!("shim imports are by name"),
            }
        })
        .collect();
    assert_eq!(names, vec!["__libc_start_main", "puts", START_SYMBOL]);
}

#[test]
fn elf_segments_keep_their_relative_layout() {
    let bytes = translated();
    let pe = PeFile64::parse(&*bytes).expect("parses");
    let rva = |name: &str| pe.section_by_name(name).expect("section exists").address() - 0x1_4000_0000;
    assert_eq!(rva(".elf0"), 0x1000);
    assert_eq!(rva(".elf1"), 0x2000);
    assert_eq!(rva(".elf3"), 0x4000);
}
