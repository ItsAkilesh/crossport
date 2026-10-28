/*
 * File: shim/startinfo.h
 * Purpose: The per-module start-info table the translator emits; a contract with translator/src/shim_abi.rs and startup.rs
 * Author: Akilesh
 * Date: 2026-10-28
 */

#ifndef CROSSPORT_STARTINFO_H
#define CROSSPORT_STARTINFO_H

#include <stdint.h>

#define INFO_MAGIC 0x54525043u /* "CPRT", little-endian */
#define INFO_VERSION 3u
#define INFO_SYMBOL "__crossport_info"
#define MAX_MODULES 256u
#define MAX_RECORDS (1u << 22)

/* *slot = *source + addend. `source` is an IAT entry, or an RVA in the executable. */
typedef struct {
    uint32_t slot_rva;
    uint32_t source_rva;
    int64_t addend;
} Binding;

/* memcpy(executable + dest_rva, *iat_rva, size): an R_X86_64_COPY performed at startup. */
typedef struct {
    uint32_t dest_rva;
    uint32_t iat_rva;
    uint64_t size;
} CopyRecord;

/* Fixed 104-byte header; the records follow it in memory in the order below. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_rva;
    uint32_t init_rva;
    uint32_t init_array_rva;
    uint32_t init_array_count;
    uint32_t fini_rva;
    uint32_t fini_array_rva;
    uint32_t fini_array_count;
    uint32_t iat_binding_count;
    uint32_t exe_binding_count;
    uint32_t copy_count;
    uint32_t module_count;
    uint32_t modules_rva;
    uint32_t tls_init_rva;
    uint32_t tls_file_size;
    uint32_t tls_mem_size;
    uint32_t tls_align;
    int32_t tls_offset; /* start of this module's TLS block relative to the thread pointer */
    uint32_t canary_cell_rva;
    uint32_t slot_fixup_count;
    uint32_t tls_total_size; /* executable only */
    uint32_t tls_max_align;  /* executable only */
    uint32_t stubs_rva;
    uint32_t stubs_size;
    uint32_t reserved;
} StartInfo;

_Static_assert(sizeof(StartInfo) == 104, "StartInfo must match INFO_HEADER_BYTES in shim_abi.rs");
_Static_assert(sizeof(Binding) == 16 && sizeof(CopyRecord) == 16, "records must match RECORD_BYTES");

typedef struct {
    const StartInfo *info;
    uint8_t *base;
} LoadedModule;

static inline const Binding *info_iat_bindings(const StartInfo *info)
{
    return (const Binding *)(info + 1);
}

static inline const Binding *info_exe_bindings(const StartInfo *info)
{
    return info_iat_bindings(info) + info->iat_binding_count;
}

static inline const CopyRecord *info_copies(const StartInfo *info)
{
    return (const CopyRecord *)(info_exe_bindings(info) + info->exe_binding_count);
}

/* RVAs of 4-byte displacements to patch with the Windows TLS slot offset. */
static inline const uint32_t *info_slot_fixups(const StartInfo *info)
{
    return (const uint32_t *)(info_copies(info) + info->copy_count);
}

#endif
