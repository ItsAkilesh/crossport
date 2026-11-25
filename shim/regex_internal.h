/*
 * File: shim/regex_internal.h
 * Purpose: The compiled form of a POSIX regular expression and the entry points shared by the parser, the matcher and the C interface
 * Author: Akilesh
 * Date: 2026-11-24
 */

#ifndef CROSSPORT_REGEX_INTERNAL_H
#define CROSSPORT_REGEX_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#define REGEX_SET_BYTES 32
#define REGEX_MAX_GROUPS 10 /* group 0 is the whole match; \1 to \9 can be referenced */
#define REGEX_UNLIMITED (-1)

/* regcomp flags and regexec flags, as in <regex.h>. */
enum { REG_EXTENDED = 1, REG_ICASE = 2, REG_NEWLINE = 4, REG_NOSUB = 8, REGEX_ANCHOR_NEWLINES = 16 }; /* the last: only ^ and $ see newlines */
enum { REG_NOTBOL = 1, REG_NOTEOL = 2 };

/* Error codes, as in <regex.h>. */
enum {
    REG_NOERROR = 0, REG_NOMATCH, REG_BADPAT, REG_ECOLLATE, REG_ECTYPE, REG_EESCAPE, REG_ESUBREG, REG_EBRACK, REG_EPAREN, REG_EBRACE,
    REG_BADBR, REG_ERANGE, REG_ESPACE, REG_BADRPT, REG_EEND, REG_ESIZE, REG_ERPAREN
};

typedef enum {
    NODE_EMPTY, NODE_CHAR, NODE_ANY, NODE_SET, NODE_LINE_START, NODE_LINE_END, NODE_BUFFER_START, NODE_BUFFER_END, NODE_WORD_BOUNDARY,
    NODE_NOT_WORD_BOUNDARY, NODE_WORD_START, NODE_WORD_END, NODE_GROUP, NODE_BACKREF, NODE_CAT, NODE_ALT, NODE_REPEAT
} NodeKind;

typedef struct RegexNode {
    NodeKind kind;
    struct RegexNode *left;
    struct RegexNode *right;
    struct RegexNode *allocated_before; /* the allocation list regex_free walks */
    int value;                          /* character, group number, or the minimum of a repeat */
    int maximum;                        /* the maximum of a repeat, or REGEX_UNLIMITED */
    uint8_t set[REGEX_SET_BYTES];
} RegexNode;

typedef struct {
    RegexNode *root;
    RegexNode *allocations;
    int group_count; /* number of parenthesised groups */
    int flags;
} RegexProgram;

/* Parses `pattern` into `program`. Returns 0 or a REG_* error code; on error the program owns nothing. */
int regex_parse(RegexProgram *program, const char *pattern, int flags);

/* Releases every node of a program. */
void regex_free(RegexProgram *program);

/* struct re_pattern_buffer (regex_t) on x86-64 glibc: 64 bytes, re_nsub at offset 48; flag_bits holds the C bit-fields. */
typedef struct {
    void *buffer;
    size_t allocated;
    size_t used;
    uint64_t syntax;
    char *fastmap;
    uint8_t *translate;
    size_t re_nsub;
    uint32_t flag_bits;
    uint32_t padding;
} RegexBuffer;

_Static_assert(sizeof(RegexBuffer) == 64, "regex_t must match glibc's layout");

/* Compiles into `buffer` (regcomp's work); returns 0 or a REG_* code. */
int regex_compile(RegexBuffer *buffer, const char *pattern, int flags);

/* Like regex_search, but tries start positions from `first` to `last` (stepping by `direction`, +1 or -1) and returns the match length
 * relative to nothing: offsets are positions in `text`. */
int regex_search_range(const RegexProgram *program, const char *text, int eflags, int first, int last, int direction, int offsets[2 * REGEX_MAX_GROUPS], int need_groups);

/* Finds the leftmost longest match at or after `start` in `text`. offsets receives 2 * REGEX_MAX_GROUPS ints (-1 when a group did not match).
 * Returns 0 for a match, REG_NOMATCH, or REG_ESPACE when the step budget ran out. */
int regex_search(const RegexProgram *program, const char *text, int eflags, int offsets[2 * REGEX_MAX_GROUPS], int need_groups);

/* The message glibc gives for an error code. */
const char *regex_message(int code);

#endif
