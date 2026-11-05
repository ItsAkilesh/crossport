// File: installer/tests/meta_real.rs
// Purpose: Read the name and icon of a real AppImage named by CROSSPORT_TEST_APPIMAGE (skipped when unset)
// Author: Akilesh
// Date: 2026-11-05

use std::path::PathBuf;

use crossport_setup::appimage_meta::read;
use crossport_setup::ico::png_to_ico;

#[test]
fn reads_name_and_icon_from_a_real_image() {
    let Some(path) = std::env::var_os("CROSSPORT_TEST_APPIMAGE").map(PathBuf::from) else {
        eprintln!("CROSSPORT_TEST_APPIMAGE not set, skipping");
        return;
    };
    let meta = read(&path).expect("the image should be readable");
    let expected = std::env::var("CROSSPORT_TEST_APPIMAGE_NAME").unwrap_or_else(|_| "KeePassXC".into());
    assert_eq!(meta.name.as_deref(), Some(expected.as_str()));
    let png = meta.icon_png.expect("the image should carry a PNG icon");
    assert!(png.len() > 100);
    let _ = png_to_ico(&png);
}
