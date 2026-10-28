// File: translator/src/lib.rs
// Purpose: Crossport translator library: analyze an ELF executable and relink it as a PE image
// Author: Akilesh
// Date: 2026-10-28

pub mod analyze;
pub mod base_reloc;
pub mod bundle;
pub mod cli;
pub mod elf_model;
pub mod error;
pub mod imports;
pub mod pe_model;
pub mod pe_writer;
pub mod relink;
pub mod stub;
