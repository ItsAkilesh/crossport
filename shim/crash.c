/*
 * File: shim/crash.c
 * Purpose: Report where a translated program crashed: the exception kind, the faulting module and offset, and a rough trail
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Two hooks report a crash once. Translated code has no Windows unwind data, so the system cannot unwind through it
 * to find a handler, and it ends the process without running the unhandled-exception filter, whether the fault is
 * in the translated code or in a shim function it called. A vectored handler, which runs before any unwinding,
 * therefore reports faults whose address is inside a translated module or the shim (and only those, so exceptions
 * other libraries raise and handle internally are never reported). The unhandled-exception filter reports what
 * still reaches it. Both use plain Win32 calls only and write straight to
 * the standard error handle, because the process is in an unknown state. Offsets are relative to the module (a bundled DLL or the executable), which is what maps a
 * crash back to the translated code. The process still ends with the exception code as its exit status.
 */

#include <stdint.h>

#include "internal.h"
#include "startinfo.h"

#define LINE_BYTES 320
#define TRAIL_SCAN_WORDS 1024
#define TRAIL_LINES 12
#define MODULE_PATH_BYTES 260
#define ACCESS_KIND_READ 0
#define ACCESS_KIND_WRITE 1
#define EXECUTABLE_PAGE_BITS (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)
#define HEX_BITS_PER_DIGIT 4
#define HEX_DIGIT_COUNT 16
#define IS_FIRST_HANDLER 1

typedef struct {
    char *at;
    char *end;
} Line;

static void put_text(Line *line, const char *text)
{
    while (*text != '\0' && line->at < line->end) {
        *line->at++ = *text++;
    }
}

static void put_hex(Line *line, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    put_text(line, "0x");
    int is_started = 0;
    for (int shift = (HEX_DIGIT_COUNT - 1) * HEX_BITS_PER_DIGIT; shift >= 0; shift -= HEX_BITS_PER_DIGIT) {
        unsigned digit = (unsigned)((value >> shift) & 0xf);
        is_started = is_started || digit != 0 || shift == 0;
        if (is_started && line->at < line->end) {
            *line->at++ = digits[digit];
        }
    }
}

/* "module+0xoffset" when the address is inside a loaded module, the bare address otherwise. */
static void put_location(Line *line, uint64_t address)
{
    HMODULE module = NULL;
    char path[MODULE_PATH_BYTES];
    DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (GetModuleHandleExA(flags, (LPCSTR)(uintptr_t)address, &module) &&
        GetModuleFileNameA(module, path, sizeof(path)) > 0) {
        const char *name = path;
        for (const char *cursor = path; *cursor != '\0'; cursor++) {
            name = (*cursor == '\\' || *cursor == '/') ? cursor + 1 : name;
        }
        put_text(line, name);
        put_text(line, "+");
        put_hex(line, address - (uint64_t)(uintptr_t)module);
    } else {
        put_hex(line, address);
    }
}

static const char *exception_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "access violation";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "privileged instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "integer divide by zero";
    case EXCEPTION_STACK_OVERFLOW:
        return "stack overflow";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "misaligned access";
    default:
        return NULL;
    }
}

static void describe_exception(Line *line, const EXCEPTION_RECORD *record)
{
    const char *name = exception_name(record->ExceptionCode);
    put_text(line, "crossport: ");
    if (name != NULL) {
        put_text(line, name);
    } else {
        put_text(line, "exception ");
        put_hex(line, record->ExceptionCode);
    }
    put_text(line, " at ");
    put_location(line, (uint64_t)(uintptr_t)record->ExceptionAddress);
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        ULONG_PTR kind = record->ExceptionInformation[0];
        put_text(line, kind == ACCESS_KIND_READ ? ", reading " : kind == ACCESS_KIND_WRITE ? ", writing " : ", executing ");
        put_hex(line, record->ExceptionInformation[1]);
    }
    put_text(line, "\n");
}

static int is_code_address(uint64_t address)
{
    MEMORY_BASIC_INFORMATION page;
    return VirtualQuery((LPCVOID)(uintptr_t)address, &page, sizeof(page)) == sizeof(page) &&
           page.State == MEM_COMMIT && page.Type == MEM_IMAGE && (page.Protect & EXECUTABLE_PAGE_BITS) != 0;
}

/* The end of the readable memory around the stack pointer, so the scan never reads a guard or unmapped page. */
static const uint64_t *readable_end(uint64_t stack_pointer)
{
    MEMORY_BASIC_INFORMATION region;
    const uint64_t *none = (const uint64_t *)(uintptr_t)stack_pointer;
    if (VirtualQuery((LPCVOID)(uintptr_t)stack_pointer, &region, sizeof(region)) != sizeof(region) ||
        region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return none;
    }
    return (const uint64_t *)((const uint8_t *)region.BaseAddress + region.RegionSize);
}

/* Words on the stack that point into executable module code: a rough, unverified call trail. */
static void print_trail(uint64_t stack_pointer)
{
    const uint64_t *word = (const uint64_t *)(uintptr_t)stack_pointer;
    const uint64_t *limit = readable_end(stack_pointer);
    int printed = 0;
    for (int scanned = 0; word < limit && scanned < TRAIL_SCAN_WORDS && printed < TRAIL_LINES; scanned++, word++) {
        if (is_code_address(*word)) {
            char buffer[LINE_BYTES];
            Line line = {buffer, buffer + sizeof(buffer) - 1};
            put_text(&line, "  from ");
            put_location(&line, *word);
            put_text(&line, "\n");
            shim_write_all(GetStdHandle(STD_ERROR_HANDLE), buffer, (size_t)(line.at - buffer));
            printed++;
        }
    }
}

static volatile LONG g_is_reported;

static int is_fatal_code(DWORD code)
{
    return exception_name(code) != NULL;
}

/* True when the address is in a translated module or in the shim itself: the code whose faults nobody else reports. */
static int is_in_our_code(uint64_t address)
{
    HMODULE module = NULL;
    HMODULE shim = NULL;
    DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (!GetModuleHandleExA(flags, (LPCSTR)(uintptr_t)address, &module)) {
        return 0;
    }
    if (GetModuleHandleExA(flags, (LPCSTR)(uintptr_t)shim_crash_init, &shim) && module == shim) {
        return 1;
    }
    size_t count = 0;
    const LoadedModule *modules = shim_loaded_modules(&count);
    for (size_t i = 0; i < count; i++) {
        if ((HMODULE)modules[i].base == module) {
            return 1;
        }
    }
    return 0;
}

static void report(EXCEPTION_POINTERS *info)
{
    if (InterlockedExchange(&g_is_reported, 1) != 0) {
        return;
    }
    char buffer[LINE_BYTES];
    Line line = {buffer, buffer + sizeof(buffer) - 1};
    describe_exception(&line, info->ExceptionRecord);
    shim_write_all(GetStdHandle(STD_ERROR_HANDLE), buffer, (size_t)(line.at - buffer));
    print_trail(info->ContextRecord->Rsp);
}

static LONG WINAPI report_crash(EXCEPTION_POINTERS *info)
{
    report(info);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI trap_own_fault(EXCEPTION_POINTERS *info)
{
    if (is_fatal_code(info->ExceptionRecord->ExceptionCode) &&
        is_in_our_code((uint64_t)(uintptr_t)info->ExceptionRecord->ExceptionAddress)) {
        report(info);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void shim_crash_init(void)
{
    AddVectoredExceptionHandler(IS_FIRST_HANDLER, trap_own_fault);
    SetUnhandledExceptionFilter(report_crash);
}
