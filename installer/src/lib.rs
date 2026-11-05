// File: installer/src/lib.rs
// Purpose: Library behind the Crossport setup program and the AppImage launcher
// Author: Akilesh
// Date: 2026-11-05

pub mod appimage_meta;
pub mod cli;
pub mod command;
#[cfg(windows)]
pub mod engine;
pub mod ico;
pub mod manifest;
pub mod names;
#[cfg(windows)]
pub mod ops;
pub mod paths;
pub mod payload;
pub mod regplan;
pub mod report;

#[cfg(windows)]
pub mod win;

#[cfg(windows)]
pub mod ui;
