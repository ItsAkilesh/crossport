// File: translator/src/shim_abi.rs
// Purpose: Constants shared with shim/crossport_libc.c; the one place the translator and shim agree on names and layout
// Author: Akilesh
// Date: 2026-10-29

/// File name of the shim DLL every translated module may import from.
pub const SHIM_DLL: &str = "crossport_libc.dll";
/// Shim function the executable's entry stub jumps to.
pub const START_SYMBOL: &str = "crossport_start";
/// The shim function that stands in for every symbol nobody provides, when stubbing is allowed.
pub const UNAVAILABLE_SYMBOL: &str = "crossport_unavailable";
/// Data symbol each translated DLL exports; it points at the module's start-info table.
pub const INFO_SYMBOL: &str = "__crossport_info";

/// "CPRT" read as a little-endian u32.
pub const INFO_MAGIC: u32 = 0x5452_5043;
pub const INFO_VERSION: u32 = 4;

/// Fixed header: 26 little-endian u32 fields, in the order of `startup::encode_info`.
pub const INFO_HEADER_BYTES: usize = 104;
/// Size of every binding and copy record (two u32 values and one 64-bit value).
pub const RECORD_BYTES: usize = 16;
/// Largest number of dependency modules one executable may list.
pub const MAX_LISTED_MODULES: usize = 256;
