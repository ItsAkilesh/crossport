/*
 * File: shim/regex_set.c
 * Purpose: Character sets for the regular-expression parser: bracket expressions, [:class:] names, and the \w and \s sets
 * Author: Akilesh
 * Date: 2026-11-24
 */

#include <string.h>

#include "regex_parser.h"

#define ALPHABET_SIZE 256

static int is_word_byte(int byte)
{
    return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '_';
}

static void set_add(RegexNode *node, int byte)
{
    node->set[byte / 8] |= (uint8_t)(1u << (byte % 8));
}

void regex_set_add_folded(Parser *parser, RegexNode *node, int byte)
{
    set_add(node, byte);
    if (parser->program->flags & REG_ICASE) {
        if (byte >= 'a' && byte <= 'z') {
            set_add(node, byte - 'a' + 'A');
        } else if (byte >= 'A' && byte <= 'Z') {
            set_add(node, byte - 'A' + 'a');
        }
    }
}


static int class_matches(const char *name, size_t length, int byte)
{
    if (length == 5 && memcmp(name, "alpha", 5) == 0) {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
    }
    if (length == 5 && memcmp(name, "digit", 5) == 0) {
        return byte >= '0' && byte <= '9';
    }
    if (length == 5 && memcmp(name, "alnum", 5) == 0) {
        return is_word_byte(byte) && byte != '_';
    }
    if (length == 5 && memcmp(name, "upper", 5) == 0) {
        return byte >= 'A' && byte <= 'Z';
    }
    if (length == 5 && memcmp(name, "lower", 5) == 0) {
        return byte >= 'a' && byte <= 'z';
    }
    if (length == 5 && memcmp(name, "space", 5) == 0) {
        return byte == ' ' || (byte >= '\t' && byte <= '\r');
    }
    if (length == 5 && memcmp(name, "blank", 5) == 0) {
        return byte == ' ' || byte == '\t';
    }
    if (length == 5 && memcmp(name, "punct", 5) == 0) {
        return byte > ' ' && byte < 127 && !(is_word_byte(byte) && byte != '_');
    }
    if (length == 5 && memcmp(name, "print", 5) == 0) {
        return byte >= ' ' && byte < 127;
    }
    if (length == 5 && memcmp(name, "graph", 5) == 0) {
        return byte > ' ' && byte < 127;
    }
    if (length == 5 && memcmp(name, "cntrl", 5) == 0) {
        return byte < ' ' || byte == 127;
    }
    if (length == 6 && memcmp(name, "xdigit", 6) == 0) {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F');
    }
    return -1;
}

/* Adds a [:name:] class to the set; the text after "[:" is at `at`. Returns the pointer past ":]" or NULL for an unknown name. */
static const char *add_class(Parser *parser, RegexNode *set, const char *at)
{
    const char *end = strstr(at, ":]");
    if (end == NULL) {
        parser->error = REG_EBRACK;
        return NULL;
    }
    size_t length = (size_t)(end - at);
    if (class_matches(at, length, 'a') < 0) {
        parser->error = REG_ECTYPE;
        return NULL;
    }
    for (int byte = 0; byte < ALPHABET_SIZE; byte++) {
        if (class_matches(at, length, byte) == 1) {
            regex_set_add_folded(parser, set, byte);
        }
    }
    return end + 2;
}

/* Reads one bracket element (a byte, or a [.c.] / [=c=] single-character form) and advances. -1 on error. */
static int bracket_byte(Parser *parser, const char **at)
{
    const char *cursor = *at;
    if (cursor[0] == '[' && (cursor[1] == '.' || cursor[1] == '=') && cursor[2] != '\0' && cursor[3] == cursor[1] && cursor[4] == ']') {
        *at = cursor + 5;
        return (unsigned char)cursor[2];
    }
    if (cursor[0] == '[' && (cursor[1] == '.' || cursor[1] == '=')) {
        parser->error = REG_ECOLLATE;
        return -1;
    }
    *at = cursor + 1;
    return (unsigned char)cursor[0];
}

static void invert(Parser *parser, RegexNode *set)
{
    for (int i = 0; i < REGEX_SET_BYTES; i++) {
        set->set[i] = (uint8_t)~set->set[i];
    }
    if (parser->program->flags & REG_NEWLINE) {
        set->set['\n' / 8] &= (uint8_t) ~(1u << ('\n' % 8));
    }
}

/* Parses a bracket expression; `parser->at` is just past the "[". */
RegexNode *regex_parse_bracket(Parser *parser)
{
    RegexNode *set = regex_make(parser, NODE_SET, NULL, NULL);
    const char *at = parser->at;
    int is_negated = *at == '^';
    at += is_negated;
    int is_first = 1;
    while (set != NULL && (*at != ']' || is_first)) {
        is_first = 0;
        if (*at == '\0') {
            parser->error = REG_EBRACK;
            return NULL;
        }
        if (at[0] == '[' && at[1] == ':') {
            at = add_class(parser, set, at + 2);
            if (at == NULL) {
                return NULL;
            }
            continue;
        }
        int low = bracket_byte(parser, &at);
        int high = low;
        if (low >= 0 && at[0] == '-' && at[1] != ']' && at[1] != '\0') {
            at++;
            high = bracket_byte(parser, &at);
        }
        if (low < 0 || high < 0) {
            return NULL;
        }
        if (high < low) {
            parser->error = REG_ERANGE;
            return NULL;
        }
        for (int byte = low; byte <= high; byte++) {
            regex_set_add_folded(parser, set, byte);
        }
    }
    if (set != NULL && is_negated) {
        invert(parser, set);
    }
    parser->at = at + 1;
    return set;
}

RegexNode *regex_word_set(Parser *parser, int is_word, int is_negated)
{
    RegexNode *set = regex_make(parser, NODE_SET, NULL, NULL);
    for (int byte = 0; set != NULL && byte < ALPHABET_SIZE; byte++) {
        int is_space = byte == ' ' || (byte >= '\t' && byte <= '\r');
        if (is_word ? is_word_byte(byte) : is_space) {
            set_add(set, byte);
        }
    }
    if (set != NULL && is_negated) {
        invert(parser, set);
    }
    return set;
}

