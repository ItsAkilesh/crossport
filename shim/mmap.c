/*
 * File: shim/mmap.c
 * Purpose: mmap, munmap, mremap, mprotect, msync and madvise: anonymous memory and file mappings on Win32 sections
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Windows maps files at 64 KiB boundaries, so a file offset that is not a multiple of that is mapped from
 * below and the returned pointer is moved up by the difference. Mappings are kept in a table so munmap can
 * undo exactly what mmap did. Unmapping part of a file mapping is accepted but does nothing: the pages stay
 * mapped until the whole mapping goes.
 */

#include <string.h>

#include "fd.h"

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MREMAP_MAYMOVE 1
#define MADV_DONTNEED 4
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define PAGE_BYTES 4096
#define MAP_FAILED_VALUE ((void *)(intptr_t)-1)
#define MAX_LOW_BITS 0xffffffffull

typedef struct Mapping {
    void *address; /* what the program was given */
    void *view;    /* the Win32 base address (the same for anonymous memory) */
    size_t length;
    int is_file;
    struct Mapping *next;
} Mapping;

static Mapping *g_mappings;
static SRWLOCK g_lock = SRWLOCK_INIT;

static size_t round_up_to_page(size_t length)
{
    return (length + PAGE_BYTES - 1) & ~((size_t)PAGE_BYTES - 1);
}

static DWORD page_protection(int protection)
{
    if (protection & PROT_EXEC) {
        return (protection & PROT_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    }
    if (protection & PROT_WRITE) {
        return PAGE_READWRITE;
    }
    return protection == 0 ? PAGE_NOACCESS : PAGE_READONLY;
}

static int add_mapping(void *address, void *view, size_t length, int is_file)
{
    Mapping *mapping = (Mapping *)HeapAlloc(GetProcessHeap(), 0, sizeof(Mapping));
    if (mapping == NULL) {
        return 0;
    }
    *mapping = (Mapping){.address = address, .view = view, .length = length, .is_file = is_file};
    AcquireSRWLockExclusive(&g_lock);
    mapping->next = g_mappings;
    g_mappings = mapping;
    ReleaseSRWLockExclusive(&g_lock);
    return 1;
}

/* Copies the table entry for `address` (exact match) out under the lock; false when there is none. */
static int find_mapping(const void *address, Mapping *out)
{
    int is_found = 0;
    AcquireSRWLockShared(&g_lock);
    for (const Mapping *at = g_mappings; at != NULL && !is_found; at = at->next) {
        if (at->address == address) {
            *out = *at;
            is_found = 1;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return is_found;
}

static void remove_mapping(const void *address)
{
    AcquireSRWLockExclusive(&g_lock);
    for (Mapping **link = &g_mappings; *link != NULL; link = &(*link)->next) {
        if ((*link)->address == address) {
            Mapping *gone = *link;
            *link = gone->next;
            HeapFree(GetProcessHeap(), 0, gone);
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

static void *map_anonymous(void *wanted, size_t length, int protection, int flags)
{
    size_t size = round_up_to_page(length);
    void *at = (flags & MAP_FIXED) ? wanted : NULL;
    void *region = VirtualAlloc(at, size, MEM_RESERVE | MEM_COMMIT, page_protection(protection));
    if (region == NULL && at != NULL) {
        region = VirtualAlloc(at, size, MEM_COMMIT, page_protection(protection));
    }
    if (region == NULL || !add_mapping(region, region, size, 0)) {
        if (region != NULL) {
            VirtualFree(region, 0, MEM_RELEASE);
        }
        shim_set_errno(LINUX_ENOMEM);
        return MAP_FAILED_VALUE;
    }
    return region;
}

static DWORD view_access(int protection, int is_private)
{
    DWORD access = (protection & PROT_WRITE) ? (is_private ? FILE_MAP_COPY : FILE_MAP_WRITE) : FILE_MAP_READ;
    return (protection & PROT_EXEC) ? (access | FILE_MAP_EXECUTE) : access;
}

static DWORD section_protection(int protection, int is_private)
{
    if (protection & PROT_EXEC) {
        return (protection & PROT_WRITE) ? (is_private ? PAGE_EXECUTE_WRITECOPY : PAGE_EXECUTE_READWRITE) : PAGE_EXECUTE_READ;
    }
    return (protection & PROT_WRITE) ? (is_private ? PAGE_WRITECOPY : PAGE_READWRITE) : PAGE_READONLY;
}

static void *map_file(size_t length, int protection, int flags, int fd, int64_t offset)
{
    HANDLE file = shim_fd_handle(fd);
    if (file == INVALID_HANDLE_VALUE || offset < 0 || (flags & MAP_FIXED)) {
        shim_set_errno(file == INVALID_HANDLE_VALUE ? LINUX_EBADF : LINUX_EINVAL);
        return MAP_FAILED_VALUE;
    }
    int is_private = (flags & MAP_PRIVATE) != 0;
    uint64_t granularity = 65536;
    uint64_t aligned = (uint64_t)offset & ~(granularity - 1);
    size_t delta = (size_t)((uint64_t)offset - aligned);
    HANDLE section = CreateFileMappingW(file, NULL, section_protection(protection, is_private), 0, 0, NULL);
    if (section == NULL) {
        shim_fail_from_win();
        return MAP_FAILED_VALUE;
    }
    void *view = MapViewOfFile(section, view_access(protection, is_private), (DWORD)(aligned >> 32),
                               (DWORD)(aligned & MAX_LOW_BITS), length + delta);
    CloseHandle(section);
    if (view == NULL || !add_mapping((char *)view + delta, view, length, 1)) {
        if (view != NULL) {
            UnmapViewOfFile(view);
        }
        shim_set_errno(view == NULL ? shim_errno_from_win(GetLastError()) : LINUX_ENOMEM);
        return MAP_FAILED_VALUE;
    }
    return (char *)view + delta;
}

LIBC_FN void *sysv_mmap64(void *address, size_t length, int protection, int flags, int fd, int64_t offset)
{
    if (length == 0) {
        shim_set_errno(LINUX_EINVAL);
        return MAP_FAILED_VALUE;
    }
    if (flags & MAP_ANONYMOUS) {
        return map_anonymous(address, length, protection, flags);
    }
    return map_file(length, protection, flags, fd, offset);
}

LIBC_FN void *sysv_mmap(void *address, size_t length, int protection, int flags, int fd, int64_t offset)
{
    return sysv_mmap64(address, length, protection, flags, fd, offset);
}

LIBC_FN int sysv_munmap(void *address, size_t length)
{
    Mapping mapping;
    if (!find_mapping(address, &mapping)) {
        /* Not the start of a mapping: release part of one by decommitting its pages. */
        return VirtualFree(address, round_up_to_page(length), MEM_DECOMMIT) ? 0 : shim_fail(LINUX_EINVAL);
    }
    if (mapping.is_file) {
        if (length < mapping.length) {
            return 0; /* part of a file view cannot be unmapped */
        }
        UnmapViewOfFile(mapping.view);
    } else if (round_up_to_page(length) < mapping.length) {
        return VirtualFree(address, round_up_to_page(length), MEM_DECOMMIT) ? 0 : shim_fail(LINUX_EINVAL);
    } else {
        VirtualFree(mapping.view, 0, MEM_RELEASE);
    }
    remove_mapping(address);
    return 0;
}

LIBC_FN void *sysv_mremap(void *address, size_t old_length, size_t new_length, int flags)
{
    Mapping mapping;
    if (!find_mapping(address, &mapping) || mapping.is_file) {
        shim_set_errno(LINUX_ENOMEM);
        return MAP_FAILED_VALUE;
    }
    if (round_up_to_page(new_length) <= mapping.length) {
        return address;
    }
    if (!(flags & MREMAP_MAYMOVE)) {
        shim_set_errno(LINUX_ENOMEM);
        return MAP_FAILED_VALUE;
    }
    void *moved = map_anonymous(NULL, new_length, PROT_READ | PROT_WRITE, 0);
    if (moved != MAP_FAILED_VALUE) {
        CopyMemory(moved, address, old_length < new_length ? old_length : new_length);
        sysv_munmap(address, mapping.length);
    }
    return moved;
}

LIBC_FN int sysv_mprotect(void *address, size_t length, int protection)
{
    DWORD previous;
    return VirtualProtect(address, length, page_protection(protection), &previous) ? 0 : shim_fail(LINUX_EINVAL);
}

LIBC_FN int sysv_msync(void *address, size_t length, int flags)
{
    (void)flags;
    return FlushViewOfFile(address, length) ? 0 : shim_fail(LINUX_ENOMEM);
}

/* MADV_DONTNEED on private anonymous memory drops the pages, so later reads see zeros; the rest are hints. */
LIBC_FN int sysv_madvise(void *address, size_t length, int advice)
{
    if (advice == MADV_DONTNEED) {
        Mapping mapping;
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(address, &info, sizeof(info)) != 0 && find_mapping(info.AllocationBase, &mapping) && !mapping.is_file) {
            VirtualFree(address, length, MEM_DECOMMIT);
            VirtualAlloc(address, length, MEM_COMMIT, PAGE_READWRITE);
        }
    }
    return 0;
}

LIBC_FN int sysv_mlock(const void *address, size_t length)
{
    (void)address;
    (void)length;
    return 0;
}

LIBC_FN int sysv_munlock(const void *address, size_t length)
{
    (void)address;
    (void)length;
    return 0;
}
