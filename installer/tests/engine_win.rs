// File: installer/tests/engine_win.rs
// Purpose: Install and uninstall Crossport and one app end to end against scratch folders and a scratch registry key
// Author: Akilesh
// Date: 2026-11-05

#![cfg(windows)]

use std::fs;
use std::path::{Path, PathBuf};

use crossport_setup::engine::app::{install_app, AppRequest};
use crossport_setup::engine::selfinstall::{install_self, SelfOptions};
use crossport_setup::engine::uninstall::uninstall;
use crossport_setup::engine::{Env, Tools};
use crossport_setup::manifest::MANIFEST_FILE;
use crossport_setup::paths::Scope;
use crossport_setup::payload::Entry;
use crossport_setup::regplan::{path_location, uninstall_key, Hive};
use crossport_setup::win::registry::Registry;

struct Sandbox {
    base: PathBuf,
    env: Env,
}

impl Sandbox {
    fn new(tag: &str) -> Sandbox {
        let base = std::env::temp_dir().join(format!("crossport-engine-{tag}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&base);
        let (menu, desktop) = (base.join("menu"), base.join("desktop"));
        fs::create_dir_all(&menu).unwrap();
        fs::create_dir_all(&desktop).unwrap();
        let registry = Registry { redirect: Some(format!(r"Software\CrossportSetupTest\{tag}-{}", std::process::id())) };
        Sandbox { base, env: Env { registry, start_menu: menu, desktop } }
    }
}

impl Drop for Sandbox {
    fn drop(&mut self) {
        let root = self.env.registry.redirect.clone().unwrap();
        let _ = Registry::default().delete_tree(Hive::CurrentUser, &root);
        let _ = fs::remove_dir_all(&self.base);
    }
}

fn entry(path: &str) -> Entry {
    Entry { path: path.into(), data: path.as_bytes().to_vec() }
}

#[test]
fn crossport_installs_and_uninstalls_completely() {
    let sb = Sandbox::new("self");
    let dir = sb.base.join("Program Files").join("Crossport");
    let entries = [entry("crossport-open.exe"), entry("uninstall.exe"), entry("runtime/lib/libx.so")];
    let opts = SelfOptions { dir: dir.clone(), scope: Scope::User, start_menu: true, desktop: true, add_to_path: true, associate: true };
    install_self(&entries, &opts, &sb.env).unwrap();

    assert!(dir.join("runtime").join("lib").join("libx.so").is_file());
    assert!(sb.env.start_menu.join("Crossport.lnk").is_file());
    assert!(sb.env.desktop.join("Crossport.lnk").is_file());
    let reg = &sb.env.registry;
    assert_eq!(reg.read_text(Hive::CurrentUser, &uninstall_key("Crossport"), "DisplayName").as_deref(), Some("Crossport"));
    let (hive, key) = path_location(Scope::User);
    assert!(reg.read_text(hive, key, "Path").unwrap().contains("Crossport"));
    assert!(reg.read_text(Hive::CurrentUser, r"Software\Classes\.AppImage", "").is_some());

    let notes = uninstall(&dir, &sb.env).unwrap();
    assert!(notes.is_empty(), "{notes:?}");
    assert!(!dir.exists());
    assert!(!sb.env.start_menu.join("Crossport.lnk").exists());
    assert_eq!(reg.read_text(Hive::CurrentUser, &uninstall_key("Crossport"), "DisplayName"), None);
    assert_eq!(reg.read_text(hive, key, "Path").as_deref(), Some(""));
    assert_eq!(reg.read_text(Hive::CurrentUser, r"Software\Classes\.AppImage", ""), None);
}

#[test]
fn uninstall_keeps_apps_installed_under_the_product_folder() {
    let sb = Sandbox::new("keep");
    let dir = sb.base.join("Crossport");
    let opts = SelfOptions { dir: dir.clone(), scope: Scope::User, start_menu: false, desktop: false, add_to_path: false, associate: false };
    install_self(&[entry("a.exe")], &opts, &sb.env).unwrap();
    fs::create_dir_all(dir.join("SomeApp")).unwrap();
    fs::write(dir.join("SomeApp").join("x.exe"), b"x").unwrap();
    let notes = uninstall(&dir, &sb.env).unwrap();
    assert!(notes.iter().any(|n| n.contains("kept")));
    assert!(dir.join("SomeApp").join("x.exe").is_file());
    assert!(!dir.join("a.exe").exists() && !dir.join(MANIFEST_FILE).exists());
}

fn fake_tools(sb: &Sandbox, script: &str) -> Tools {
    let translator = sb.base.join("crossport.bat");
    fs::write(&translator, script).unwrap();
    let shim = sb.base.join("crossport_libc.dll");
    fs::write(&shim, b"x").unwrap();
    Tools { translator, shim, runtime: None }
}

const WORKING: &str = "@echo off\r\nmkdir \"%~6\"\r\ncopy nul \"%~6\\lua.exe\" >nul\r\necho program: usr/bin/lua\r\necho wrote %~6\\lua.exe (2 modules)\r\n";
const FAILING: &str = "@echo off\r\nmkdir \"%~6\"\r\necho crossport: library libstdc++.so.6 was not found in any --lib-dir 1>&2\r\nexit /b 1\r\n";

fn request<'a>(dir: &'a Path, launcher: &'a Path) -> AppRequest<'a> {
    AppRequest { image: Path::new("Lua.AppImage"), name: "Lua", dir, scope: Scope::User, start_menu: true, desktop: false, stub_missing: false, icon_png: None, launcher }
}

#[test]
fn an_app_installs_registers_and_uninstalls() {
    let sb = Sandbox::new("app");
    let launcher = sb.base.join("crossport-open.exe");
    fs::write(&launcher, b"launcher").unwrap();
    let dir = sb.base.join("Apps").join("Lua");
    let outcome = install_app(&request(&dir, &launcher), &fake_tools(&sb, WORKING), &sb.env).unwrap();
    assert!(outcome.report.succeeded);
    assert_eq!(outcome.exe.unwrap().file_name().unwrap(), "lua.exe");
    assert!(dir.join("uninstall.exe").is_file() && dir.join(MANIFEST_FILE).is_file());
    assert!(sb.env.start_menu.join("Lua.lnk").is_file() && !sb.env.desktop.join("Lua.lnk").exists());
    let key = uninstall_key("Crossport.App.Lua");
    let command = sb.env.registry.read_text(Hive::CurrentUser, &key, "UninstallString").unwrap();
    assert!(command.contains("uninstall.exe") && command.contains("--uninstall"));

    assert!(uninstall(&dir, &sb.env).unwrap().is_empty());
    assert!(!dir.exists() && !sb.env.start_menu.join("Lua.lnk").exists());
    assert_eq!(sb.env.registry.read_text(Hive::CurrentUser, &key, "DisplayName"), None);
}

#[test]
fn a_failed_conversion_leaves_nothing_behind_and_explains() {
    let sb = Sandbox::new("fail");
    let launcher = sb.base.join("crossport-open.exe");
    fs::write(&launcher, b"launcher").unwrap();
    let dir = sb.base.join("Apps").join("Lua");
    let outcome = install_app(&request(&dir, &launcher), &fake_tools(&sb, FAILING), &sb.env).unwrap();
    assert!(!outcome.report.succeeded && outcome.exe.is_none());
    assert!(outcome.report.findings[0].summary.contains("libstdc++.so.6"));
    assert!(!dir.exists());
    assert_eq!(sb.env.registry.read_text(Hive::CurrentUser, &uninstall_key("Crossport.App.Lua"), "DisplayName"), None);
}

#[test]
fn a_folder_that_is_not_empty_is_refused() {
    let sb = Sandbox::new("busy");
    let launcher = sb.base.join("crossport-open.exe");
    fs::write(&launcher, b"x").unwrap();
    let dir = sb.base.join("Busy");
    fs::create_dir_all(&dir).unwrap();
    fs::write(dir.join("keep.txt"), b"mine").unwrap();
    let result = install_app(&request(&dir, &launcher), &fake_tools(&sb, WORKING), &sb.env);
    assert!(result.unwrap_err().contains("not empty"));
    assert!(dir.join("keep.txt").is_file());
}

#[test]
fn a_running_app_blocks_the_uninstall_before_anything_is_removed() {
    let sb = Sandbox::new("locked");
    let launcher = sb.base.join("crossport-open.exe");
    fs::write(&launcher, b"launcher").unwrap();
    let dir = sb.base.join("Apps").join("Lua");
    install_app(&request(&dir, &launcher), &fake_tools(&sb, WORKING), &sb.env).unwrap();
    let held = fs::File::open(dir.join("lua.exe")).unwrap();
    let error = uninstall(&dir, &sb.env).unwrap_err();
    assert!(error.contains("in use"), "{error}");
    assert!(dir.join("lua.exe").is_file() && sb.env.start_menu.join("Lua.lnk").is_file());
    assert!(sb.env.registry.read_text(Hive::CurrentUser, &uninstall_key("Crossport.App.Lua"), "DisplayName").is_some());
    drop(held);
    assert!(uninstall(&dir, &sb.env).unwrap().is_empty());
    assert!(!dir.exists());
}
