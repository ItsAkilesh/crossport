// File: installer/src/lib.rs
// Purpose: Library behind the Crossport setup program and the AppImage launcher
// Author: Akilesh
// Date: 2026-11-05

pub mod appimage_meta;
pub mod command;
pub mod ico;
pub mod manifest;
pub mod names;
pub mod paths;
pub mod regplan;
pub mod report;

#[cfg(windows)]
pub mod win;
