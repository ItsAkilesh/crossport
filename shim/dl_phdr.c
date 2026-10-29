/*
 * File: shim/dl_phdr.c
 * Purpose: dl_iterate_phdr and _dl_find_object: how the C and C++ unwinders find the frame tables of translated modules
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * A thrown C++ exception is unwound with the DWARF tables in each module's .eh_frame, which the unwinder
 * finds through the program headers of every loaded module. Translated modules have no ELF headers, so
 * the shim describes each one with two synthetic program headers: one PT_LOAD covering the image, and
 * the PT_GNU_EH_FRAME pointing at the module's .eh_frame_hdr. Addresses are RVAs and the "load bias" is
 * the image base, which gives the unwinder the real address of everything it reads. The tables are
 * position independent, so nothing in them cares about the bias.
 */

#include "libc_abi.h"
#include "startinfo.h"

#define PT_LOAD_TYPE 1u
#define PT_GNU_EH_FRAME_TYPE 0x6474e550u
#define PF_READ_EXECUTE 5u
#define PF_READ 4u
#define PAGE_ALIGN 0x1000u
#define IMAGE_RVA_BIAS 0x1000u
#define PHDRS_PER_MODULE 2
#define DLFO_RESERVED_WORDS 7

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Phdr;

/* glibc's struct dl_phdr_info, with the four fields added in glibc 2.4 (size 64). */
typedef struct {
    uint64_t dlpi_addr;
    const char *dlpi_name;
    const Phdr *dlpi_phdr;
    uint16_t dlpi_phnum;
    uint64_t dlpi_adds;
    uint64_t dlpi_subs;
    uint64_t dlpi_tls_modid;
    void *dlpi_tls_data;
} PhdrInfo;

/* glibc's struct dl_find_object for x86-64. */
typedef struct {
    uint64_t dlfo_flags;
    void *dlfo_map_start;
    void *dlfo_map_end;
    void *dlfo_link_map;
    void *dlfo_eh_frame;
    uint64_t reserved[DLFO_RESERVED_WORDS];
} FindObject;

_Static_assert(sizeof(Phdr) == 56, "Elf64_Phdr is 56 bytes");
_Static_assert(sizeof(PhdrInfo) == 64, "dl_phdr_info is 64 bytes");
_Static_assert(sizeof(FindObject) == 96, "dl_find_object is 96 bytes");

typedef int (SYSV *PhdrCallback)(PhdrInfo *info, size_t size, void *data);

static uint32_t image_size(const uint8_t *base)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)(const void *)base;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(const void *)(base + dos->e_lfanew);
    return nt->OptionalHeader.SizeOfImage;
}

/* Fills the two program headers of one module; returns how many are meaningful. */
static uint16_t describe(const LoadedModule *module, Phdr phdrs[PHDRS_PER_MODULE])
{
    uint32_t size = image_size(module->base);
    phdrs[0] = (Phdr){.p_type = PT_LOAD_TYPE, .p_flags = PF_READ_EXECUTE, .p_vaddr = IMAGE_RVA_BIAS,
                      .p_paddr = IMAGE_RVA_BIAS, .p_filesz = size - IMAGE_RVA_BIAS, .p_memsz = size - IMAGE_RVA_BIAS,
                      .p_align = PAGE_ALIGN};
    if (module->info->eh_frame_hdr_rva == 0) {
        return 1;
    }
    phdrs[1] = (Phdr){.p_type = PT_GNU_EH_FRAME_TYPE, .p_flags = PF_READ, .p_vaddr = module->info->eh_frame_hdr_rva,
                      .p_paddr = module->info->eh_frame_hdr_rva, .p_align = sizeof(uint32_t)};
    return PHDRS_PER_MODULE;
}

LIBC_FN int sysv_dl_iterate_phdr(PhdrCallback callback, void *data)
{
    size_t count = 0;
    const LoadedModule *modules = shim_loaded_modules(&count);
    int result = 0;
    for (size_t i = count; i > 0 && result == 0; i--) {
        Phdr phdrs[PHDRS_PER_MODULE];
        PhdrInfo info = {.dlpi_addr = (uint64_t)(uintptr_t)modules[i - 1].base, .dlpi_name = "",
                         .dlpi_phdr = phdrs, .dlpi_phnum = describe(&modules[i - 1], phdrs)};
        result = callback(&info, sizeof(info), data);
    }
    return result;
}

/* glibc 2.35's faster lookup, which current libgcc uses instead of dl_iterate_phdr when it is present. */
LIBC_FN int sysv__dl_find_object(const void *address, FindObject *result)
{
    size_t count = 0;
    const LoadedModule *modules = shim_loaded_modules(&count);
    uintptr_t target = (uintptr_t)address;
    for (size_t i = 0; i < count; i++) {
        uintptr_t start = (uintptr_t)modules[i].base;
        uintptr_t end = start + image_size(modules[i].base);
        if (target >= start && target < end && modules[i].info->eh_frame_hdr_rva != 0) {
            *result = (FindObject){.dlfo_map_start = (void *)start, .dlfo_map_end = (void *)end,
                                   .dlfo_eh_frame = modules[i].base + modules[i].info->eh_frame_hdr_rva};
            return 0;
        }
    }
    return -1;
}
