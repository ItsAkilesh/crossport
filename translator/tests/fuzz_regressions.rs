// File: translator/tests/fuzz_regressions.rs
// Purpose: Inputs the fuzzers found that once made the translator panic; each must now end in an error or success
// Author: Akilesh
// Date: 2026-11-05

use std::fs;
use std::path::PathBuf;
use std::sync::atomic::{AtomicUsize, Ordering};

use crossport_translator::appimage::{unpack, Limits};
use crossport_translator::error::TranslateError;
use crossport_translator::naming::ModuleName;
use crossport_translator::shim_exports::ShimExports;
use crossport_translator::source::DirectorySource;
use crossport_translator::translate::{translate, Missing};

const RELOC_ADDRESS_OVERFLOW: &[u8] = include_bytes!("fixtures/fuzz/reloc_address_overflow.elf");
const CODE_RANGE_OVERFLOW: &[u8] = include_bytes!("fixtures/fuzz/code_range_overflow.elf");
const RVA_BIAS_EXE: &[u8] = include_bytes!("fixtures/fuzz/rva_bias_overflow_exe.elf");
const RVA_BIAS_LIB: &[u8] = include_bytes!("fixtures/fuzz/rva_bias_overflow_lib.elf");
const NON_UTF8_NAME: &[u8] = include_bytes!("fixtures/fuzz/non_utf8_name.squashfs");
const RUNTIME_BYTES: usize = 64;
const SECTION_TABLE_FIELD_AT: usize = 40;
const APPIMAGE_MARKER_AT: usize = 8;

fn scratch(label: &str) -> PathBuf {
    static NEXT: AtomicUsize = AtomicUsize::new(0);
    let dir = std::env::temp_dir().join(format!(
        "crossport-fuzzreg-{label}-{}-{}",
        std::process::id(),
        NEXT.fetch_add(1, Ordering::Relaxed)
    ));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).expect("scratch directory");
    dir
}

fn translate_hostile(exe: &[u8]) -> Result<(), TranslateError> {
    let shim = ShimExports::from_names(
        [
            "puts",
            "__libc_start_main",
            "crossport_start",
            "crossport_unavailable",
        ]
        .map(String::from),
    );
    let source = DirectorySource::new(vec![
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/two_libs")
    ]);
    let name = ModuleName::parse("fuzzed").expect("valid name");
    translate(name, exe.to_vec(), &source, &shim, Missing::Stub).map(|_| ())
}

/// A relocation whose symbol value is close to `u64::MAX` overflowed `image base + address` (a debug build
/// panicked, a release build wrapped to a wrong address). It is now a typed error.
#[test]
fn a_relocation_target_near_the_top_of_the_address_space_is_an_error() {
    assert!(matches!(
        translate_hostile(RELOC_ADDRESS_OVERFLOW),
        Err(TranslateError::LimitExceeded(_))
    ));
}

/// A code range (from a symbol or an unwind entry) starting near `u64::MAX` overflowed `RVA_BIAS + start`.
#[test]
fn a_code_range_near_the_top_of_the_address_space_is_an_error() {
    assert!(matches!(
        translate_hostile(CODE_RANGE_OVERFLOW),
        Err(TranslateError::LimitExceeded(_))
    ));
}

/// More addresses from the file (entry point, relocation offset, array or range) overflowed `RVA_BIAS + x`.
#[test]
fn addresses_beyond_the_image_limit_are_an_error_wherever_they_come_from() {
    for hostile in [RVA_BIAS_EXE, RVA_BIAS_LIB] {
        assert!(matches!(
            translate_hostile(hostile),
            Err(TranslateError::LimitExceeded(_))
        ));
    }
}

/// An entry name that is not UTF-8 made the SquashFS reader unwrap and panic on a Windows host.
/// Unpacking must return, as an error or as a result, and never unwind.
#[test]
fn an_entry_name_that_is_not_utf8_does_not_panic_the_unpacker() {
    let work = scratch("utf8");
    let mut image = vec![0u8; RUNTIME_BYTES];
    image[..4].copy_from_slice(b"\x7fELF");
    image[APPIMAGE_MARKER_AT..APPIMAGE_MARKER_AT + 3].copy_from_slice(b"AI\x02");
    image[SECTION_TABLE_FIELD_AT..SECTION_TABLE_FIELD_AT + 8]
        .copy_from_slice(&(RUNTIME_BYTES as u64).to_le_bytes());
    image.extend_from_slice(NON_UTF8_NAME);
    let file = work.join("input.AppImage");
    fs::write(&file, image).expect("writes the image");
    let _ = unpack(&file, &work.join("out"), &Limits::default());
    let _ = fs::remove_dir_all(&work);
}
