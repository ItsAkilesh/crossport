// File: translator/src/ifunc.rs
// Purpose: GNU indirect functions (STT_GNU_IFUNC and R_X86_64_IRELATIVE): a lazy trampoline per resolver, so a call reaches the chosen implementation
// Author: Akilesh
// Date: 2026-11-06

use crate::elf_model::{Definition, DynSymbol, LoadSegment, RelocKind, Relocation};
use crate::error::TranslateError;
use crate::pe_model::{align_up, PAGE_SIZE};

/// Bytes reserved for each trampoline; its code is shorter and the rest is padding.
pub const TRAMPOLINE_BYTES: u64 = 0x100;
/// Cap on distinct resolvers in one module, so hostile input cannot demand an enormous section.
const MAX_IFUNCS: usize = 4096;
const CELL_BYTES: u64 = 8;
const SAVED_VECTOR_REGISTERS: u8 = 8;
const VECTOR_SLOT_BYTES: u8 = 16;
const FRAME_BYTES: u32 = 0x80;
const INT3: u8 = 0xCC;

/// The resolvers of one module and where their trampolines live (ELF virtual addresses).
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct IfuncTable {
    /// Virtual address of the first trampoline: the page after the last segment.
    pub base_vaddr: u64,
    pub resolvers: Vec<u64>,
}

impl IfuncTable {
    pub fn trampoline_vaddr(&self, index: usize) -> u64 {
        self.base_vaddr + index as u64 * TRAMPOLINE_BYTES
    }

    /// Virtual address of the cell holding the resolved target of trampoline `index`.
    pub fn cell_vaddr(&self, index: usize) -> u64 {
        let code_bytes = align_up(self.resolvers.len() as u64 * TRAMPOLINE_BYTES, PAGE_SIZE);
        self.base_vaddr + code_bytes.max(PAGE_SIZE) + index as u64 * CELL_BYTES
    }

    fn index_of(&mut self, resolver: u64) -> usize {
        match self.resolvers.iter().position(|&known| known == resolver) {
            Some(index) => index,
            None => {
                self.resolvers.push(resolver);
                self.resolvers.len() - 1
            }
        }
    }
}

/// The page-aligned virtual address just past the last segment.
pub fn image_end_vaddr(segments: &[LoadSegment]) -> u64 {
    segments
        .iter()
        .map(|segment| align_up(segment.vaddr + segment.mem_size, PAGE_SIZE))
        .max()
        .unwrap_or(0)
}

/// Points every ifunc symbol and `IRELATIVE` slot at a trampoline instead of its resolver.
pub fn redirect(
    segments: &[LoadSegment],
    symbols: &mut [DynSymbol],
    relocations: &mut [Relocation],
) -> Result<IfuncTable, TranslateError> {
    let mut table = IfuncTable {
        base_vaddr: image_end_vaddr(segments),
        resolvers: Vec::new(),
    };
    for symbol in symbols.iter_mut().filter(|symbol| symbol.is_ifunc) {
        if let Definition::Address(resolver) = symbol.definition {
            let index = table.index_of(resolver);
            symbol.definition = Definition::Address(table.trampoline_vaddr(index));
        }
    }
    for relocation in relocations.iter_mut() {
        if let RelocKind::Indirect { resolver } = relocation.kind {
            let index = table.index_of(resolver);
            let addend = table.trampoline_vaddr(index) as i64;
            relocation.kind = RelocKind::Relative { addend };
        }
    }
    if table.resolvers.len() > MAX_IFUNCS {
        return Err(TranslateError::LimitExceeded(format!(
            "more than {MAX_IFUNCS} indirect functions"
        )));
    }
    Ok(table)
}

/// Machine code of the trampoline pair for every resolver, as one block of `TRAMPOLINE_BYTES` each.
pub fn build_trampolines(table: &IfuncTable) -> Vec<u8> {
    let mut code = Vec::with_capacity(table.resolvers.len() * TRAMPOLINE_BYTES as usize);
    for (index, &resolver) in table.resolvers.iter().enumerate() {
        let start = table.trampoline_vaddr(index);
        let mut block = trampoline(start, table.cell_vaddr(index), resolver);
        block.resize(TRAMPOLINE_BYTES as usize, INT3);
        code.extend_from_slice(&block);
    }
    code
}

fn rel32(from_end: u64, target: u64) -> [u8; 4] {
    (target.wrapping_sub(from_end) as u32).to_le_bytes()
}

/// `jmp *cell` once resolved; the first call saves the argument registers, runs the resolver, and caches its answer.
/// The cell is read through `r11`, never `rax`, so a variadic target still sees its vector-register count.
fn trampoline(start: u64, cell: u64, resolver: u64) -> Vec<u8> {
    let mut code: Vec<u8> = Vec::new();
    let at = |code: &Vec<u8>| start + code.len() as u64;
    code.extend_from_slice(&[0x4C, 0x8B, 0x1D]); // mov r11, [rip+cell]
    code.extend_from_slice(&rel32(at(&code) + 4, cell));
    code.extend_from_slice(&[0x4D, 0x85, 0xDB]); // test r11, r11
    code.extend_from_slice(&[0x74, 0x03]); // jz slow
    code.extend_from_slice(&[0x41, 0xFF, 0xE3]); // jmp r11
    code.extend_from_slice(&[0x50, 0x57, 0x56, 0x52, 0x51, 0x41, 0x50, 0x41, 0x51]); // push rax rdi rsi rdx rcx r8 r9
    code.extend_from_slice(&[0x48, 0x81, 0xEC]); // sub rsp, FRAME_BYTES
    code.extend_from_slice(&FRAME_BYTES.to_le_bytes());
    vector_moves(&mut code, 0x7F); // movdqu [rsp+16*i], xmm_i
    code.extend_from_slice(&[0x4C, 0x8D, 0x1D]); // lea r11, [rip+resolver]
    code.extend_from_slice(&rel32(at(&code) + 4, resolver));
    code.extend_from_slice(&[0x41, 0xFF, 0xD3]); // call r11
    code.extend_from_slice(&[0x48, 0x89, 0x05]); // mov [rip+cell], rax
    code.extend_from_slice(&rel32(at(&code) + 4, cell));
    code.extend_from_slice(&[0x49, 0x89, 0xC3]); // mov r11, rax
    vector_moves(&mut code, 0x6F); // movdqu xmm_i, [rsp+16*i]
    code.extend_from_slice(&[0x48, 0x81, 0xC4]); // add rsp, FRAME_BYTES
    code.extend_from_slice(&FRAME_BYTES.to_le_bytes());
    code.extend_from_slice(&[0x41, 0x59, 0x41, 0x58, 0x59, 0x5A, 0x5E, 0x5F, 0x58]); // pop r9 r8 rcx rdx rsi rdi rax
    code.extend_from_slice(&[0x41, 0xFF, 0xE3]); // jmp r11
    code
}

/// `movdqu` between `xmm0..xmm7` and the frame at `rsp`; `opcode` is 0x7F for a store and 0x6F for a load.
fn vector_moves(code: &mut Vec<u8>, opcode: u8) {
    for register in 0..SAVED_VECTOR_REGISTERS {
        let modrm = 0x44 | (register << 3);
        code.extend_from_slice(&[
            0xF3,
            0x0F,
            opcode,
            modrm,
            0x24,
            register * VECTOR_SLOT_BYTES,
        ]);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::elf_model::{SegmentFlags, SymbolBinding};

    fn segment(vaddr: u64, mem_size: u64) -> LoadSegment {
        LoadSegment {
            vaddr,
            file_offset: vaddr,
            file_size: mem_size,
            mem_size,
            flags: SegmentFlags {
                is_readable: true,
                is_writable: false,
                is_executable: true,
            },
        }
    }

    fn ifunc_symbol(address: u64) -> DynSymbol {
        DynSymbol {
            name: "crc32_z".into(),
            binding: SymbolBinding::Global,
            definition: Definition::Address(address),
            size: 12,
            is_tls: false,
            is_ifunc: true,
        }
    }

    #[test]
    fn the_trampolines_start_on_the_page_after_the_last_segment() {
        assert_eq!(
            image_end_vaddr(&[segment(0, 0x1800), segment(0x3000, 0x10)]),
            0x4000
        );
        assert_eq!(image_end_vaddr(&[]), 0);
    }

    #[test]
    fn an_ifunc_symbol_is_redirected_and_aliases_share_a_trampoline() {
        let mut symbols = vec![
            ifunc_symbol(0x75e0),
            ifunc_symbol(0x75e0),
            ifunc_symbol(0x7700),
        ];
        let table = redirect(&[segment(0, 0x2000)], &mut symbols, &mut []).expect("redirects");
        assert_eq!(table.resolvers, [0x75e0, 0x7700]);
        assert_eq!(symbols[0].address(), Some(0x2000));
        assert_eq!(symbols[1].address(), Some(0x2000));
        assert_eq!(symbols[2].address(), Some(0x2000 + TRAMPOLINE_BYTES));
    }

    #[test]
    fn an_irelative_slot_becomes_a_relative_slot_on_the_trampoline() {
        let mut relocations = vec![Relocation {
            offset: 0x100,
            kind: RelocKind::Indirect { resolver: 0x75e0 },
        }];
        let table = redirect(&[segment(0, 0x2000)], &mut [], &mut relocations).expect("redirects");
        assert_eq!(relocations[0].kind, RelocKind::Relative { addend: 0x2000 });
        assert_eq!(table.resolvers, [0x75e0]);
    }

    #[test]
    fn too_many_resolvers_are_refused() {
        let mut symbols: Vec<DynSymbol> = (0..=MAX_IFUNCS as u64)
            .map(|i| ifunc_symbol(0x100 + i))
            .collect();
        let result = redirect(&[segment(0, 0x2000)], &mut symbols, &mut []);
        assert!(matches!(result, Err(TranslateError::LimitExceeded(_))));
    }

    #[test]
    fn a_trampoline_fits_its_block_and_reads_its_own_cell() {
        let table = IfuncTable {
            base_vaddr: 0x2000,
            resolvers: vec![0x75e0],
        };
        let code = build_trampolines(&table);
        assert_eq!(code.len() as u64, TRAMPOLINE_BYTES);
        assert_eq!(&code[..3], &[0x4C, 0x8B, 0x1D]);
        let displacement = i32::from_le_bytes([code[3], code[4], code[5], code[6]]);
        assert_eq!(
            0x2000 + 7 + i64::from(displacement) as u64,
            table.cell_vaddr(0)
        );
        assert_eq!(code[TRAMPOLINE_BYTES as usize - 1], INT3);
    }

    #[test]
    fn the_cell_section_starts_on_the_page_after_the_code() {
        let table = IfuncTable {
            base_vaddr: 0x2000,
            resolvers: vec![1; 17],
        };
        assert_eq!(table.cell_vaddr(0), 0x2000 + 0x2000);
    }
}
