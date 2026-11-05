// File: fuzz/fuzz_targets/fuzz_unpack_squashfs.rs
// Purpose: cargo-fuzz entry point for the unpack_squashfs target
// Author: Akilesh
// Date: 2026-11-05

#![no_main]

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    crossport_fuzz::unpack_squashfs(data);
});
