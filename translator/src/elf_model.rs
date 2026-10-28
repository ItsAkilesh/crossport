// File: translator/src/elf_model.rs
// Purpose: Plain data describing an analyzed ELF executable, independent of the parser
// Author: Akilesh
// Date: 2026-10-28

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SegmentFlags {
    pub is_readable: bool,
    pub is_writable: bool,
    pub is_executable: bool,
}

#[derive(Debug, Clone)]
pub struct LoadSegment {
    pub vaddr: u64,
    pub file_offset: u64,
    pub file_size: u64,
    pub mem_size: u64,
    pub flags: SegmentFlags,
}

/// An undefined dynamic symbol that must be bound to something outside the executable.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImportSymbol {
    pub name: String,
    pub is_weak: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RelocKind {
    /// `R_X86_64_RELATIVE`: the slot holds `load base + addend`.
    Relative { addend: i64 },
    /// `R_X86_64_GLOB_DAT` or `R_X86_64_JUMP_SLOT`: the slot holds the address of `imports[symbol]`.
    Import { symbol: usize },
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Relocation {
    pub offset: u64,
    pub kind: RelocKind,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ArrayRange {
    pub vaddr: u64,
    pub count: u64,
}

#[derive(Debug, Clone)]
pub struct ElfInfo {
    pub entry: u64,
    pub segments: Vec<LoadSegment>,
    pub needed: Vec<String>,
    pub imports: Vec<ImportSymbol>,
    pub relocations: Vec<Relocation>,
    pub init: Option<u64>,
    pub fini: Option<u64>,
    pub init_array: Option<ArrayRange>,
    pub fini_array: Option<ArrayRange>,
}
