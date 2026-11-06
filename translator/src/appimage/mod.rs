// File: translator/src/appimage/mod.rs
// Purpose: Unpack an AppImage (type 2) into an AppDir without running it, rejecting anything unsafe
// Author: Akilesh
// Date: 2026-11-06

mod desktop;
mod extract;
mod header;
mod junest;
pub(crate) mod paths;

pub use extract::{unpack, AppDir, Limits, UnpackReport};
