// File: fuzz/src/lib.rs
// Purpose: Fuzz targets for the translator: each takes hostile bytes and must return, never panic, hang or over-allocate
// Author: Akilesh
// Date: 2026-11-05

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};

use crossport_translator::analyze::analyze;
use crossport_translator::appimage::{unpack, Limits};
use crossport_translator::cli;
use crossport_translator::error::TranslateError;
use crossport_translator::naming::ModuleName;
use crossport_translator::shim_exports::ShimExports;
use crossport_translator::source::{
    read_bounded, AppDirSource, LibrarySource, LoadedLibrary, SearchContext,
};
use crossport_translator::translate::{translate, Missing};

pub const TWO_LIBS_EXE: &[u8] = include_bytes!("../../translator/tests/fixtures/two_libs/two_libs");
pub const LIB_GREET: &[u8] =
    include_bytes!("../../translator/tests/fixtures/two_libs/libgreet.so.1");
pub const LIB_MATH: &[u8] = include_bytes!("../../translator/tests/fixtures/two_libs/libmath.so.1");
pub const HELLO_ELF: &[u8] = include_bytes!("../../translator/tests/fixtures/hello_dyn.elf");

const SHIM_SYMBOLS: [&str; 4] = ["puts", "__libc_start_main", "crossport_start", "crossport_unavailable"];
const EXE_NAME: &str = "fuzzed";
const LIB_GREET_NAME: &str = "libgreet.so.1";
const LIB_MATH_NAME: &str = "libmath.so.1";
/// Header of an AppImage-shaped ELF: the SquashFS payload follows it directly.
const RUNTIME_BYTES: usize = 64;
const APPIMAGE_MARKER_AT: usize = 8;
const SECTION_TABLE_FIELD_AT: usize = 40;
/// Small caps so a hostile archive that passes the checks cannot fill the fuzzing machine's disk.
const FUZZ_LIMITS: Limits = Limits {
    max_entries: 2_000,
    max_file_bytes: 8 * 1024 * 1024,
    max_total_bytes: 32 * 1024 * 1024,
};

/// The names of every target, as the driver and the cargo-fuzz entry points use them.
pub const TARGETS: [(&str, fn(&[u8])); 8] = [
    ("analyze_elf", analyze_elf),
    ("translate_exe", translate_exe),
    ("translate_lib", translate_lib),
    ("shim_pe", shim_pe),
    ("unpack_image", unpack_image),
    ("unpack_squashfs", unpack_squashfs),
    ("convert", convert),
    ("cli_parse", cli_parse),
];

fn shim() -> ShimExports {
    ShimExports::from_names(SHIM_SYMBOLS.map(String::from))
}

/// Serves the fixture libraries from memory, with one of them optionally replaced by fuzz data.
struct MemorySource<'a> {
    greet: &'a [u8],
}

impl LibrarySource for MemorySource<'_> {
    fn load(
        &self,
        name: &ModuleName,
        _context: &SearchContext,
    ) -> Result<LoadedLibrary, TranslateError> {
        let bytes = match name.as_str() {
            LIB_GREET_NAME => self.greet,
            LIB_MATH_NAME => LIB_MATH,
            other => {
                return Err(TranslateError::MissingLibrary {
                    name: other.to_string(),
                })
            }
        };
        Ok(LoadedLibrary {
            bytes: bytes.to_vec(),
            origin: None,
        })
    }
}

fn scratch_dir(label: &str) -> PathBuf {
    static NEXT: AtomicUsize = AtomicUsize::new(0);
    std::env::temp_dir().join(format!(
        "crossport-fuzz-{label}-{}-{}",
        std::process::id(),
        NEXT.fetch_add(1, Ordering::Relaxed)
    ))
}

/// The ELF analysis alone: headers, segments, dynamic section, symbols, relocations.
pub fn analyze_elf(data: &[u8]) {
    let _ = analyze(data);
}

/// A hostile executable with the real fixture libraries behind it.
pub fn translate_exe(data: &[u8]) {
    let source = MemorySource { greet: LIB_GREET };
    for missing in [Missing::Fail, Missing::Stub] {
        let name = ModuleName::parse(EXE_NAME).expect("a fixed valid name");
        let _ = translate(name, data.to_vec(), &source, &shim(), missing);
    }
}

/// A real executable whose first library is hostile: the library path of the loader and relinker.
pub fn translate_lib(data: &[u8]) {
    let source = MemorySource { greet: data };
    let name = ModuleName::parse(EXE_NAME).expect("a fixed valid name");
    let _ = translate(name, TWO_LIBS_EXE.to_vec(), &source, &shim(), Missing::Stub);
}

/// The shim DLL is trusted input, but its export table is still parsed from a file.
pub fn shim_pe(data: &[u8]) {
    let _ = ShimExports::from_dll(data);
}

/// Prefix `data` with an AppImage header whose runtime ends right after it.
pub fn with_runtime_header(data: &[u8]) -> Vec<u8> {
    let mut image = vec![0u8; RUNTIME_BYTES];
    image[..4].copy_from_slice(b"\x7fELF");
    image[APPIMAGE_MARKER_AT..APPIMAGE_MARKER_AT + 3].copy_from_slice(b"AI\x02");
    image[SECTION_TABLE_FIELD_AT..SECTION_TABLE_FIELD_AT + 8]
        .copy_from_slice(&(RUNTIME_BYTES as u64).to_le_bytes());
    image.extend_from_slice(data);
    image
}

fn unpack_bytes(label: &str, image: &[u8]) -> Option<PathBuf> {
    let work = scratch_dir(label);
    fs::create_dir_all(&work).ok()?;
    let file = work.join("input.AppImage");
    fs::write(&file, image).ok()?;
    let out = work.join("out");
    let _ = unpack(&file, &out, &FUZZ_LIMITS);
    Some(work)
}

fn remove(work: Option<PathBuf>) {
    if let Some(work) = work {
        let _ = fs::remove_dir_all(work);
    }
}

/// A whole file as the user hands it over: the AppImage header and runtime parsing included.
pub fn unpack_image(data: &[u8]) {
    remove(unpack_bytes("image", data));
}

/// The SquashFS reader behind a valid header, so every input byte goes to the filesystem parser.
pub fn unpack_squashfs(data: &[u8]) {
    remove(unpack_bytes("squashfs", &with_runtime_header(data)));
}

/// `crossport convert`: unpack, then translate the program `AppRun` names against the unpacked AppDir.
pub fn convert(data: &[u8]) {
    let work = scratch_dir("convert");
    let outcome = convert_in(&work, &with_runtime_header(data));
    let _ = outcome;
    let _ = fs::remove_dir_all(&work);
}

fn convert_in(work: &Path, image: &[u8]) -> Result<(), TranslateError> {
    let io = |context: &str, source| TranslateError::Io {
        context: context.to_string(),
        source,
    };
    fs::create_dir_all(work).map_err(|e| io("creating the work directory", e))?;
    let file = work.join("input.AppImage");
    fs::write(&file, image).map_err(|e| io("writing the image", e))?;
    let appdir = work.join("appdir");
    let unpacked = unpack(&file, &appdir, &FUZZ_LIMITS)?;
    let program = unpacked
        .apprun_target
        .iter()
        .fold(appdir.clone(), |path, part| path.join(part));
    let bytes = read_bounded(
        fs::File::open(&program).map_err(|e| io("opening the program", e))?,
        "program",
    )?;
    let source = AppDirSource::new(appdir, program, &[])?;
    let name = ModuleName::parse(EXE_NAME)?;
    translate(name, bytes, &source, &shim(), Missing::Stub)?;
    Ok(())
}

/// The command line, split on NUL bytes into arguments.
pub fn cli_parse(data: &[u8]) {
    let args: Vec<String> = data
        .split(|byte| *byte == 0)
        .map(|part| String::from_utf8_lossy(part).into_owned())
        .collect();
    let _ = cli::parse(&args);
}
