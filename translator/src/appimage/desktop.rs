// File: translator/src/appimage/desktop.rs
// Purpose: Find an AppImage's real program from its .desktop file when AppRun is a shell script Crossport cannot run
// Author: Akilesh
// Date: 2026-10-29
//
// Most graphical AppImages start through an AppRun shell script that sets the environment and then runs the
// real program. The desktop entry names that program in `Exec=`, so the program can be found without running
// any script. The environment a script would have set is not reproduced here.

use std::fs::{self, File};
use std::io::Read;
use std::path::Path;

const DESKTOP_SECTION: &str = "[Desktop Entry]";
const EXEC_KEY: &str = "Exec";
/// Directories of an AppDir searched for the program, in order.
const PROGRAM_DIRECTORIES: &[&[&str]] = &[
    &["usr", "bin"],
    &["bin"],
    &["usr", "sbin"],
    &["usr", "games"],
];
const ELF_MAGIC: [u8; 4] = [0x7f, b'E', b'L', b'F'];

/// The first word of `Exec=` in the main section of a desktop entry, without any directory part.
pub(super) fn exec_program(text: &str) -> Option<String> {
    let mut in_section = false;
    for line in text.lines().map(str::trim) {
        if line.starts_with('[') {
            in_section = line == DESKTOP_SECTION;
        } else if in_section {
            if let Some(value) = line
                .strip_prefix(EXEC_KEY)
                .and_then(|rest| rest.trim_start().strip_prefix('='))
            {
                return first_word(value.trim());
            }
        }
    }
    None
}

fn first_word(command: &str) -> Option<String> {
    let word = match command.strip_prefix('"') {
        Some(quoted) => quoted.split('"').next()?,
        None => command.split_whitespace().next()?,
    };
    let name = word.rsplit('/').next()?;
    let is_safe = !name.is_empty() && name != "." && name != ".." && !name.contains('\\');
    is_safe.then(|| name.to_string())
}

fn is_elf(path: &Path) -> bool {
    let mut magic = [0u8; 4];
    File::open(path)
        .and_then(|mut file| file.read_exact(&mut magic))
        .is_ok()
        && magic == ELF_MAGIC
}

/// Archive-relative components of the program the AppDir's desktop entry launches, if it is an ELF file.
pub(super) fn find_program(root: &Path) -> Option<Vec<String>> {
    let mut entries: Vec<_> = fs::read_dir(root).ok()?.flatten().collect();
    entries.sort_by_key(|entry| entry.file_name());
    for entry in entries {
        let name = entry.file_name().to_string_lossy().into_owned();
        if !name.to_ascii_lowercase().ends_with(".desktop") {
            continue;
        }
        let program = fs::read_to_string(entry.path())
            .ok()
            .and_then(|text| exec_program(&text));
        if let Some(found) = program.and_then(|program| locate(root, &program)) {
            return Some(found);
        }
    }
    None
}

fn locate(root: &Path, program: &str) -> Option<Vec<String>> {
    PROGRAM_DIRECTORIES.iter().find_map(|directory| {
        let mut components: Vec<String> = directory.iter().map(|part| part.to_string()).collect();
        components.push(program.to_string());
        let path = components
            .iter()
            .fold(root.to_path_buf(), |path, part| path.join(part));
        is_elf(&path).then_some(components)
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    const ELF_BYTES: [u8; 8] = [0x7f, b'E', b'L', b'F', 2, 1, 1, 0];

    fn temp_dir(name: &str) -> std::path::PathBuf {
        let dir = std::env::temp_dir().join(format!("crossport-desktop-{name}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).expect("creates");
        dir
    }

    #[test]
    fn exec_takes_the_first_word_without_its_directory() {
        let text = "[Desktop Entry]\nName=App\nExec=/usr/bin/app --flag %U\nIcon=app\n";
        assert_eq!(exec_program(text).as_deref(), Some("app"));
    }

    #[test]
    fn exec_handles_quotes_and_spacing() {
        let text = "[Desktop Entry]\nExec = \"my app\" %f\n";
        assert_eq!(exec_program(text).as_deref(), Some("my app"));
    }

    #[test]
    fn only_the_main_section_counts() {
        let text = "[Desktop Action New]\nExec=other\n[Desktop Entry]\nExec=real\n";
        assert_eq!(exec_program(text).as_deref(), Some("real"));
        assert_eq!(exec_program("[Desktop Action X]\nExec=a\n"), None);
    }

    #[test]
    fn unsafe_or_empty_commands_are_ignored() {
        assert_eq!(exec_program("[Desktop Entry]\nExec=\n"), None);
        assert_eq!(exec_program("[Desktop Entry]\nExec=..\n"), None);
        assert_eq!(exec_program("[Desktop Entry]\nName=x\n"), None);
    }

    #[test]
    fn the_program_is_found_in_usr_bin_and_must_be_an_elf_file() {
        let root = temp_dir("found");
        fs::create_dir_all(root.join("usr/bin")).expect("creates");
        fs::write(root.join("app.desktop"), "[Desktop Entry]\nExec=app %U\n").expect("writes");
        fs::write(root.join("usr/bin/app"), b"#!/bin/sh\n").expect("writes");
        assert_eq!(find_program(&root), None, "a script is not a program");
        fs::write(root.join("usr/bin/app"), ELF_BYTES).expect("writes");
        assert_eq!(
            find_program(&root),
            Some(vec![
                "usr".to_string(),
                "bin".to_string(),
                "app".to_string()
            ])
        );
    }

    #[test]
    fn no_desktop_file_means_no_program() {
        assert_eq!(find_program(&temp_dir("empty")), None);
    }
}
