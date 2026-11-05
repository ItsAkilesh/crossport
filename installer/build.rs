// File: installer/build.rs
// Purpose: Embed the application manifest into both programs (MSVC linker only; other targets skip it)
// Author: Akilesh
// Date: 2026-11-05

fn main() {
    let target = std::env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    let os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    println!("cargo:rerun-if-changed=crossport.manifest");
    if os == "windows" && target == "msvc" {
        let dir = std::env::var("CARGO_MANIFEST_DIR").unwrap_or_default();
        println!("cargo:rustc-link-arg-bins=/MANIFEST:EMBED");
        println!("cargo:rustc-link-arg-bins=/MANIFESTINPUT:{dir}/crossport.manifest");
    }
}
