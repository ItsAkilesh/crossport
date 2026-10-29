// File: translator/tests/appdir_search.rs
// Purpose: Library search inside an AppDir: RUNPATH with $ORIGIN, default directories and confinement to the AppDir
// Author: Akilesh
// Date: 2026-10-28

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};

use crossport_translator::error::TranslateError;
use crossport_translator::naming::ModuleName;
use crossport_translator::shim_exports::ShimExports;
use crossport_translator::source::AppDirSource;
use crossport_translator::translate::{translate, Missing};

const EXE: &[u8] = include_bytes!("fixtures/two_libs/two_libs");
const GREET: &[u8] = include_bytes!("fixtures/two_libs/libgreet.so.1");
const MATH: &[u8] = include_bytes!("fixtures/two_libs/libmath.so.1");

fn scratch(label: &str) -> PathBuf {
    static NEXT: AtomicUsize = AtomicUsize::new(0);
    let dir = std::env::temp_dir().join(format!(
        "crossport-appdir-{label}-{}-{}",
        std::process::id(),
        NEXT.fetch_add(1, Ordering::Relaxed)
    ));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).expect("dir");
    dir
}

fn write(path: &Path, bytes: &[u8]) {
    fs::create_dir_all(path.parent().expect("has a parent")).expect("dirs");
    fs::write(path, bytes).expect("file");
}

fn shim() -> ShimExports {
    ShimExports::from_names(["puts", "__libc_start_main", "crossport_start"].map(String::from))
}

fn translate_in(root: &Path, executable: &Path) -> Result<usize, TranslateError> {
    let source = AppDirSource::new(root.to_path_buf(), executable.to_path_buf(), &[])?;
    let name = ModuleName::parse("two_libs").expect("valid");
    translate(name, EXE.to_vec(), &source, &shim(), Missing::Fail).map(|t| t.files.len())
}

#[test]
fn runpath_origin_finds_libraries_next_to_the_executable() {
    let root = scratch("origin");
    let exe = root.join("opt/app/two_libs");
    write(&exe, EXE);
    write(&root.join("opt/app/libgreet.so.1"), GREET);
    write(&root.join("opt/app/libmath.so.1"), MATH);
    assert_eq!(translate_in(&root, &exe).expect("translates"), 3);
}

#[test]
fn the_default_library_directories_are_searched_last() {
    let root = scratch("defaults");
    let exe = root.join("usr/bin/two_libs");
    write(&exe, EXE);
    write(&root.join("usr/lib/libgreet.so.1"), GREET);
    write(&root.join("usr/lib/x86_64-linux-gnu/libmath.so.1"), MATH);
    assert_eq!(translate_in(&root, &exe).expect("translates"), 3);
}

#[test]
fn libraries_outside_the_appdir_are_never_read() {
    let root = scratch("confined");
    let exe = root.join("usr/bin/two_libs");
    write(&exe, EXE);
    let outside = scratch("outside");
    write(&outside.join("libgreet.so.1"), GREET);
    write(&outside.join("libmath.so.1"), MATH);
    let result = translate_in(&root, &exe);
    assert!(
        matches!(result, Err(TranslateError::MissingLibrary { .. })),
        "{result:?}"
    );
}

#[test]
fn a_nearer_directory_wins_over_a_default_one() {
    let root = scratch("order");
    let exe = root.join("usr/bin/two_libs");
    write(&exe, EXE);
    write(&root.join("usr/bin/libgreet.so.1"), GREET);
    write(&root.join("usr/bin/libmath.so.1"), MATH);
    write(
        &root.join("usr/lib/libgreet.so.1"),
        b"this decoy must not be chosen",
    );
    assert_eq!(
        translate_in(&root, &exe).expect("the RUNPATH copy is used, not the decoy"),
        3
    );
}

#[test]
fn an_executable_outside_the_appdir_is_a_usage_error() {
    let root = scratch("outside-exe");
    let elsewhere = scratch("elsewhere").join("two_libs");
    let result = AppDirSource::new(root, elsewhere, &[]);
    assert!(matches!(result, Err(TranslateError::Usage(_))));
}

#[test]
fn a_directory_or_a_symlink_named_like_a_library_is_not_read_as_one() {
    let root = scratch("isdir");
    let exe = root.join("usr/bin/two_libs");
    write(&exe, EXE);
    fs::create_dir_all(root.join("usr/bin/libgreet.so.1"))
        .expect("a directory with the library's name");
    write(&root.join("usr/lib/libgreet.so.1"), GREET);
    write(&root.join("usr/lib/libmath.so.1"), MATH);
    assert_eq!(
        translate_in(&root, &exe).expect("falls through to usr/lib"),
        3
    );
}

#[cfg(unix)]
#[test]
fn a_symlink_that_leaves_the_appdir_is_not_followed() {
    let root = scratch("symlink-escape");
    let exe = root.join("usr/bin/two_libs");
    write(&exe, EXE);
    let outside = scratch("symlink-target");
    write(&outside.join("libgreet.so.1"), GREET);
    write(&outside.join("libmath.so.1"), MATH);
    fs::create_dir_all(root.join("usr/lib")).expect("dir");
    std::os::unix::fs::symlink(
        outside.join("libgreet.so.1"),
        root.join("usr/lib/libgreet.so.1"),
    )
    .expect("symlink");
    std::os::unix::fs::symlink(
        outside.join("libmath.so.1"),
        root.join("usr/lib/libmath.so.1"),
    )
    .expect("symlink");
    let result = translate_in(&root, &exe);
    assert!(
        matches!(result, Err(TranslateError::MissingLibrary { .. })),
        "{result:?}"
    );
}

#[cfg(unix)]
#[test]
fn a_symlink_that_stays_inside_the_appdir_is_followed() {
    let root = scratch("symlink-inside");
    let exe = root.join("usr/bin/two_libs");
    write(&exe, EXE);
    write(&root.join("usr/lib/libgreet.so.1.0"), GREET);
    write(&root.join("usr/lib/libmath.so.1.0"), MATH);
    std::os::unix::fs::symlink("libgreet.so.1.0", root.join("usr/lib/libgreet.so.1"))
        .expect("symlink");
    std::os::unix::fs::symlink("libmath.so.1.0", root.join("usr/lib/libmath.so.1"))
        .expect("symlink");
    assert_eq!(translate_in(&root, &exe).expect("translates"), 3);
}
