// File: fuzz/fuzz_targets/fuzz_translate_exe.rs
// Purpose: cargo-fuzz entry point for the translate_exe target
// Author: Akilesh
// Date: 2026-11-05

#![no_main]

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    crossport_fuzz::translate_exe(data);
});
