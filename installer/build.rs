// File: installer/build.rs
// Purpose: Embed the application manifest into both programs (MSVC linker only) and fix the version a release stamps in
// Author: Akilesh
// Date: 2026-11-05

/// The version both programs report: a release's tag (`CROSSPORT_RELEASE_VERSION`, with or without `v`) or the crate's own.
fn release_version() -> String {
    println!("cargo:rerun-if-env-changed=CROSSPORT_RELEASE_VERSION");
    let own = std::env::var("CARGO_PKG_VERSION").unwrap_or_default();
    let Ok(tag) = std::env::var("CROSSPORT_RELEASE_VERSION") else { return own };
    let version = tag.trim().trim_start_matches('v').to_string();
    let numbers: Vec<&str> = version.split('.').collect();
    let is_plain = numbers.len() == 3 && numbers.iter().all(|n| !n.is_empty() && n.bytes().all(|b| b.is_ascii_digit()));
    assert!(is_plain, "CROSSPORT_RELEASE_VERSION must look like 1.2.3 or v1.2.3, got {tag:?}");
    version
}

fn main() {
    println!("cargo:rustc-env=CROSSPORT_VERSION={}", release_version());
    let target = std::env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    let os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    println!("cargo:rerun-if-changed=crossport.manifest");
    if os == "windows" && target == "msvc" {
        let dir = std::env::var("CARGO_MANIFEST_DIR").unwrap_or_default();
        println!("cargo:rustc-link-arg-bins=/MANIFEST:EMBED");
        println!("cargo:rustc-link-arg-bins=/MANIFESTINPUT:{dir}/crossport.manifest");
    }
}
