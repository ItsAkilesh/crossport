/*
 * File: shim/regex_parse.c
 * Purpose: Parses POSIX basic and extended regular expressions (with the GNU extensions \w \s \b \< \> and back-references) into a node tree
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Matching is byte-wise: a multibyte UTF-8 character is several bytes, and "." matches one of them.
 * Grammar (ERE; BRE is the same with \( \) \{ \} \| \+ \? as the operators):
 *   alt := cat ('|' cat)*    cat := piece*    piece := atom ('*' | '+' | '?' | '{m,n}')*
 */

#include <string.h>

#include "libc_abi.h"
#include "regex_parser.h"

#define MAX_REPEAT 32767
#define FIRST_BACKREF '1'
#define LAST_BACKREF '9'

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void sysv_free(void *block);


RegexNode *regex_make(Parser *parser, NodeKind kind, RegexNode *left, RegexNode *right)
{
    RegexNode *node = sysv_malloc(sizeof(*node));
    if (node == NULL) {
        parser->error = REG_ESPACE;
        return NULL;
    }
    memset(node, 0, sizeof(*node));
    node->kind = kind;
    node->left = left;
    node->right = right;
    node->allocated_before = parser->program->allocations;
    parser->program->allocations = node;
    return node;
}

static RegexNode *literal(Parser *parser, int byte)
{
    int is_letter = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
    if ((parser->program->flags & REG_ICASE) && is_letter) {
        RegexNode *set = regex_make(parser, NODE_SET, NULL, NULL);
        if (set != NULL) {
            regex_set_add_folded(parser, set, byte);
        }
        return set;
    }
    RegexNode *node = regex_make(parser, NODE_CHAR, NULL, NULL);
    if (node != NULL) {
        node->value = byte;
    }
    return node;
}

/* Does the pattern continue with the two-byte BRE operator `\x` or the one-byte ERE operator `x`? */
static int is_operator(const Parser *parser, char byte)
{
    if (parser->is_ere) {
        return parser->at[0] == byte;
    }
    return parser->at[0] == '\\' && parser->at[1] == byte;
}

static void skip_operator(Parser *parser)
{
    parser->at += parser->is_ere ? 1 : 2;
}

static RegexNode *parse_escape(Parser *parser)
{
    char byte = *parser->at++;
    NodeKind simple = NODE_EMPTY;
    switch (byte) {
    case '\0':
        parser->error = REG_EESCAPE;
        parser->at--;
        return NULL;
    case 'w': case 'W':
        return regex_word_set(parser, 1, byte == 'W');
    case 's': case 'S':
        return regex_word_set(parser, 0, byte == 'S');
    case 'b': simple = NODE_WORD_BOUNDARY; break;
    case 'B': simple = NODE_NOT_WORD_BOUNDARY; break;
    case '<': simple = NODE_WORD_START; break;
    case '>': simple = NODE_WORD_END; break;
    case '`': simple = NODE_BUFFER_START; break;
    case '\'': simple = NODE_BUFFER_END; break;
    case ')':
        if (!parser->is_ere && parser->depth == 0) {
            parser->error = REG_EPAREN;
            return NULL;
        }
        break;
    default:
        break;
    }
    if (simple != NODE_EMPTY) {
        return regex_make(parser, simple, NULL, NULL);
    }
    if (byte >= FIRST_BACKREF && byte <= LAST_BACKREF) {
        if (byte - '0' > parser->closed_groups) {
            parser->error = REG_ESUBREG;
            return NULL;
        }
        RegexNode *node = regex_make(parser, NODE_BACKREF, NULL, NULL);
        if (node != NULL) {
            node->value = byte - '0';
        }
        return node;
    }
    return literal(parser, (unsigned char)byte);
}

static RegexNode *parse_alt(Parser *parser);

static RegexNode *parse_group(Parser *parser)
{
    int index = ++parser->program->group_count;
    parser->depth++;
    parser->is_start = 1;
    RegexNode *inner = parse_alt(parser);
    if (inner == NULL) {
        return NULL;
    }
    if (!is_operator(parser, ')')) {
        parser->error = REG_EPAREN;
        return NULL;
    }
    skip_operator(parser);
    parser->depth--;
    parser->closed_groups = index > parser->closed_groups ? index : parser->closed_groups;
    RegexNode *group = regex_make(parser, NODE_GROUP, inner, NULL);
    if (group != NULL) {
        group->value = index;
    }
    return group;
}

/* BRE: is "$" an anchor here (last in the pattern, or before \) or \|)? */
static int dollar_is_anchor(const Parser *parser)
{
    const char *next = parser->at + 1;
    return parser->is_ere || next[0] == '\0' || (next[0] == '\\' && (next[1] == ')' || next[1] == '|'));
}

static RegexNode *parse_atom(Parser *parser)
{
    char byte = *parser->at;
    int was_start = parser->is_start;
    parser->is_start = 0;
    if (parser->is_ere ? byte == '(' : (byte == '\\' && parser->at[1] == '(')) {
        parser->at += parser->is_ere ? 1 : 2;
        return parse_group(parser);
    }
    if (byte == '[') {
        parser->at++;
        return regex_parse_bracket(parser);
    }
    if (byte == '.') {
        parser->at++;
        return regex_make(parser, NODE_ANY, NULL, NULL);
    }
    if (byte == '^' && (parser->is_ere || was_start)) {
        parser->at++;
        parser->is_start = !parser->is_ere;
        return regex_make(parser, NODE_LINE_START, NULL, NULL);
    }
    if (byte == '$' && dollar_is_anchor(parser)) {
        parser->at++;
        return regex_make(parser, NODE_LINE_END, NULL, NULL);
    }
    if (byte == '\\') {
        parser->at++;
        return parse_escape(parser);
    }
    if (parser->is_ere && (byte == '*' || byte == '+' || byte == '?')) {
        parser->error = REG_BADRPT;
        return NULL;
    }
    parser->at++;
    return literal(parser, (unsigned char)byte);
}

static int read_number(Parser *parser)
{
    int value = -1;
    while (*parser->at >= '0' && *parser->at <= '9') {
        value = (value < 0 ? 0 : value) * 10 + (*parser->at - '0');
        value = value > MAX_REPEAT + 1 ? MAX_REPEAT + 1 : value;
        parser->at++;
    }
    return value;
}

/* Parses "m", "m,", "m,n" or ",n" and the closing brace; `at` is just past the opening brace. Returns 0 or a REG_* code. */
static int parse_interval(Parser *parser, int *minimum, int *maximum)
{
    *minimum = read_number(parser);
    *maximum = *minimum;
    if (*parser->at == ',') {
        parser->at++;
        *maximum = read_number(parser);
        *maximum = *maximum < 0 ? REGEX_UNLIMITED : *maximum;
    }
    *minimum = *minimum < 0 ? 0 : *minimum;
    if (!is_operator(parser, '}')) {
        return parser->at[0] == '\0' ? REG_EBRACE : REG_BADBR;
    }
    skip_operator(parser);
    if (*maximum != REGEX_UNLIMITED && *maximum < *minimum) {
        return REG_BADBR;
    }
    return *minimum > MAX_REPEAT || *maximum > MAX_REPEAT ? REG_ESIZE : REG_NOERROR;
}

/* Reads the next repetition operator into (minimum, maximum); returns 1 when there is one. */
static int next_repeat(Parser *parser, int *minimum, int *maximum)
{
    if (parser->at[0] == '*') {
        parser->at++;
        *minimum = 0;
        *maximum = REGEX_UNLIMITED;
        return 1;
    }
    if (is_operator(parser, '+') || is_operator(parser, '?')) {
        int is_plus = parser->at[parser->is_ere ? 0 : 1] == '+';
        skip_operator(parser);
        *minimum = is_plus ? 1 : 0;
        *maximum = is_plus ? REGEX_UNLIMITED : 1;
        return 1;
    }
    int is_brace = is_operator(parser, '{');
    int has_digits = parser->at[parser->is_ere ? 1 : 2] >= '0' && parser->at[parser->is_ere ? 1 : 2] <= '9';
    if (!is_brace || (parser->is_ere && !has_digits && parser->at[1] != ',')) {
        return 0;
    }
    skip_operator(parser);
    parser->error = parse_interval(parser, minimum, maximum);
    return parser->error == REG_NOERROR;
}

static RegexNode *parse_piece(Parser *parser)
{
    RegexNode *atom = parse_atom(parser);
    int minimum;
    int maximum;
    int is_repeated = 0;
    while (atom != NULL && next_repeat(parser, &minimum, &maximum)) {
        if (is_repeated && !parser->is_ere) {
            parser->error = REG_BADRPT; /* in a basic expression an operator cannot follow another one */
            return NULL;
        }
        is_repeated = 1;
        atom = regex_make(parser, NODE_REPEAT, atom, NULL);
        if (atom != NULL) {
            atom->value = minimum;
            atom->maximum = maximum;
        }
    }
    if (parser->error != REG_NOERROR) {
        return NULL;
    }
    return atom;
}

static int at_end_of_cat(const Parser *parser)
{
    return parser->at[0] == '\0' || is_operator(parser, '|') || (parser->depth > 0 && is_operator(parser, ')'));
}

static RegexNode *parse_cat(Parser *parser)
{
    RegexNode *result = regex_make(parser, NODE_EMPTY, NULL, NULL);
    while (result != NULL && !at_end_of_cat(parser)) {
        RegexNode *piece = parse_piece(parser);
        result = piece != NULL ? regex_make(parser, NODE_CAT, result, piece) : NULL;
    }
    return result;
}

static RegexNode *parse_alt(Parser *parser)
{
    parser->is_start = 1;
    RegexNode *left = parse_cat(parser);
    while (left != NULL && is_operator(parser, '|')) {
        skip_operator(parser);
        parser->is_start = 1;
        RegexNode *right = parse_cat(parser);
        left = right != NULL ? regex_make(parser, NODE_ALT, left, right) : NULL;
    }
    return left;
}

void regex_free(RegexProgram *program)
{
    RegexNode *node = program->allocations;
    while (node != NULL) {
        RegexNode *before = node->allocated_before;
        sysv_free(node);
        node = before;
    }
    program->allocations = NULL;
    program->root = NULL;
}

int regex_parse(RegexProgram *program, const char *pattern, int flags)
{
    Parser parser = {.at = pattern, .program = program, .is_ere = (flags & REG_EXTENDED) != 0};
    program->flags = flags;
    program->group_count = 0;
    program->allocations = NULL;
    program->root = parse_alt(&parser);
    if (program->root != NULL && parser.at[0] != '\0') {
        parser.error = REG_ERPAREN;
    }
    if (program->root == NULL || parser.error != REG_NOERROR) {
        regex_free(program);
        return parser.error != REG_NOERROR ? parser.error : REG_BADPAT;
    }
    return REG_NOERROR;
}
