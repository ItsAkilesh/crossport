// File: translator/tests/appimage.rs
// Purpose: Unpack real SquashFS images built in memory: formats, symlinks, safety rules, limits and corruption
// Author: Akilesh
// Date: 2026-10-28

use std::fs;
use std::io::Cursor;
use std::path::PathBuf;
use std::sync::atomic::{AtomicUsize, Ordering};

use backhand::compression::Compressor;
use backhand::{FilesystemCompressor, FilesystemWriter, NodeHeader};
use crossport_translator::appimage::{unpack, Limits};
use crossport_translator::error::TranslateError;

const ELF_PROGRAM: &[u8] = b"\x7fELF fake program bytes";
const RUNTIME_END: u64 = 64;

enum Entry<'a> {
    Dir(&'a str),
    File(&'a str, &'a [u8]),
    Link(&'a str, &'a str),
}

fn scratch(label: &str) -> PathBuf {
    static NEXT: AtomicUsize = AtomicUsize::new(0);
    let dir = std::env::temp_dir().join(format!(
        "crossport-appimage-{label}-{}-{}",
        std::process::id(),
        NEXT.fetch_add(1, Ordering::Relaxed)
    ));
    let _ = fs::remove_dir_all(&dir);
    dir
}

/// An AppImage-shaped ELF header (no section headers) followed by a SquashFS image.
fn image_bytes(compressor: Compressor, entries: &[Entry<'_>]) -> Vec<u8> {
    let mut writer = FilesystemWriter::default();
    writer.set_compressor(FilesystemCompressor::new(compressor, None).expect("compressor"));
    let header = NodeHeader::new(0o755, 0, 0, 0);
    let mut made = std::collections::HashSet::new();
    for entry in entries {
        match entry {
            Entry::Dir(path) => {
                if made.insert(PathBuf::from(path)) {
                    writer.push_dir_all(path, header).expect("dir");
                }
            }
            Entry::File(path, bytes) => {
                ensure_parent(&mut writer, &mut made, header, path);
                writer
                    .push_file(Cursor::new(bytes.to_vec()), path, header)
                    .expect("file");
            }
            Entry::Link(path, target) => {
                ensure_parent(&mut writer, &mut made, header, path);
                writer.push_symlink(target, path, header).expect("link");
            }
        }
    }
    let mut squashfs = Cursor::new(Vec::new());
    writer.write(&mut squashfs).expect("writes squashfs");
    let mut image = vec![0u8; RUNTIME_END as usize];
    image[..4].copy_from_slice(b"\x7fELF");
    image[8..11].copy_from_slice(b"AI\x02");
    image[0x28..0x30].copy_from_slice(&RUNTIME_END.to_le_bytes());
    image.extend_from_slice(squashfs.get_ref());
    image
}

fn ensure_parent(
    writer: &mut FilesystemWriter<'_, '_, '_>,
    made: &mut std::collections::HashSet<PathBuf>,
    header: NodeHeader,
    path: &str,
) {
    let parent = std::path::Path::new(path)
        .parent()
        .filter(|p| !p.as_os_str().is_empty());
    if let Some(parent) = parent {
        if made.insert(parent.to_path_buf()) {
            writer.push_dir_all(parent, header).expect("parent dirs");
        }
    }
}

fn write_image(label: &str, bytes: &[u8]) -> PathBuf {
    let dir = scratch(label);
    fs::create_dir_all(&dir).expect("dir");
    let path = dir.join("app.AppImage");
    fs::write(&path, bytes).expect("image");
    path
}

fn sample_entries() -> Vec<Entry<'static>> {
    vec![
        Entry::Dir("usr/bin"),
        Entry::File("usr/bin/tool", ELF_PROGRAM),
        Entry::Link("AppRun", "usr/bin/tool"),
        Entry::File("usr/lib/libx.so.1.2", b"library bytes"),
        Entry::Link("usr/lib/libx.so.1", "libx.so.1.2"),
        Entry::Link("usr/lib/libx.so", "libx.so.1"),
        Entry::File("usr/share/doc/readme.txt", b"hello\n"),
    ]
}

fn unpack_with(
    label: &str,
    bytes: &[u8],
    limits: &Limits,
) -> Result<(PathBuf, crossport_translator::appimage::AppDir), TranslateError> {
    let image = write_image(label, bytes);
    let out = scratch(&format!("{label}-out"));
    unpack(&image, &out, limits).map(|app_dir| (out, app_dir))
}

fn unpack_ok(label: &str, bytes: &[u8]) -> (PathBuf, crossport_translator::appimage::AppDir) {
    unpack_with(label, bytes, &Limits::default()).expect("unpacks")
}

#[test]
fn files_and_chained_symlinks_come_out_as_a_usable_appdir() {
    let (out, app_dir) = unpack_ok("tree", &image_bytes(Compressor::Gzip, &sample_entries()));
    assert_eq!(
        fs::read(out.join("usr/bin/tool")).expect("tool"),
        ELF_PROGRAM
    );
    assert_eq!(
        fs::read(out.join("AppRun")).expect("AppRun"),
        ELF_PROGRAM,
        "the link became a copy"
    );
    assert_eq!(
        fs::read(out.join("usr/lib/libx.so")).expect("chained link"),
        b"library bytes"
    );
    assert_eq!(
        fs::read(out.join("usr/share/doc/readme.txt")).expect("data"),
        b"hello\n"
    );
    assert_eq!(app_dir.apprun_target, ["usr", "bin", "tool"]);
    assert_eq!(app_dir.report.links_materialized, 3);
    assert!(app_dir.report.skipped.is_empty());
}

#[test]
fn gzip_zstd_and_xz_payloads_all_unpack() {
    for (label, compressor) in [
        ("gzip", Compressor::Gzip),
        ("zstd", Compressor::Zstd),
        ("xz", Compressor::Xz),
    ] {
        let (out, _) = unpack_ok(label, &image_bytes(compressor, &sample_entries()));
        assert_eq!(
            fs::read(out.join("usr/lib/libx.so.1")).expect("link"),
            b"library bytes",
            "{label}"
        );
    }
}

#[test]
fn a_script_launcher_is_refused_with_a_clear_reason() {
    let entries = [Entry::File("AppRun", b"#!/bin/sh\nexec ./usr/bin/tool\n")];
    let error = unpack_with(
        "script",
        &image_bytes(Compressor::Gzip, &entries),
        &Limits::default(),
    )
    .expect_err("refused");
    assert!(matches!(error, TranslateError::Unsupported(m) if m.contains("script")));
}

#[test]
fn a_missing_or_non_elf_apprun_is_refused() {
    let none = [Entry::File("usr/bin/tool", ELF_PROGRAM)];
    assert!(unpack_with(
        "no-apprun",
        &image_bytes(Compressor::Gzip, &none),
        &Limits::default()
    )
    .is_err());
    let text = [Entry::File("AppRun", b"just text")];
    assert!(unpack_with(
        "text-apprun",
        &image_bytes(Compressor::Gzip, &text),
        &Limits::default()
    )
    .is_err());
}

#[test]
fn names_windows_cannot_store_stop_the_unpack() {
    for bad in ["usr/bin/aux", "usr/bin/NUL.txt", "usr/bin/what?"] {
        let entries = [Entry::File("AppRun", ELF_PROGRAM), Entry::File(bad, b"x")];
        let result = unpack_with(
            "badname",
            &image_bytes(Compressor::Gzip, &entries),
            &Limits::default(),
        );
        assert!(
            matches!(result, Err(TranslateError::Unsupported(_))),
            "{bad}"
        );
    }
}

#[test]
fn entries_that_collide_on_a_case_insensitive_file_system_stop_the_unpack() {
    let entries = [
        Entry::File("AppRun", ELF_PROGRAM),
        Entry::File("usr/lib/Libx.so", b"a"),
        Entry::File("usr/lib/libx.so", b"b"),
    ];
    let result = unpack_with(
        "collide",
        &image_bytes(Compressor::Gzip, &entries),
        &Limits::default(),
    );
    assert!(matches!(result, Err(TranslateError::Unsupported(m)) if m.contains("case")));
}

#[test]
fn links_that_leave_the_archive_or_dangle_are_skipped_and_reported_not_followed() {
    let entries = [
        Entry::File("AppRun", ELF_PROGRAM),
        Entry::Link("escape", "../../etc/passwd"),
        Entry::Link("absolute", "/etc/passwd"),
        Entry::Link("dangling", "missing/file"),
        Entry::Dir("usr/lib"),
        Entry::Link("dirlink", "usr/lib"),
    ];
    let (out, app_dir) = unpack_ok("links", &image_bytes(Compressor::Gzip, &entries));
    assert_eq!(
        app_dir.report.skipped.len(),
        4,
        "{:?}",
        app_dir.report.skipped
    );
    for name in ["escape", "absolute", "dangling", "dirlink"] {
        assert!(!out.join(name).exists(), "{name} must not be created");
    }
}

#[test]
fn the_size_and_entry_limits_are_enforced() {
    let bytes = image_bytes(Compressor::Gzip, &sample_entries());
    let entries = Limits {
        max_entries: 3,
        ..Limits::default()
    };
    assert!(matches!(
        unpack_with("cap-entries", &bytes, &entries),
        Err(TranslateError::LimitExceeded(_))
    ));
    let per_file = Limits {
        max_file_bytes: 4,
        ..Limits::default()
    };
    assert!(matches!(
        unpack_with("cap-file", &bytes, &per_file),
        Err(TranslateError::LimitExceeded(_))
    ));
    let total = Limits {
        max_total_bytes: 40,
        ..Limits::default()
    };
    assert!(matches!(
        unpack_with("cap-total", &bytes, &total),
        Err(TranslateError::LimitExceeded(_))
    ));
}

#[test]
fn an_existing_non_empty_destination_is_never_written_into() {
    let image = write_image("dest", &image_bytes(Compressor::Gzip, &sample_entries()));
    let out = scratch("dest-out");
    fs::create_dir_all(&out).expect("dir");
    fs::write(out.join("precious.txt"), b"keep me").expect("file");
    assert!(matches!(
        unpack(&image, &out, &Limits::default()),
        Err(TranslateError::Usage(_))
    ));
    assert_eq!(
        fs::read(out.join("precious.txt")).expect("untouched"),
        b"keep me"
    );
}

#[test]
fn something_that_is_not_an_appimage_is_rejected_before_any_extraction() {
    let image = write_image(
        "plain",
        b"\x7fELF but nothing else here, no AppImage marker at all ........................",
    );
    let out = scratch("plain-out");
    assert!(unpack(&image, &out, &Limits::default()).is_err());
    assert!(!out.exists(), "nothing is created for a rejected input");
}

#[test]
fn corrupted_images_never_panic_or_exceed_the_limits() {
    let original = image_bytes(Compressor::Gzip, &sample_entries());
    let limits = Limits {
        max_entries: 64,
        max_file_bytes: 1 << 20,
        max_total_bytes: 4 << 20,
    };
    for position in (0..original.len()).step_by(7) {
        let mut mutated = original.clone();
        mutated[position] ^= 0xFF;
        let _ = unpack_with("mutate", &mutated, &limits);
    }
    for length in (0..original.len()).step_by(53) {
        let _ = unpack_with("truncate", &original[..length], &limits);
    }
}
