// File: translator/src/lib.rs
// Purpose: Crossport translator library: analyze ELF modules and relink them as PE images
// Author: Akilesh
// Date: 2026-10-28

pub mod analyze;
pub mod base_reloc;
pub mod bundle;
pub mod cli;
pub mod elf_model;
pub mod error;
pub mod exports;
pub mod graph;
pub mod imports;
pub mod naming;
pub mod pe_model;
pub mod pe_writer;
pub mod relink;
pub mod resolve;
pub mod shim_abi;
pub mod shim_exports;
pub mod source;
pub mod startup;
pub mod translate;
