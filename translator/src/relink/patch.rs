// File: translator/src/relink/patch.rs
// Purpose: Apply a module's relocations to its virtual image, deferring what only the shim can finish at startup
// Author: Akilesh
// Date: 2026-10-28

use crate::elf_model::{RelocKind, Relocation};
use crate::error::TranslateError;
use crate::graph::ModuleGraph;
use crate::pe_model::RVA_BIAS;
use crate::resolve::{Resolver, Target, TlsTarget};
use crate::shim_abi::SHIM_DLL;
use crate::startup::Binding;
use crate::tls_layout::TlsLayout;

use super::layout::write_u64;

/// `*slot = *IAT(dll, symbol) + addend`, finished by the shim at startup.
pub(super) struct ImportBinding {
    pub slot_rva: u32,
    pub dll: String,
    pub symbol: String,
    pub addend: i64,
}

/// `memcpy(slot, *IAT(dll, symbol), size)`, finished by the shim at startup.
pub(super) struct ImportCopy {
    pub dest_rva: u32,
    pub dll: String,
    pub symbol: String,
    pub size: u64,
}

#[derive(Default)]
pub(super) struct Patched {
    /// Slots that now hold an absolute pointer and need a PE base relocation.
    pub base_reloc_slots: Vec<u32>,
    pub import_bindings: Vec<ImportBinding>,
    pub executable_bindings: Vec<Binding>,
    pub copies: Vec<ImportCopy>,
}

pub(super) struct PatchContext<'a> {
    pub graph: &'a ModuleGraph,
    pub resolver: &'a Resolver<'a>,
    pub module: usize,
    pub image_base: u64,
    pub tls: &'a TlsLayout,
}

pub(super) fn apply(
    image: &mut [u8],
    context: &PatchContext<'_>,
) -> Result<Patched, TranslateError> {
    let symbol_count = context.graph.modules[context.module].info.symbols.len();
    let mut patcher = Patcher {
        context,
        image,
        targets: vec![None; symbol_count],
        out: Patched::default(),
    };
    for relocation in &context.graph.modules[context.module].info.relocations {
        patcher.apply_one(relocation)?;
    }
    Ok(patcher.out)
}

struct Patcher<'a, 'i> {
    context: &'a PatchContext<'a>,
    image: &'i mut [u8],
    /// Resolution results, memoised per symbol index.
    targets: Vec<Option<Target>>,
    out: Patched,
}

fn rva32(rva: u64) -> Result<u32, TranslateError> {
    u32::try_from(rva)
        .map_err(|_| TranslateError::LimitExceeded("RVA does not fit in 32 bits".into()))
}

impl Patcher<'_, '_> {
    fn apply_one(&mut self, relocation: &Relocation) -> Result<(), TranslateError> {
        let slot = RVA_BIAS + relocation.offset;
        match relocation.kind {
            RelocKind::Relative { addend } => self.absolute(slot, 0, addend),
            RelocKind::Symbol { symbol, addend } => self.symbol(slot, symbol, addend),
            RelocKind::Copy { symbol } => self.copy(slot, symbol),
            RelocKind::TlsModule { symbol } => {
                let target = self.tls_target(symbol)?;
                write_u64(self.image, slot, target.module as u64)
            }
            RelocKind::TlsOffset { symbol, addend } => {
                let target = self.tls_target(symbol)?;
                write_u64(self.image, slot, target.offset.wrapping_add(addend as u64))
            }
            RelocKind::TlsThreadOffset { symbol, addend } => {
                let target = self.tls_target(symbol)?;
                let block = self.context.tls.module(target.module).ok_or_else(|| {
                    TranslateError::Elf("TLS relocation targets a module without a TLS segment".into())
                })?;
                let value = block.tp_offset.wrapping_add(target.offset as i64).wrapping_add(addend);
                write_u64(self.image, slot, value as u64)
            }
        }
    }

    /// Writes `image base + bias + address + addend` and records a base relocation.
    fn absolute(&mut self, slot: u64, address: u64, addend: i64) -> Result<(), TranslateError> {
        let value = (self.context.image_base + RVA_BIAS + address).wrapping_add(addend as u64);
        write_u64(self.image, slot, value)?;
        self.out.base_reloc_slots.push(rva32(slot)?);
        Ok(())
    }

    /// TLS relocations are resolved completely at translation time: static TLS
    /// means every module's offset from the thread pointer is already known.
    fn tls_target(&self, symbol: Option<usize>) -> Result<TlsTarget, TranslateError> {
        let module = &self.context.graph.modules[self.context.module];
        let definition = symbol.map(|index| &module.info.symbols[index]);
        self.context.resolver.resolve_tls(self.context.module, definition)
    }

    fn target(&mut self, symbol: usize) -> Result<Target, TranslateError> {
        if let Some(Some(target)) = self.targets.get(symbol) {
            return Ok(*target);
        }
        let module = &self.context.graph.modules[self.context.module];
        let definition = module.info.symbols.get(symbol).ok_or_else(|| {
            TranslateError::Elf(format!("relocation refers to missing symbol {symbol}"))
        })?;
        let target = self
            .context
            .resolver
            .resolve(self.context.module, definition)?;
        self.targets[symbol] = Some(target);
        Ok(target)
    }

    fn symbol(&mut self, slot: u64, symbol: usize, addend: i64) -> Result<(), TranslateError> {
        let target = self.target(symbol)?;
        let name = self.context.graph.modules[self.context.module].info.symbols[symbol]
            .name
            .clone();
        match target {
            Target::Local { address } => self.absolute(slot, address, addend),
            Target::Null => write_u64(self.image, slot, addend as u64),
            Target::Executable { address } => {
                self.out.executable_bindings.push(Binding {
                    slot_rva: rva32(slot)?,
                    source_rva: rva32(RVA_BIAS + address)?,
                    addend,
                });
                Ok(())
            }
            Target::Library { .. } | Target::Shim => {
                let dll = self.dll_name(target)?;
                self.out.import_bindings.push(ImportBinding {
                    slot_rva: rva32(slot)?,
                    dll,
                    symbol: name,
                    addend,
                });
                Ok(())
            }
        }
    }

    fn copy(&mut self, slot: u64, symbol: usize) -> Result<(), TranslateError> {
        if self.context.module != 0 {
            return Err(TranslateError::Unsupported(
                "copy relocation in a shared library".into(),
            ));
        }
        let definition = self.context.graph.modules[0].info.symbols[symbol].clone();
        let target = self.context.resolver.resolve_copy_source(0, &definition)?;
        let dll = self.dll_name(target)?;
        self.out.copies.push(ImportCopy {
            dest_rva: rva32(slot)?,
            dll,
            symbol: definition.name,
            size: definition.size,
        });
        Ok(())
    }

    fn dll_name(&self, target: Target) -> Result<String, TranslateError> {
        match target {
            Target::Library { module } => {
                Ok(self.context.graph.modules[module].name.dll_file_name())
            }
            Target::Shim => Ok(SHIM_DLL.to_string()),
            _ => Err(TranslateError::Elf(
                "internal: target is not an import".into(),
            )),
        }
    }
}
