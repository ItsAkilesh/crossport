/*
 * File: shim/regex_gnu.c
 * Purpose: The GNU regex interface (re_compile_pattern, re_search, re_match, re_set_syntax, re_compile_fastmap) over the shim's engine
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * GNU grep, find and sed use these calls instead of regcomp. The syntax word is reduced to what the engine distinguishes: extended or
 * basic operators (RE_NO_BK_PARENS) and case folding (RE_ICASE). As in glibc, a pattern compiled here lets ^ and $ see newlines.
 */

#include <string.h>

#include "libc_abi.h"
#include "regex_internal.h"
#include "startinfo.h"

#define RE_NO_BK_PARENS_BIT 0x2000u
#define RE_ICASE_BIT 0x400000u
#define RE_NO_SUB_BIT 0x2000000u
#define RE_DOT_NEWLINE_BIT 0x40u
#define MINIMUM_REGISTERS 30
#define SEARCH_FAILED (-1)
#define SEARCH_ERROR (-2)

/* Bit positions in RegexBuffer.flag_bits (the order of glibc's bit-fields). */
#define BITS_REGS_ALLOCATED_SHIFT 1
#define BITS_REGS_ALLOCATED_MASK 3u
#define BITS_NO_SUB (1u << 4)
#define BITS_NOT_BOL (1u << 5)
#define BITS_NOT_EOL (1u << 6)
#define BITS_NEWLINE_ANCHOR (1u << 7)
enum { REGS_UNALLOCATED = 0, REGS_REALLOCATE = 1, REGS_FIXED = 2 };

/* struct re_registers: the register count and two arrays of offsets. */
typedef struct {
    uint32_t count;
    int32_t *starts;
    int32_t *ends;
} Registers;

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void *sysv_realloc(void *block, size_t size);
LIBC_FN void sysv_free(void *block);

LIBC_DATA(uint64_t, re_syntax_options);

LIBC_FN uint64_t sysv_re_set_syntax(uint64_t syntax)
{
    uint64_t *current = (uint64_t *)shim_data_home(&crossport_data_re_syntax_options);
    uint64_t previous = *current;
    *current = syntax;
    return previous;
}

/* Returns an error message, or NULL when the pattern compiled. */
LIBC_FN const char *sysv_re_compile_pattern(const char *pattern, size_t length, RegexBuffer *buffer)
{
    uint64_t syntax = *(uint64_t *)shim_data_home(&crossport_data_re_syntax_options);
    int flags = REGEX_ANCHOR_NEWLINES;
    flags |= (syntax & RE_NO_BK_PARENS_BIT) ? REG_EXTENDED : 0;
    flags |= (syntax & RE_ICASE_BIT) ? REG_ICASE : 0;
    flags |= (syntax & RE_NO_SUB_BIT) ? REG_NOSUB : 0;
    flags |= (syntax & RE_DOT_NEWLINE_BIT) ? 0 : REG_NEWLINE;
    char *copy = sysv_malloc(length + 1);
    if (copy == NULL) {
        return regex_message(REG_ESPACE);
    }
    memcpy(copy, pattern, length);
    copy[length] = '\0';
    int code = regex_compile(buffer, copy, flags);
    sysv_free(copy);
    if (code != REG_NOERROR) {
        return regex_message(code);
    }
    buffer->syntax = syntax;
    buffer->flag_bits |= BITS_NEWLINE_ANCHOR | ((flags & REG_NOSUB) ? BITS_NO_SUB : 0);
    return NULL;
}

LIBC_FN int sysv_re_compile_fastmap(RegexBuffer *buffer)
{
    (void)buffer;
    return 0;
}

/* Makes room for the offsets of every group (at least 30, as glibc allocates) unless the caller fixed the register count. */
static int prepare_registers(RegexBuffer *buffer, Registers *registers, size_t groups)
{
    uint32_t kind = (buffer->flag_bits >> BITS_REGS_ALLOCATED_SHIFT) & BITS_REGS_ALLOCATED_MASK;
    uint32_t wanted = groups + 1 > MINIMUM_REGISTERS ? (uint32_t)(groups + 1) : MINIMUM_REGISTERS;
    if (kind == REGS_FIXED) {
        return 0;
    }
    if (kind == REGS_UNALLOCATED || registers->count < wanted) {
        registers->starts = sysv_realloc(kind == REGS_UNALLOCATED ? NULL : registers->starts, wanted * sizeof(int32_t));
        registers->ends = sysv_realloc(kind == REGS_UNALLOCATED ? NULL : registers->ends, wanted * sizeof(int32_t));
        if (registers->starts == NULL || registers->ends == NULL) {
            return -1;
        }
        registers->count = wanted;
        buffer->flag_bits = (buffer->flag_bits & ~(BITS_REGS_ALLOCATED_MASK << BITS_REGS_ALLOCATED_SHIFT)) | (REGS_REALLOCATE << BITS_REGS_ALLOCATED_SHIFT);
    }
    return 0;
}

static int fill_registers(RegexBuffer *buffer, Registers *registers, const int offsets[2 * REGEX_MAX_GROUPS])
{
    if (prepare_registers(buffer, registers, buffer->re_nsub) != 0) {
        return -1;
    }
    for (uint32_t i = 0; i < registers->count; i++) {
        int is_known = i < REGEX_MAX_GROUPS && i <= buffer->re_nsub;
        registers->starts[i] = is_known ? offsets[2 * i] : -1;
        registers->ends[i] = is_known ? offsets[2 * i + 1] : -1;
    }
    return 0;
}

/* The shared part of re_search and re_match: returns the match start (or its length, for a match) or a negative code. */
static int run_search(RegexBuffer *buffer, const char *string, int length, int first, int last, int direction, Registers *registers, int is_match)
{
    const RegexProgram *program = buffer->buffer;
    int eflags = ((buffer->flag_bits & BITS_NOT_BOL) ? REG_NOTBOL : 0) | ((buffer->flag_bits & BITS_NOT_EOL) ? REG_NOTEOL : 0);
    char *copy = sysv_malloc((size_t)length + 1);
    int offsets[2 * REGEX_MAX_GROUPS];
    if (copy == NULL) {
        return SEARCH_ERROR;
    }
    memcpy(copy, string, (size_t)length);
    copy[length] = '\0';
    int wants_registers = registers != NULL && !(buffer->flag_bits & BITS_NO_SUB);
    int code = regex_search_range(program, copy, eflags, first, last, direction, offsets, wants_registers);
    sysv_free(copy);
    if (code == REG_NOMATCH) {
        return SEARCH_FAILED;
    }
    if (code != REG_NOERROR || (wants_registers && fill_registers(buffer, registers, offsets) != 0)) {
        return SEARCH_ERROR;
    }
    return is_match ? offsets[1] - offsets[0] : offsets[0];
}

/* re_search(3): the offset of the leftmost match starting in [start, start + range] (or the rightmost, for a negative range). */
LIBC_FN int sysv_re_search(RegexBuffer *buffer, const char *string, int length, int start, int range, Registers *registers)
{
    int last = start + range;
    last = last < 0 ? 0 : (last > length ? length : last);
    if (start < 0 || start > length) {
        return SEARCH_FAILED;
    }
    return run_search(buffer, string, length, start, last, range >= 0 ? 1 : -1, registers, 0);
}

/* re_match(3): the length of the match that begins exactly at `start`, or -1. */
LIBC_FN int sysv_re_match(RegexBuffer *buffer, const char *string, int length, int start, Registers *registers)
{
    if (start < 0 || start > length) {
        return SEARCH_FAILED;
    }
    return run_search(buffer, string, length, start, start, 1, registers, 1);
}
