/*
 * File: shim/regex_api.c
 * Purpose: The POSIX regex interface over the shim's own engine: regcomp, regexec, regerror and regfree
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * regex_t and regmatch_t keep glibc's layouts (the program reads re_nsub and passes the structure by pointer). Before this file regcomp
 * always failed, which blocked every program that filters text with a pattern.
 */

#include <string.h>

#include "libc_abi.h"
#include "regex_internal.h"

#define MESSAGE_COUNT 17

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void sysv_free(void *block);

typedef struct {
    int32_t start;
    int32_t end;
} RegMatch;

static const char *const MESSAGES[MESSAGE_COUNT] = {
    "Success",
    "No match",
    "Invalid regular expression",
    "Invalid collation character",
    "Invalid character class name",
    "Trailing backslash",
    "Invalid back reference",
    "Unmatched [, [^, [:, [., or [=",
    "Unmatched ( or \\(",
    "Unmatched \\{",
    "Invalid content of \\{\\}",
    "Invalid range end",
    "Memory exhausted",
    "Invalid preceding regular expression",
    "Premature end of regular expression",
    "Regular expression too big",
    "Unmatched ) or \\)",
};

const char *regex_message(int code)
{
    return code >= 0 && code < MESSAGE_COUNT ? MESSAGES[code] : "Unknown error";
}

int regex_compile(RegexBuffer *pattern_buffer, const char *pattern, int flags)
{
    RegexProgram *program = sysv_malloc(sizeof(*program));
    if (program == NULL) {
        return REG_ESPACE;
    }
    memset(pattern_buffer, 0, sizeof(*pattern_buffer));
    int code = regex_parse(program, pattern, flags);
    if (code != REG_NOERROR) {
        sysv_free(program);
        return code;
    }
    pattern_buffer->buffer = program;
    pattern_buffer->re_nsub = (size_t)program->group_count;
    return REG_NOERROR;
}

LIBC_FN int sysv_regcomp(RegexBuffer *pattern_buffer, const char *pattern, int flags)
{
    return regex_compile(pattern_buffer, pattern, flags);
}

LIBC_FN int sysv_regexec(const RegexBuffer *pattern_buffer, const char *text, size_t count, RegMatch *matches, int eflags)
{
    const RegexProgram *program = pattern_buffer->buffer;
    int offsets[2 * REGEX_MAX_GROUPS];
    int wants_groups = !(program->flags & REG_NOSUB) && count > 0 && matches != NULL;
    int code = regex_search(program, text, eflags, offsets, wants_groups && count > 1);
    if (code != REG_NOERROR || !wants_groups) {
        return code;
    }
    for (size_t i = 0; i < count; i++) {
        int is_known = i < REGEX_MAX_GROUPS && i <= (size_t)program->group_count;
        matches[i].start = is_known ? offsets[2 * i] : -1;
        matches[i].end = is_known ? offsets[2 * i + 1] : -1;
    }
    return REG_NOERROR;
}

LIBC_FN size_t sysv_regerror(int code, const RegexBuffer *pattern_buffer, char *buffer, size_t size)
{
    (void)pattern_buffer;
    const char *message = regex_message(code);
    size_t needed = strlen(message) + 1;
    if (size > 0) {
        size_t copied = needed < size ? needed : size - 1;
        memcpy(buffer, message, copied);
        buffer[copied] = '\0';
    }
    return needed;
}

LIBC_FN void sysv_regfree(RegexBuffer *pattern_buffer)
{
    RegexProgram *program = pattern_buffer->buffer;
    if (program != NULL) {
        regex_free(program);
        sysv_free(program);
    }
    pattern_buffer->buffer = NULL;
}
