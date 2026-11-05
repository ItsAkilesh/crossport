// File: fuzz/src/seedgen.rs
// Purpose: Write the seed corpora: fixture ELF files and small AppImage payloads built with the SquashFS writer
// Author: Akilesh
// Date: 2026-11-05

use std::fs;
use std::io::Cursor;
use std::path::Path;

use backhand::compression::Compressor;
use backhand::{FilesystemCompressor, FilesystemWriter, NodeHeader};
use crossport_fuzz::{with_runtime_header, HELLO_ELF, LIB_GREET, LIB_MATH, TWO_LIBS_EXE};

const DIRECTORY_MODE: u16 = 0o755;

fn write(corpus: &Path, target: &str, name: &str, bytes: &[u8]) {
    let dir = corpus.join(target);
    fs::create_dir_all(&dir).expect("creates the corpus directory");
    fs::write(dir.join(name), bytes).expect("writes a seed");
}

/// A SquashFS image holding `AppRun` (the program), two libraries beside it and a symlink to one.
fn squashfs(compressor: Compressor, program: &[u8]) -> Vec<u8> {
    let mut writer = FilesystemWriter::default();
    writer.set_compressor(FilesystemCompressor::new(compressor, None).expect("compressor"));
    let header = NodeHeader::new(DIRECTORY_MODE, 0, 0, 0);
    writer.push_dir_all("usr/lib", header).expect("dir");
    writer
        .push_file(Cursor::new(program.to_vec()), "AppRun", header)
        .expect("program");
    for (name, bytes) in [("libgreet.so.1", LIB_GREET), ("libmath.so.1", LIB_MATH)] {
        writer
            .push_file(Cursor::new(bytes.to_vec()), format!("usr/lib/{name}"), header)
            .expect("library");
    }
    writer
        .push_symlink("libgreet.so.1", "usr/lib/libgreet.so", header)
        .expect("link");
    let mut out = Cursor::new(Vec::new());
    writer.write(&mut out).expect("writes the image");
    out.into_inner()
}

fn main() {
    let corpus = std::env::args().nth(1).unwrap_or_else(|| "corpus".into());
    let corpus = Path::new(&corpus);
    for (name, bytes) in [
        ("two_libs", TWO_LIBS_EXE),
        ("hello_dyn", HELLO_ELF),
        ("libgreet", LIB_GREET),
        ("libmath", LIB_MATH),
    ] {
        write(corpus, "analyze_elf", name, bytes);
        write(corpus, "translate_exe", name, bytes);
        write(corpus, "translate_lib", name, bytes);
    }
    for (name, compressor) in [
        ("gzip", Compressor::Gzip),
        ("xz", Compressor::Xz),
        ("zstd", Compressor::Zstd),
    ] {
        let payload = squashfs(compressor, TWO_LIBS_EXE);
        write(corpus, "unpack_squashfs", name, &payload);
        write(corpus, "convert", name, &payload);
        write(corpus, "unpack_image", name, &with_runtime_header(&payload));
    }
    let commands: [(&str, &[u8]); 3] = [
        ("unpack", b"unpack\0app.AppImage\0--out\0out"),
        ("analyze", b"analyze\0prog"),
        (
            "convert",
            b"convert\0app.AppImage\0--shim\0shim.dll\0--out\0out\0--stub-missing\0--runtime\0lib",
        ),
    ];
    for (name, bytes) in commands {
        write(corpus, "cli_parse", name, bytes);
    }
}
