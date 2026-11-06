// File: installer/src/engine/selfinstall.rs
// Purpose: Install Crossport itself from the setup payload, with its shortcuts, PATH entry and association
// Author: Akilesh
// Date: 2026-11-05

use std::fs;
use std::path::Path;

use super::uninstall::uninstall;
use super::{folder_size_kb, Env, UNINSTALLER};
use crate::command::quote_arg;
use crate::manifest::{Manifest, MANIFEST_FILE};
use crate::paths::Scope;
use crate::payload::{extract, Entry};
use crate::regplan::{
    add_to_path, association_entries, association_keys, path_location, uninstall_entries, uninstall_key, Hive,
    UninstallInfo,
};
use crate::win::shortcut::{create, Link};

pub const PRODUCT_ID: &str = "Crossport";
pub const PRODUCT_NAME: &str = "Crossport";
pub const PRODUCT_VERSION: &str = crate::update::VERSION;
const OPENER: &str = "crossport-open.exe";

/// The choices on the setup wizard's options page.
#[derive(Debug, Clone)]
pub struct SelfOptions {
    pub dir: std::path::PathBuf,
    pub scope: Scope,
    pub start_menu: bool,
    pub desktop: bool,
    pub add_to_path: bool,
    pub associate: bool,
}

/// Install Crossport. Anything partly done is undone if a step fails.
pub fn install_self(entries: &[Entry], opts: &SelfOptions, env: &Env) -> Result<(), String> {
    let written = extract(entries, &opts.dir).map_err(|e| format!("copying files to {}: {e}", opts.dir.display()))?;
    let mut manifest = Manifest::new(PRODUCT_NAME, opts.scope, opts.dir.clone());
    manifest.files = written.iter().map(|p| p.to_string_lossy().replace('\\', "/")).collect();
    let result = register(opts, env, &mut manifest);
    let saved = result.and_then(|_| {
        let text = manifest.serialize().map_err(|e| format!("{e:?}"))?;
        fs::write(opts.dir.join(MANIFEST_FILE), text).map_err(|e| e.to_string())
    });
    if saved.is_err() {
        let _ = fs::write(opts.dir.join(MANIFEST_FILE), manifest.serialize().unwrap_or_default());
        let _ = uninstall(&opts.dir, env);
    }
    saved
}

fn register(opts: &SelfOptions, env: &Env, manifest: &mut Manifest) -> Result<(), String> {
    let hive = Hive::for_scope(opts.scope);
    let opener = opts.dir.join(OPENER);
    let link = Link { target: &opener, arguments: "", working_dir: &opts.dir, description: "Install an AppImage with Crossport", icon: Some(&opener) };
    for (wanted, folder) in [(opts.start_menu, &env.start_menu), (opts.desktop, &env.desktop)] {
        if wanted {
            let path = folder.join(format!("{PRODUCT_NAME}.lnk"));
            create(&path, &link)?;
            manifest.shortcuts.push(path);
        }
    }
    let uninstaller = opts.dir.join(UNINSTALLER);
    let command = format!("{} --uninstall {}", quote_arg(&uninstaller.to_string_lossy()), quote_arg(&opts.dir.to_string_lossy()));
    let info = UninstallInfo {
        id: PRODUCT_ID,
        display_name: PRODUCT_NAME,
        version: PRODUCT_VERSION,
        install_dir: &opts.dir,
        uninstall_command: &command,
        icon: Some(&opener),
        size_kb: folder_size_kb(&opts.dir),
    };
    env.registry.apply(&uninstall_entries(opts.scope, &info))?;
    manifest.keys.push((hive.name().to_string(), uninstall_key(PRODUCT_ID)));
    if opts.add_to_path {
        add_path(opts, env, manifest)?;
    }
    if opts.associate {
        env.registry.apply(&association_entries(opts.scope, &opts.dir))?;
        for (hive, key) in association_keys(opts.scope) {
            manifest.keys.push((hive.name().to_string(), key));
        }
        if env.registry.redirect.is_none() {
            crate::win::announce_association_change();
        }
    }
    Ok(())
}

fn add_path(opts: &SelfOptions, env: &Env, manifest: &mut Manifest) -> Result<(), String> {
    let (hive, key) = path_location(opts.scope);
    let current = env.registry.read_text(hive, key, "Path").unwrap_or_default();
    let updated = add_to_path(&current, &opts.dir);
    if updated != current {
        env.registry.write_path(hive, key, &updated)?;
        manifest.path_entries.push(opts.dir.clone());
        if env.registry.redirect.is_none() {
            crate::win::registry::announce_environment_change();
        }
    }
    Ok(())
}

/// True when `dir` holds a Crossport install (its manifest exists).
pub fn is_installed(dir: &Path) -> bool {
    dir.join(MANIFEST_FILE).is_file()
}
