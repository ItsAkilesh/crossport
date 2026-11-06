// File: fuzz/fuzz_targets/fuzz_analyze_elf.rs
// Purpose: cargo-fuzz entry point for the analyze_elf target
// Author: Akilesh
// Date: 2026-11-05

#![no_main]

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    crossport_fuzz::analyze_elf(data);
});
