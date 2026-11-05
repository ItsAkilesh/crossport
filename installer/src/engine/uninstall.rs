// File: installer/src/engine/uninstall.rs
// Purpose: Remove an install from its manifest: shortcuts, registry keys, PATH entries, then the files
// Author: Akilesh
// Date: 2026-11-05

use std::fs;
use std::io::ErrorKind;
use std::path::Path;

use super::Env;
use crate::manifest::{Manifest, MANIFEST_FILE};
use crate::regplan::{path_location, EXTENSION, PROG_ID, remove_from_path, Hive};

/// Remove everything the manifest in `dir` lists, then the folder. Returns notes about anything left behind.
pub fn uninstall(dir: &Path, env: &Env) -> Result<Vec<String>, String> {
    let text = fs::read_to_string(dir.join(MANIFEST_FILE))
        .map_err(|e| format!("{} has no uninstall record: {e}", dir.display()))?;
    let manifest = Manifest::parse(&text).map_err(|e| format!("the uninstall record is damaged: {e:?}"))?;
    let mut notes = Vec::new();
    for shortcut in &manifest.shortcuts {
        ignore_missing(fs::remove_file(shortcut), &mut notes, &shortcut.display().to_string());
    }
    for (hive, key) in &manifest.keys {
        if let Some(hive) = Hive::from_name(hive) {
            env.registry.delete_tree(hive, key).unwrap_or_else(|e| notes.push(e));
        }
    }
    drop_extension_key(env, &manifest);
    for entry in &manifest.path_entries {
        edit_path(env, &manifest, entry).unwrap_or_else(|e| notes.push(e));
    }
    remove_files(&manifest, dir, &mut notes);
    Ok(notes)
}

/// Remove `.AppImage` itself only when it still names our ProgID, so another program's choice survives.
fn drop_extension_key(env: &Env, manifest: &Manifest) {
    let hive = Hive::for_scope(manifest.scope);
    let key = format!(r"Software\Classes\{EXTENSION}");
    if env.registry.read_text(hive, &key, "").as_deref() == Some(PROG_ID) {
        let _ = env.registry.delete_tree(hive, &key);
    }
}

fn ignore_missing(result: std::io::Result<()>, notes: &mut Vec<String>, what: &str) {
    match result {
        Err(e) if e.kind() != ErrorKind::NotFound => notes.push(format!("could not remove {what}: {e}")),
        _ => {}
    }
}

fn edit_path(env: &Env, manifest: &Manifest, entry: &Path) -> Result<(), String> {
    let (hive, key) = path_location(manifest.scope);
    let current = env.registry.read_text(hive, key, "Path").unwrap_or_default();
    let updated = remove_from_path(&current, entry);
    if updated != current {
        env.registry.write_path(hive, key, &updated)?;
        if env.registry.redirect.is_none() {
            crate::win::registry::announce_environment_change();
        }
    }
    Ok(())
}

fn remove_files(manifest: &Manifest, dir: &Path, notes: &mut Vec<String>) {
    if manifest.files.is_empty() {
        ignore_missing(fs::remove_dir_all(dir), notes, &dir.display().to_string());
        return;
    }
    for file in &manifest.files {
        let path = file.split('/').fold(dir.to_path_buf(), |p, part| p.join(part));
        ignore_missing(fs::remove_file(&path), notes, &path.display().to_string());
    }
    ignore_missing(fs::remove_file(dir.join(MANIFEST_FILE)), notes, MANIFEST_FILE);
    remove_empty_dirs(dir);
    if dir.is_dir() {
        notes.push(format!("{} still holds other files and was kept", dir.display()));
    }
}

/// Delete `dir` and any empty folders beneath it, leaving folders that still hold files.
fn remove_empty_dirs(dir: &Path) {
    for entry in fs::read_dir(dir).into_iter().flatten().flatten() {
        if entry.path().is_dir() {
            remove_empty_dirs(&entry.path());
        }
    }
    let _ = fs::remove_dir(dir);
}
