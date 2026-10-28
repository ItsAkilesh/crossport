// File: translator/src/shim_abi.rs
// Purpose: Constants shared with shim/crossport_libc.c; the one place the translator and shim agree on names and layout
// Author: Akilesh
// Date: 2026-10-28

/// File name of the shim DLL every translated module may import from.
pub const SHIM_DLL: &str = "crossport_libc.dll";
/// Shim function the executable's entry stub jumps to.
pub const START_SYMBOL: &str = "crossport_start";
/// Data symbol each translated DLL exports; it points at the module's start-info table.
pub const INFO_SYMBOL: &str = "__crossport_info";

/// "CPRT" read as a little-endian u32.
pub const INFO_MAGIC: u32 = 0x5452_5043;
pub const INFO_VERSION: u32 = 2;

/// Fixed header: 14 little-endian u32 fields, in the order of `startup::encode_info`.
pub const INFO_HEADER_BYTES: usize = 56;
/// Size of every binding and copy record (two u32 values and one 64-bit value).
pub const RECORD_BYTES: usize = 16;
/// Largest number of dependency modules one executable may list.
pub const MAX_LISTED_MODULES: usize = 256;
