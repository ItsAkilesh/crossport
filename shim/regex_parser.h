/*
 * File: shim/regex_parser.h
 * Purpose: The parser state and the helpers regex_parse.c and regex_set.c share
 * Author: Akilesh
 * Date: 2026-11-24
 */

#ifndef CROSSPORT_REGEX_PARSER_H
#define CROSSPORT_REGEX_PARSER_H

#include "regex_internal.h"

typedef struct {
    const char *at;
    RegexProgram *program;
    int is_ere;
    int is_start; /* at the start of an expression: a BRE "*" is literal and "^" is an anchor here */
    int depth;
    int closed_groups; /* groups whose closing parenthesis has been seen: the valid back-references */
    int error;
} Parser;

RegexNode *regex_make(Parser *parser, NodeKind kind, RegexNode *left, RegexNode *right);
void regex_set_add_folded(Parser *parser, RegexNode *node, int byte);
RegexNode *regex_parse_bracket(Parser *parser);
RegexNode *regex_word_set(Parser *parser, int is_word, int is_negated);

#endif
