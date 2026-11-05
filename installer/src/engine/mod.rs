// File: installer/src/engine/mod.rs
// Purpose: The environment an install runs in (registry, shortcut folders, tools) shared by every operation
// Author: Akilesh
// Date: 2026-11-05

pub mod app;
pub mod selfinstall;
pub mod uninstall;

use std::path::{Path, PathBuf};

use crate::paths::Scope;
use crate::win::registry::Registry;
use crate::win::shortcut::{folder, Place};

/// File name every install gets for its own uninstaller.
pub const UNINSTALLER: &str = "uninstall.exe";
pub const TRANSLATOR: &str = "crossport.exe";
pub const SHIM: &str = "crossport_libc.dll";
pub const RUNTIME_DIR: &str = "runtime";

/// Where an install writes outside its own folder. Tests point these at scratch locations.
#[derive(Debug, Clone)]
pub struct Env {
    pub registry: Registry,
    pub start_menu: PathBuf,
    pub desktop: PathBuf,
}

impl Env {
    /// The real registry and shortcut folders for a scope.
    pub fn system(scope: Scope) -> Result<Env, String> {
        let find = |place| folder(scope, place).ok_or("could not find the shortcut folders".to_string());
        Ok(Env { registry: Registry::default(), start_menu: find(Place::StartMenu)?, desktop: find(Place::Desktop)? })
    }
}

/// The Crossport files the launcher uses to convert an AppImage.
#[derive(Debug, Clone)]
pub struct Tools {
    pub translator: PathBuf,
    pub shim: PathBuf,
    pub runtime: Option<PathBuf>,
}

impl Tools {
    /// Tools installed beside the running program, in `dir`.
    pub fn beside(dir: &Path) -> Tools {
        let runtime = dir.join(RUNTIME_DIR);
        Tools {
            translator: dir.join(TRANSLATOR),
            shim: dir.join(SHIM),
            runtime: runtime.is_dir().then_some(runtime),
        }
    }

    /// Why the tools cannot be used, if they cannot.
    pub fn problem(&self) -> Option<String> {
        [&self.translator, &self.shim]
            .iter()
            .find(|p| !p.is_file())
            .map(|p| format!("{} is missing. Reinstall Crossport.", p.display()))
    }
}

/// Size of a folder's files in kilobytes (rounded up, capped), for Apps and features.
pub fn folder_size_kb(dir: &Path) -> u32 {
    fn total(dir: &Path) -> u64 {
        std::fs::read_dir(dir)
            .into_iter()
            .flatten()
            .flatten()
            .map(|e| match e.metadata() {
                Ok(m) if m.is_dir() => total(&e.path()),
                Ok(m) => m.len(),
                Err(_) => 0,
            })
            .sum()
    }
    u32::try_from(total(dir).div_ceil(1024)).unwrap_or(u32::MAX)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tools_report_what_is_missing() {
        let dir = std::env::temp_dir().join(format!("crossport-tools-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let tools = Tools::beside(&dir);
        assert!(tools.problem().unwrap().contains("crossport.exe"));
        std::fs::write(dir.join(TRANSLATOR), b"x").unwrap();
        std::fs::write(dir.join(SHIM), b"x").unwrap();
        assert!(Tools::beside(&dir).problem().is_none());
        assert!(Tools::beside(&dir).runtime.is_none());
        std::fs::create_dir(dir.join(RUNTIME_DIR)).unwrap();
        assert!(Tools::beside(&dir).runtime.is_some());
        assert_eq!(folder_size_kb(&dir), 1);
        std::fs::remove_dir_all(&dir).unwrap();
    }
}
