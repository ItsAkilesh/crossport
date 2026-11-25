/*
 * File: shim/regex_match.c
 * Purpose: Matches a parsed regular expression: leftmost start, longest match, with sub-matches and back-references
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * A backtracking search in continuation-passing style. Every path from one start position is explored and the longest match wins (the POSIX
 * rule); the first path found at that length supplies the sub-matches. A step budget stops pathological patterns with REG_ESPACE.
 */

#include <string.h>

#include "libc_abi.h"
#include "regex_internal.h"

#define STEP_BUDGET 20000000L

typedef struct Cont Cont;

typedef struct {
    const RegexProgram *program;
    const char *text;
    const char *limit; /* the terminating NUL */
    int eflags;
    int start_offset;
    int starts[REGEX_MAX_GROUPS];
    int ends[REGEX_MAX_GROUPS];
    int best_starts[REGEX_MAX_GROUPS];
    int best_ends[REGEX_MAX_GROUPS];
    int best_end;
    int is_found;
    int is_aborted;
    int is_first_enough;
    long steps;
} Match;

struct Cont {
    int (*run)(const Cont *self, const char *at, Match *match);
    const Cont *up;
    const RegexNode *node;
    int count;
    const char *origin;
};

static int step(const RegexNode *node, const char *at, Match *match, const Cont *next);

static int is_word_byte(int byte)
{
    return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '_';
}

static int fold(const Match *match, int byte)
{
    return (match->program->flags & REG_ICASE) && byte >= 'A' && byte <= 'Z' ? byte - 'A' + 'a' : byte;
}

static int has_newline_anchors(const Match *match)
{
    return (match->program->flags & (REG_NEWLINE | REGEX_ANCHOR_NEWLINES)) != 0;
}

/* Does the zero-width node hold at `at`? */
static int assertion_holds(const RegexNode *node, const char *at, const Match *match)
{
    int is_start = at == match->text;
    int is_end = *at == '\0';
    int before_is_word = !is_start && is_word_byte((unsigned char)at[-1]);
    int after_is_word = !is_end && is_word_byte((unsigned char)*at);
    switch (node->kind) {
    case NODE_LINE_START:
        return is_start ? !(match->eflags & REG_NOTBOL) : (has_newline_anchors(match) && at[-1] == '\n');
    case NODE_LINE_END:
        return is_end ? !(match->eflags & REG_NOTEOL) : (has_newline_anchors(match) && *at == '\n');
    case NODE_BUFFER_START:
        return is_start && !(match->eflags & REG_NOTBOL);
    case NODE_BUFFER_END:
        return is_end && !(match->eflags & REG_NOTEOL);
    case NODE_WORD_BOUNDARY:
        return before_is_word != after_is_word;
    case NODE_NOT_WORD_BOUNDARY:
        return before_is_word == after_is_word;
    case NODE_WORD_START:
        return !before_is_word && after_is_word;
    default:
        return before_is_word && !after_is_word; /* NODE_WORD_END */
    }
}

/* Does the single-byte node match the byte at `at`? */
static int byte_matches(const RegexNode *node, const char *at, const Match *match)
{
    int byte = (unsigned char)*at;
    if (byte == 0) {
        return 0;
    }
    switch (node->kind) {
    case NODE_CHAR:
        return fold(match, byte) == fold(match, node->value);
    case NODE_ANY:
        return !(has_newline_anchors(match) && byte == '\n');
    default:
        return (node->set[byte / 8] >> (byte % 8)) & 1; /* NODE_SET */
    }
}

static int finish(const Cont *self, const char *at, Match *match)
{
    (void)self;
    int end = (int)(at - match->text);
    if (!match->is_found || end > match->best_end) {
        match->is_found = 1;
        match->best_end = end;
        memcpy(match->best_starts, match->starts, sizeof(match->starts));
        memcpy(match->best_ends, match->ends, sizeof(match->ends));
    }
    return match->is_first_enough || at == match->limit;
}

static int then_step(const Cont *self, const char *at, Match *match)
{
    return step(self->node, at, match, self->up);
}

static int end_group(const Cont *self, const char *at, Match *match)
{
    int index = self->node->value;
    int old_start = match->starts[index];
    int old_end = match->ends[index];
    match->starts[index] = (int)(self->origin - match->text);
    match->ends[index] = (int)(at - match->text);
    int stop = self->up->run(self->up, at, match);
    match->starts[index] = old_start;
    match->ends[index] = old_end;
    return stop;
}

static int repeat(const RegexNode *node, const char *at, int count, Match *match, const Cont *next);

static int repeat_again(const Cont *self, const char *at, Match *match)
{
    if (at == self->origin && self->count > self->node->value) {
        /* An empty first iteration ends the loop with its group set to the empty string, as glibc does; a later one adds nothing. */
        return self->count == 1 ? self->up->run(self->up, at, match) : 0;
    }
    return repeat(self->node, at, self->count, match, self->up);
}

static int repeat(const RegexNode *node, const char *at, int count, Match *match, const Cont *next)
{
    if (node->maximum == REGEX_UNLIMITED || count < node->maximum) {
        Cont again = {repeat_again, next, node, count + 1, at};
        if (step(node->left, at, match, &again)) {
            return 1;
        }
    }
    return count >= node->value ? next->run(next, at, match) : 0;
}

static int match_backref(const RegexNode *node, const char *at, Match *match, const Cont *next)
{
    int group = node->value;
    if (match->starts[group] < 0) {
        return 0;
    }
    const char *saved = match->text + match->starts[group];
    int length = match->ends[group] - match->starts[group];
    for (int i = 0; i < length; i++) {
        if (at[i] == '\0' || fold(match, (unsigned char)at[i]) != fold(match, (unsigned char)saved[i])) {
            return 0;
        }
    }
    return next->run(next, at + length, match);
}

static int step(const RegexNode *node, const char *at, Match *match, const Cont *next)
{
    if (++match->steps > STEP_BUDGET) {
        match->is_aborted = 1;
        return 1;
    }
    switch (node->kind) {
    case NODE_EMPTY:
        return next->run(next, at, match);
    case NODE_CHAR: case NODE_ANY: case NODE_SET:
        return byte_matches(node, at, match) ? next->run(next, at + 1, match) : 0;
    case NODE_CAT: {
        Cont then = {then_step, next, node->right, 0, at};
        return step(node->left, at, match, &then);
    }
    case NODE_ALT:
        return step(node->left, at, match, next) || step(node->right, at, match, next);
    case NODE_GROUP: {
        if (node->value >= REGEX_MAX_GROUPS) {
            return step(node->left, at, match, next);
        }
        Cont close = {end_group, next, node, 0, at};
        return step(node->left, at, match, &close);
    }
    case NODE_REPEAT:
        return repeat(node, at, 0, match, next);
    case NODE_BACKREF:
        return match_backref(node, at, match, next);
    default:
        return assertion_holds(node, at, match) ? next->run(next, at, match) : 0;
    }
}

int regex_search_range(const RegexProgram *program, const char *text, int eflags, int first, int last, int direction, int offsets[2 * REGEX_MAX_GROUPS], int need_groups)
{
    Match match;
    memset(&match, 0, sizeof(match));
    match.program = program;
    match.text = text;
    match.limit = text + strlen(text);
    match.eflags = eflags;
    match.is_first_enough = !need_groups;
    Cont done = {finish, NULL, NULL, 0, NULL};
    int length = (int)(match.limit - text);
    for (int at = first; at >= 0 && at <= length && (direction > 0 ? at <= last : at >= last); at += direction) {
        for (int i = 0; i < REGEX_MAX_GROUPS; i++) {
            match.starts[i] = match.ends[i] = -1;
        }
        step(program->root, text + at, &match, &done);
        if (match.is_aborted) {
            return REG_ESPACE;
        }
        if (match.is_found) {
            match.best_starts[0] = at;
            match.best_ends[0] = match.best_end;
            for (int i = 0; i < REGEX_MAX_GROUPS; i++) {
                offsets[2 * i] = match.best_starts[i];
                offsets[2 * i + 1] = match.best_ends[i];
            }
            return 0;
        }
    }
    return REG_NOMATCH;
}

int regex_search(const RegexProgram *program, const char *text, int eflags, int offsets[2 * REGEX_MAX_GROUPS], int need_groups)
{
    return regex_search_range(program, text, eflags, 0, (int)strlen(text), 1, offsets, need_groups);
}
