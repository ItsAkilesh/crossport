// File: translator/src/rewrite/stub.rs
// Purpose: Build the out-of-line stub that executes one FS-relative instruction against the real thread pointer
// Author: Akilesh
// Date: 2026-10-28

use iced_x86::{
    Code, Encoder, Instruction, InstructionInfoFactory, MemoryOperand, Register,
};

use crate::error::TranslateError;

/// `TEB.ArbitraryUserPointer` (`gs:[0x28]`): a free per-thread word used to save the
/// scratch register for the few instructions the stub runs. Never the stack,
/// because translated code may use the red zone below `rsp`.
const SAVE_SLOT: i64 = 0x28;
/// Placeholder for the Windows TLS slot displacement; the shim patches it at startup.
const TP_SLOT_PLACEHOLDER: i64 = 0x1480;
const SCRATCH_CANDIDATES: [Register; 8] = [
    Register::R11,
    Register::R10,
    Register::R9,
    Register::R8,
    Register::R15,
    Register::R14,
    Register::RSI,
    Register::RDI,
];

pub(super) struct BuiltStub {
    pub bytes: Vec<u8>,
    /// Offset, inside `bytes`, of the 4-byte `gs:` displacement the shim must patch.
    pub fixup_offset: u32,
}

fn unsupported(rva: u64, reason: &str) -> TranslateError {
    TranslateError::Unsupported(format!("fs-relative instruction at rva {rva:#x}: {reason}"))
}

fn encode_error(rva: u64, error: iced_x86::IcedError) -> TranslateError {
    unsupported(rva, &format!("cannot be re-encoded ({error})"))
}

fn gs_slot(displacement: i64) -> MemoryOperand {
    MemoryOperand::new(Register::None, Register::None, 1, displacement, 4, false, Register::GS)
}

/// A scratch register the instruction does not read, write or address with.
fn pick_scratch(instruction: &Instruction, rva: u64) -> Result<Register, TranslateError> {
    let mut factory = InstructionInfoFactory::new();
    let info = factory.info(instruction);
    let used: Vec<Register> = info.used_registers().iter().map(|u| u.register().full_register()).collect();
    SCRATCH_CANDIDATES
        .into_iter()
        .find(|candidate| !used.contains(candidate))
        .ok_or_else(|| unsupported(rva, "no free scratch register"))
}

fn check_addressing(instruction: &Instruction, rva: u64) -> Result<(), TranslateError> {
    if instruction.is_ip_rel_memory_operand() {
        return Err(unsupported(rva, "RIP-relative operand with an fs override"));
    }
    if instruction.is_vsib() {
        return Err(unsupported(rva, "vector-indexed (gather/scatter) operand"));
    }
    for register in [instruction.memory_base(), instruction.memory_index()] {
        let is_usable = register == Register::None || (register.is_gpr64() && register != Register::RSP);
        if !is_usable {
            return Err(unsupported(rva, "address register is not a usable 64-bit register"));
        }
    }
    let displacement = instruction.memory_displacement64() as i64;
    if i32::try_from(displacement).is_err() {
        return Err(unsupported(rva, "displacement does not fit in 32 bits"));
    }
    Ok(())
}

/// Rewrites the memory operand to `[scratch + index*scale + displacement]`.
/// When both a base and an index exist, the caller has folded the base into
/// the scratch with a `lea` first.
fn retarget(instruction: &Instruction, scratch: Register, has_base_and_index: bool) -> Instruction {
    let mut modified = *instruction;
    let (base, index) = (instruction.memory_base(), instruction.memory_index());
    let scale = instruction.memory_index_scale();
    modified.set_segment_prefix(Register::None);
    modified.set_memory_base(scratch);
    if has_base_and_index || base == Register::None {
        modified.set_memory_index(index);
        modified.set_memory_index_scale(scale);
    } else {
        modified.set_memory_index(base);
        modified.set_memory_index_scale(1);
    }
    let displacement = instruction.memory_displacement64() as i64;
    modified.set_memory_displacement64(displacement as u64);
    modified.set_memory_displ_size(if i8::try_from(displacement).is_ok() { 1 } else { 4 });
    modified
}

/// Builds: save scratch, load the thread pointer, optional `lea`, the
/// retargeted instruction, restore scratch, jump back. None of it changes the flags.
/// `stub_rva` is where the stub will live; `return_rva` is the instruction after the site.
pub(super) fn build(
    instruction: &Instruction,
    rva: u64,
    stub_rva: u64,
    return_rva: u64,
) -> Result<BuiltStub, TranslateError> {
    check_addressing(instruction, rva)?;
    let scratch = pick_scratch(instruction, rva)?;
    let (base, index) = (instruction.memory_base(), instruction.memory_index());
    let has_base_and_index = base != Register::None && index != Register::None;
    let mut steps = vec![
        Instruction::with2(Code::Mov_rm64_r64, gs_slot(SAVE_SLOT), scratch).map_err(|e| encode_error(rva, e))?,
        Instruction::with2(Code::Mov_r64_rm64, scratch, gs_slot(TP_SLOT_PLACEHOLDER))
            .map_err(|e| encode_error(rva, e))?,
    ];
    if has_base_and_index {
        let address = MemoryOperand::with_base_index(scratch, base);
        steps.push(Instruction::with2(Code::Lea_r64_m, scratch, address).map_err(|e| encode_error(rva, e))?);
    }
    steps.push(retarget(instruction, scratch, has_base_and_index));
    steps.push(
        Instruction::with2(Code::Mov_r64_rm64, scratch, gs_slot(SAVE_SLOT)).map_err(|e| encode_error(rva, e))?,
    );
    steps.push(Instruction::with_branch(Code::Jmp_rel32_64, return_rva).map_err(|e| encode_error(rva, e))?);
    encode(&steps, rva, stub_rva)
}

fn encode(steps: &[Instruction], rva: u64, stub_rva: u64) -> Result<BuiltStub, TranslateError> {
    let mut bytes = Vec::new();
    let mut fixup_offset = None;
    for (position, step) in steps.iter().enumerate() {
        let mut encoder = Encoder::new(64);
        encoder
            .encode(step, stub_rva + bytes.len() as u64)
            .map_err(|e| encode_error(rva, e))?;
        if position == 1 {
            fixup_offset = Some(bytes.len() + encoder.get_constant_offsets().displacement_offset());
        }
        bytes.extend_from_slice(&encoder.take_buffer());
    }
    let fixup_offset = fixup_offset.ok_or_else(|| unsupported(rva, "internal: no slot displacement"))?;
    Ok(BuiltStub { bytes, fixup_offset: fixup_offset as u32 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use iced_x86::{Decoder, DecoderOptions, FlowControl};

    fn decode(bytes: &[u8], ip: u64) -> Instruction {
        Decoder::with_ip(64, bytes, ip, DecoderOptions::NONE).decode()
    }

    fn all(bytes: &[u8], ip: u64) -> Vec<Instruction> {
        let mut decoder = Decoder::with_ip(64, bytes, ip, DecoderOptions::NONE);
        let mut out = Vec::new();
        while decoder.can_decode() {
            out.push(decoder.decode());
        }
        out
    }

    // mov %fs:0xffffffffffffff90,%eax
    const LOAD_LE: [u8; 8] = [0x64, 0x8b, 0x04, 0x25, 0x90, 0xff, 0xff, 0xff];
    // movb $0x0,%fs:-0x20(%rax)
    const STORE_BASE: [u8; 5] = [0x64, 0xc6, 0x40, 0xe0, 0x00];
    // mov %esi,%fs:0xffffffffffffff90
    const STORE_LE: [u8; 8] = [0x64, 0x89, 0x34, 0x25, 0x90, 0xff, 0xff, 0xff];

    #[test]
    fn stub_for_an_absolute_load_ends_with_a_jump_back_and_has_no_fs_prefix() {
        let site = decode(&LOAD_LE, 0x2000);
        let stub = build(&site, 0x2000, 0x9000, 0x2008).expect("builds");
        let steps = all(&stub.bytes, 0x9000);
        assert!(steps.iter().all(|i| i.segment_prefix() != Register::FS));
        let last = steps.last().expect("has steps");
        assert_eq!(last.flow_control(), FlowControl::UnconditionalBranch);
        assert_eq!(last.near_branch_target(), 0x2008);
    }

    #[test]
    fn the_scratch_register_is_saved_and_restored_around_the_instruction() {
        let site = decode(&STORE_BASE, 0x2000);
        let stub = build(&site, 0x2000, 0x9000, 0x2005).expect("builds");
        let steps = all(&stub.bytes, 0x9000);
        assert_eq!(steps[0].code(), Code::Mov_rm64_r64);
        assert_eq!(steps[0].memory_segment(), Register::GS);
        assert_eq!(steps[0].memory_displacement64(), 0x28);
        let restore = &steps[steps.len() - 2];
        assert_eq!(restore.code(), Code::Mov_r64_rm64);
        assert_eq!(restore.memory_displacement64(), 0x28);
        assert_eq!(steps[0].op1_register(), restore.op0_register());
    }

    #[test]
    fn a_base_register_is_kept_as_an_index_and_the_scratch_avoids_it() {
        let site = decode(&STORE_BASE, 0x2000);
        let stub = build(&site, 0x2000, 0x9000, 0x2005).expect("builds");
        let steps = all(&stub.bytes, 0x9000);
        let retargeted = &steps[2];
        assert_eq!(retargeted.memory_index(), Register::RAX);
        assert_ne!(retargeted.memory_base(), Register::RAX);
        assert_eq!(retargeted.memory_displacement64() as i64, -0x20);
    }

    #[test]
    fn the_slot_fixup_points_at_a_patchable_displacement() {
        let site = decode(&STORE_LE, 0x2000);
        let stub = build(&site, 0x2000, 0x9000, 0x2008).expect("builds");
        let at = stub.fixup_offset as usize;
        let value = u32::from_le_bytes(stub.bytes[at..at + 4].try_into().expect("4 bytes"));
        assert_eq!(i64::from(value), TP_SLOT_PLACEHOLDER);
    }

    #[test]
    fn the_scratch_register_is_never_one_the_instruction_uses() {
        // mov %fs:(%r11),%r10: uses r11 and r10, so neither may be the scratch.
        let site = decode(&[0x64, 0x4d, 0x8b, 0x13], 0x2000);
        let stub = build(&site, 0x2000, 0x9000, 0x2004).expect("builds");
        let steps = all(&stub.bytes, 0x9000);
        let scratch = steps[0].op1_register();
        assert!(scratch != Register::R11 && scratch != Register::R10);
    }

    #[test]
    fn rsp_relative_operands_are_refused() {
        // mov %fs:(%rsp),%rax
        let site = decode(&[0x64, 0x48, 0x8b, 0x04, 0x24], 0x2000);
        assert!(matches!(build(&site, 0x2000, 0x9000, 0x2005), Err(TranslateError::Unsupported(_))));
    }
}
