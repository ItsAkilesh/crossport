// File: installer/tests/shortcut_win.rs
// Purpose: Create a real .lnk in a temporary folder and read its target back
// Author: Akilesh
// Date: 2026-11-05

#![cfg(windows)]

use std::path::Path;

use crossport_setup::paths::Scope;
use crossport_setup::win::shortcut::{create, folder, target_of, Link, Place};

#[test]
fn creates_and_reads_back_a_shortcut() {
    let dir = std::env::temp_dir().join(format!("crossport-lnk-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let target = Path::new(r"C:\Windows\System32\notepad.exe");
    let lnk = dir.join("Test App.lnk");
    let link = Link { target, arguments: "--x", working_dir: &dir, description: "test", icon: None };
    create(&lnk, &link).unwrap();
    assert!(lnk.is_file());
    assert_eq!(target_of(&lnk).unwrap().to_lowercase(), target.to_string_lossy().to_lowercase());
    std::fs::remove_dir_all(&dir).unwrap();
}

#[test]
fn shortcut_folders_exist_for_both_scopes() {
    for scope in [Scope::User, Scope::Machine] {
        for place in [Place::StartMenu, Place::Desktop] {
            assert!(folder(scope, place).is_some(), "{scope:?} {place:?}");
        }
    }
}
