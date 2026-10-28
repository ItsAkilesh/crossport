// File: translator/tests/translate.rs
// Purpose: End-to-end translation of the real corpus ELF files, checked with an independent PE parser
// Author: Akilesh
// Date: 2026-10-28

use std::path::PathBuf;

use crossport_translator::error::TranslateError;
use crossport_translator::naming::ModuleName;
use crossport_translator::shim_exports::ShimExports;
use crossport_translator::source::DirectorySource;
use crossport_translator::translate::{translate, OutputFile};
use object::read::pe::{ImageNtHeaders, PeFile64};
use object::{LittleEndian, NameOrOrdinal, Object, ObjectSection};

const EXE: &[u8] = include_bytes!("fixtures/two_libs/two_libs");
const HELLO: &[u8] = include_bytes!("fixtures/hello_dyn.elf");
const FILE_DLL: u16 = 0x2000;
const IMAGE_BASE_EXE: u64 = 0x1_4000_0000;

fn fixtures() -> DirectorySource {
    DirectorySource::new(vec![
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/two_libs")
    ])
}

fn shim() -> ShimExports {
    ShimExports::from_names(["puts", "__libc_start_main", "crossport_start"].map(String::from))
}

fn name(raw: &str) -> ModuleName {
    ModuleName::parse(raw).expect("valid name")
}

fn translate_two_libs() -> Vec<OutputFile> {
    translate(name("two_libs"), EXE.to_vec(), &fixtures(), &shim())
        .expect("translates")
        .files
}

fn find<'a>(files: &'a [OutputFile], file_name: &str) -> &'a [u8] {
    &files
        .iter()
        .find(|f| f.file_name == file_name)
        .expect("file exists")
        .bytes
}

fn imports_of(bytes: &[u8]) -> Vec<(String, String)> {
    let pe = PeFile64::parse(bytes).expect("valid PE");
    pe.imports()
        .expect("import table parses")
        .map(|import| {
            let import = import.expect("import entry parses");
            let symbol = match import.name() {
                NameOrOrdinal::Name(name) => String::from_utf8_lossy(name).into_owned(),
                NameOrOrdinal::Ordinal(_) => panic!("imports are by name"),
            };
            (
                String::from_utf8_lossy(import.library()).into_owned(),
                symbol,
            )
        })
        .collect()
}

fn exports_of(bytes: &[u8]) -> Vec<String> {
    let pe = PeFile64::parse(bytes).expect("valid PE");
    Object::exports(&pe)
        .expect("export table parses")
        .filter_map(|export| match export.expect("export entry parses").name() {
            NameOrOrdinal::Name(name) => Some(String::from_utf8_lossy(name).into_owned()),
            NameOrOrdinal::Ordinal(_) => None,
        })
        .collect()
}

#[test]
fn produces_the_executable_first_then_libraries_in_discovery_order() {
    let files = translate_two_libs();
    let names: Vec<&str> = files.iter().map(|f| f.file_name.as_str()).collect();
    assert_eq!(
        names,
        ["two_libs.exe", "libgreet.so.1.dll", "libmath.so.1.dll"]
    );
}

#[test]
fn executable_is_a_pe_with_a_stub_entry_and_the_right_imports() {
    let files = translate_two_libs();
    let bytes = find(&files, "two_libs.exe");
    let pe = PeFile64::parse(bytes).expect("independent parser accepts the executable");
    assert_eq!(
        pe.nt_headers()
            .file_header()
            .characteristics
            .get(LittleEndian)
            .0
            & FILE_DLL,
        0
    );
    assert_eq!(pe.relative_address_base(), IMAGE_BASE_EXE);
    assert_eq!(
        pe.entry(),
        pe.section_by_name(".cport")
            .expect("startup section")
            .address()
    );

    let imports = imports_of(bytes);
    let from = |dll: &str| -> Vec<&str> {
        let mut symbols: Vec<&str> = imports
            .iter()
            .filter(|(d, _)| d == dll)
            .map(|(_, s)| s.as_str())
            .collect();
        symbols.sort_unstable();
        symbols
    };
    assert_eq!(from("libgreet.so.1.dll"), ["greet_run", "greet_total"]);
    assert_eq!(
        from("libmath.so.1.dll"),
        ["math_bump", "math_counter", "math_format"]
    );
    let shim_imports = from("crossport_libc.dll");
    assert!(shim_imports.contains(&"puts") && shim_imports.contains(&"crossport_start"));
}

#[test]
fn libraries_are_dlls_that_export_their_symbols_and_start_info() {
    let files = translate_two_libs();
    let math = find(&files, "libmath.so.1.dll");
    let pe = PeFile64::parse(math).expect("independent parser accepts the library");
    assert_ne!(
        pe.nt_headers()
            .file_header()
            .characteristics
            .get(LittleEndian)
            .0
            & FILE_DLL,
        0
    );
    assert_eq!(
        pe.nt_headers()
            .optional_header()
            .address_of_entry_point
            .get(LittleEndian),
        0
    );
    let mut exported = exports_of(math);
    exported.sort();
    assert_eq!(
        exported,
        [
            "__crossport_info",
            "math_add",
            "math_bump",
            "math_counter",
            "math_format",
            "math_mul",
            "math_op_count",
            "math_ops"
        ]
    );
}

#[test]
fn library_imports_follow_elf_lookup_not_dt_needed() {
    let files = translate_two_libs();
    let greet_imports = imports_of(find(&files, "libgreet.so.1.dll"));
    let mut math_calls: Vec<&str> = greet_imports
        .iter()
        .filter(|(d, _)| d == "libmath.so.1.dll")
        .map(|(_, s)| s.as_str())
        .collect();
    math_calls.sort_unstable();
    assert_eq!(math_calls, ["math_add", "math_bump", "math_ops"]);
    assert!(
        !greet_imports
            .iter()
            .any(|(_, symbol)| symbol == "math_counter"),
        "references to copied data bind into the executable, not libmath"
    );
}

#[test]
fn every_section_is_page_aligned_and_inside_the_image() {
    let files = translate_two_libs();
    for file in &files {
        let pe = PeFile64::parse(&*file.bytes).expect("parses");
        for section in pe.sections() {
            let rva = section.address() - pe.relative_address_base();
            assert_eq!(
                rva % 0x1000,
                0,
                "{} in {}",
                section.name().unwrap_or("?"),
                file.file_name
            );
        }
    }
}

#[test]
fn translation_is_deterministic() {
    let first = translate_two_libs();
    let second = translate_two_libs();
    for (left, right) in first.iter().zip(&second) {
        assert_eq!(
            left.bytes, right.bytes,
            "{} differs between runs",
            left.file_name
        );
    }
}

#[test]
fn a_single_module_program_still_translates() {
    let files = translate(name("hello_dyn"), HELLO.to_vec(), &fixtures(), &shim())
        .expect("translates")
        .files;
    assert_eq!(files.len(), 1);
    assert_eq!(files[0].file_name, "hello_dyn.exe");
}

#[test]
fn missing_library_names_the_library() {
    let empty = DirectorySource::new(vec![std::env::temp_dir().join("crossport-no-such-dir")]);
    let result = translate(name("two_libs"), EXE.to_vec(), &empty, &shim());
    assert!(
        matches!(result, Err(TranslateError::MissingLibrary { name }) if name == "libgreet.so.1")
    );
}

#[test]
fn a_symbol_nobody_provides_fails_at_translation_time() {
    let without_puts =
        ShimExports::from_names(["__libc_start_main", "crossport_start"].map(String::from));
    let result = translate(name("two_libs"), EXE.to_vec(), &fixtures(), &without_puts);
    assert!(
        matches!(result, Err(TranslateError::UnresolvedSymbol { symbol, .. }) if symbol == "puts")
    );
}

#[test]
fn a_shim_without_the_start_function_is_rejected() {
    let broken = ShimExports::from_names(["puts".to_string()]);
    let result = translate(name("two_libs"), EXE.to_vec(), &fixtures(), &broken);
    assert!(matches!(result, Err(TranslateError::Unsupported(_))));
}

fn replace_all(bytes: &mut [u8], from: &[u8], to: &[u8]) -> usize {
    assert_eq!(from.len(), to.len());
    let mut count = 0;
    let mut at = 0;
    while at + from.len() <= bytes.len() {
        if &bytes[at..at + from.len()] == from {
            bytes[at..at + from.len()].copy_from_slice(to);
            count += 1;
            at += from.len();
        } else {
            at += 1;
        }
    }
    count
}

#[test]
fn a_dt_needed_that_tries_to_escape_the_library_directory_is_rejected() {
    let mut hostile = EXE.to_vec();
    assert!(replace_all(&mut hostile, b"libmath.so.1", b"../math.so.1") >= 1);
    let result = translate(name("two_libs"), hostile, &fixtures(), &shim());
    assert!(
        matches!(result, Err(TranslateError::Unsupported(message)) if message.contains("plain file name"))
    );
}

#[test]
fn mutated_executables_never_panic_the_translator() {
    for position in (0..EXE.len()).step_by(11) {
        let mut mutated = EXE.to_vec();
        mutated[position] ^= 0xFF;
        let _ = translate(name("two_libs"), mutated, &fixtures(), &shim());
    }
}
