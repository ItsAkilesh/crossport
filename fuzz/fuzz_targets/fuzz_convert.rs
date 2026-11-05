// File: fuzz/fuzz_targets/fuzz_convert.rs
// Purpose: cargo-fuzz entry point for the convert target
// Author: Akilesh
// Date: 2026-11-05

#![no_main]

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    crossport_fuzz::convert(data);
});
