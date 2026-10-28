// File: translator/src/elf_model.rs
// Purpose: Plain data describing an analyzed ELF module, independent of the parser
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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SymbolBinding {
    Local,
    Global,
    Weak,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Definition {
    Undefined,
    /// Defined at this virtual address inside the module.
    Address(u64),
    /// An absolute value that is not an address (`SHN_ABS` and friends).
    Absolute,
}

/// One entry of `.dynsym`. Relocations refer to these by index.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DynSymbol {
    pub name: String,
    pub binding: SymbolBinding,
    pub definition: Definition,
    pub size: u64,
    /// `STT_TLS`: the value is an offset inside the module's TLS segment, not an address.
    pub is_tls: bool,
}

impl DynSymbol {
    pub fn address(&self) -> Option<u64> {
        match self.definition {
            Definition::Address(address) => Some(address),
            _ => None,
        }
    }

    pub fn is_weak(&self) -> bool {
        self.binding == SymbolBinding::Weak
    }

    /// Whether other modules can bind to this address symbol by name.
    pub fn is_exported(&self) -> bool {
        !self.is_tls && self.is_visible_definition()
    }

    /// Whether other modules can bind to this thread-local symbol by name.
    pub fn is_tls_exported(&self) -> bool {
        self.is_tls && self.is_visible_definition()
    }

    fn is_visible_definition(&self) -> bool {
        self.binding != SymbolBinding::Local && !self.name.is_empty() && self.address().is_some()
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RelocKind {
    /// `R_X86_64_RELATIVE`: the slot holds `load base + addend`.
    Relative { addend: i64 },
    /// `R_X86_64_64`, `GLOB_DAT` and `JUMP_SLOT`: the slot holds `symbol address + addend`.
    Symbol { symbol: usize, addend: i64 },
    /// `R_X86_64_COPY`: copy the symbol's initial data from the defining library into the slot.
    Copy { symbol: usize },
    /// `R_X86_64_DTPMOD64`: the id of the module defining the TLS symbol (`None`: this module).
    TlsModule { symbol: Option<usize> },
    /// `R_X86_64_DTPOFF64`: the symbol's offset inside its module's TLS block, plus the addend.
    TlsOffset { symbol: Option<usize>, addend: i64 },
    /// `R_X86_64_TPOFF64`: the symbol's offset from the thread pointer (negative), plus the addend.
    TlsThreadOffset { symbol: Option<usize>, addend: i64 },
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

/// The `PT_TLS` template: `file_size` bytes of initial data followed by zeroes up to `mem_size`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TlsSegment {
    pub vaddr: u64,
    pub file_size: u64,
    pub mem_size: u64,
    pub align: u64,
}

/// A function's address range, from `.eh_frame`. The rewriter decodes only these ranges.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CodeRange {
    pub start: u64,
    pub len: u64,
}

#[derive(Debug, Clone)]
pub struct ElfInfo {
    pub entry: u64,
    pub segments: Vec<LoadSegment>,
    pub soname: Option<String>,
    pub needed: Vec<String>,
    /// `DT_RPATH` and `DT_RUNPATH` entries, split on `:`.
    pub rpath: Vec<String>,
    pub runpath: Vec<String>,
    pub symbols: Vec<DynSymbol>,
    pub relocations: Vec<Relocation>,
    pub init: Option<u64>,
    pub fini: Option<u64>,
    pub init_array: Option<ArrayRange>,
    pub fini_array: Option<ArrayRange>,
    pub tls: Option<TlsSegment>,
    pub code_ranges: Vec<CodeRange>,
}
