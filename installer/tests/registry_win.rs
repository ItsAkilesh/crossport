// File: installer/tests/registry_win.rs
// Purpose: Write the planned registry entries into a scratch key under HKCU, read them back, and delete them
// Author: Akilesh
// Date: 2026-11-05

#![cfg(windows)]

use std::path::Path;

use crossport_setup::paths::Scope;
use crossport_setup::regplan::{association_entries, uninstall_entries, uninstall_key, Hive, UninstallInfo};
use crossport_setup::win::registry::Registry;

fn scratch(name: &str) -> Registry {
    Registry { redirect: Some(format!(r"Software\CrossportSetupTest\{name}-{}", std::process::id())) }
}

#[test]
fn uninstall_entries_round_trip_and_delete() {
    let registry = scratch("uninstall");
    let dir = Path::new(r"C:\Program Files\Crossport\Lua");
    let info = UninstallInfo {
        id: "Crossport.App.Lua",
        display_name: "Lua",
        version: "5.4",
        install_dir: dir,
        uninstall_command: "\"u.exe\" --uninstall",
        icon: None,
        size_kb: 10,
    };
    registry.apply(&uninstall_entries(Scope::User, &info)).unwrap();
    let key = uninstall_key("Crossport.App.Lua");
    assert_eq!(registry.read_text(Hive::CurrentUser, &key, "DisplayName").as_deref(), Some("Lua"));
    assert_eq!(
        registry.read_text(Hive::CurrentUser, &key, "InstallLocation").as_deref(),
        Some(r"C:\Program Files\Crossport\Lua")
    );
    registry.delete_tree(Hive::CurrentUser, &key).unwrap();
    assert_eq!(registry.read_text(Hive::CurrentUser, &key, "DisplayName"), None);
    registry.delete_tree(Hive::CurrentUser, &key).unwrap();
    let root = registry.redirect.clone().unwrap();
    Registry::default().delete_tree(Hive::CurrentUser, &root).unwrap();
}

#[test]
fn association_entries_are_readable() {
    let registry = scratch("assoc");
    registry.apply(&association_entries(Scope::User, Path::new(r"C:\Crossport"))).unwrap();
    let open = registry
        .read_text(Hive::CurrentUser, r"Software\Classes\Crossport.AppImage\shell\open\command", "")
        .unwrap();
    assert_eq!(open, "\"C:\\Crossport\\crossport-open.exe\" \"%1\"");
    let ext = registry.read_text(Hive::CurrentUser, r"Software\Classes\.AppImage", "").unwrap();
    assert_eq!(ext, "Crossport.AppImage");
    let root = registry.redirect.clone().unwrap();
    Registry::default().delete_tree(Hive::CurrentUser, &root).unwrap();
}

#[test]
fn path_value_is_written_expandable() {
    let registry = scratch("path");
    registry.write_path(Hive::CurrentUser, "Environment", r"C:\a;C:\b").unwrap();
    assert_eq!(
        registry.read_text(Hive::CurrentUser, "Environment", "Path").as_deref(),
        Some(r"C:\a;C:\b")
    );
    let root = registry.redirect.clone().unwrap();
    Registry::default().delete_tree(Hive::CurrentUser, &root).unwrap();
}
