/*
 * File: tests/corpus/regex_posix/regex_posix.c
 * Purpose: Compares regcomp, regexec and regerror with glibc over a table of basic and extended patterns: matches, sub-matches, flags and errors
 * Author: Akilesh
 * Date: 2026-11-24
 */

#define _GNU_SOURCE
#include <regex.h>
#include <stdio.h>
#include <string.h>

#define MAX_GROUPS 10

typedef struct {
    const char *pattern;
    int flags;
    const char *text;
    int exec_flags;
} Case;

#define BRE 0
#define ERE REG_EXTENDED
#define ICASE REG_ICASE
#define NL REG_NEWLINE

static const Case CASES[] = {
    {"abc", BRE, "xxabcxx", 0},
    {"a.c", BRE, "abc", 0},
    {"a*", BRE, "baaa", 0},
    {"a\\+", BRE, "baaa", 0},
    {"a\\?b", BRE, "ab", 0},
    {"a\\{2,3\\}", BRE, "aaaa", 0},
    {"\\(ab\\)*c", BRE, "ababc", 0},
    {"\\(a\\)\\(b\\)\\(c\\)", BRE, "abc", 0},
    {"\\(a*\\)\\1", BRE, "aaaa", 0},
    {"^abc$", BRE, "abc", 0},
    {"^abc$", BRE, "abcd", 0},
    {"a^b", BRE, "a^b", 0},
    {"a$b", BRE, "a$b", 0},
    {"*a", BRE, "*a", 0},
    {"\\(*a\\)", BRE, "*a", 0},
    {"a\\|b", BRE, "xb", 0},
    {"[abc]", BRE, "xxb", 0},
    {"[^abc]", BRE, "abcd", 0},
    {"[a-c]*", BRE, "abcabcx", 0},
    {"[]a]", BRE, "x]", 0},
    {"[[:digit:]]\\+", BRE, "abc123def", 0},
    {"[[:alpha:]_][[:alnum:]_]*", BRE, "  foo_bar9 baz", 0},
    {"[[:space:]]", BRE, "ab cd", 0},
    {"[[:upper:]][[:lower:]]*", BRE, "xx Hello", 0},
    {"[[:punct:]]", BRE, "ab,c", 0},
    {"a+", ERE, "caaat", 0},
    {"a|b|c", ERE, "xxc", 0},
    {"(a|ab)(c|bcd)", ERE, "abcd", 0},
    {"(a+)(b+)?", ERE, "aaac", 0},
    {"(a*)*", ERE, "b", 0},
    {"(a*)+", ERE, "aa", 0},
    {"a{2}", ERE, "aaaa", 0},
    {"a{2,}", ERE, "caaaa", 0},
    {"a{,2}", ERE, "aaa", 0},
    {"(foo|foobar)", ERE, "foobar", 0},
    {"x*", ERE, "abc", 0},
    {"^", ERE, "abc", 0},
    {"$", ERE, "abc", 0},
    {"^$", ERE, "", 0},
    {"(^|,)a", ERE, "b,a", 0},
    {"a(,|$)", ERE, "xa", 0},
    {"\\bfoo\\b", ERE, "a foo b", 0},
    {"\\bfoo\\b", ERE, "afoo b", 0},
    {"\\<foo\\>", ERE, "x foo.", 0},
    {"\\w+", ERE, "  hello_1 world", 0},
    {"\\W+", ERE, "ab, cd", 0},
    {"\\s+", ERE, "ab \t cd", 0},
    {"\\S+", ERE, "  ab cd", 0},
    {"\\B", ERE, "ab", 0},
    {"(a)\\1", ERE, "aa", 0},
    {"(a)(b)\\2\\1", ERE, "abba", 0},
    {"hello", ICASE, "say HeLLo", 0},
    {"[a-c]+", ERE | ICASE, "xxABCxx", 0},
    {"[[:upper:]]+", ERE | ICASE, "abc", 0},
    {"(A)\\1", ERE | ICASE, "aA", 0},
    {"a.b", ERE, "a\nb", 0},
    {"a.b", ERE | NL, "a\nb", 0},
    {"^b", ERE | NL, "a\nb", 0},
    {"a$", ERE | NL, "a\nb", 0},
    {"^b", ERE, "a\nb", 0},
    {"[^x]b", ERE | NL, "\nb", 0},
    {"^abc", ERE, "abc", REG_NOTBOL},
    {"abc$", ERE, "abc", REG_NOTEOL},
    {"a\\.b", ERE, "axb a.b", 0},
    {"a\\*b", ERE, "a*b", 0},
    {"\\(", ERE, "(", 0},
    {"[.]", BRE, "a.b", 0},
    {"[a\\]", BRE, "\\", 0},
    {"colou?r", ERE, "my color", 0},
    {"(ab|a)(bc|c)?", ERE, "abc", 0},
    {"a|b*", ERE, "bbb", 0},
    {"(.*)@(.*)", ERE, "user@example.com", 0},
    {"([0-9]+)-([0-9]+)-([0-9]+)", ERE, "date 2026-11-24 end", 0},
    {"^[ \t]*#", ERE, "   # comment", 0},
    {".*", ERE, "", 0},
    {"a{0}b", ERE, "ab", 0},
    {"(a|b)*c", ERE, "ababc", 0},
    {"((a)|(b))+", ERE, "ab", 0},
    {"a**", BRE, "aaa", 0},
    {"\\(^a\\)", BRE, "a", 0},
    {"x\\{1,2\\}y", BRE, "xxxy", 0},
    /* syntax errors: the message must match too */
    {"a\\{1", BRE, "", 0},
    {"\\(a", BRE, "", 0},
    {"a\\)", BRE, "", 0},
    {"(a", ERE, "", 0},
    {"a)", ERE, "", 0},
    {"[a", BRE, "", 0},
    {"[[:foo:]]", BRE, "", 0},
    {"[z-a]", BRE, "", 0},
    {"*a", ERE, "", 0},
    {"a{2,1}", ERE, "", 0},
    {"a\\", BRE, "", 0},
    {"\\1", BRE, "", 0},
    {"(a)\\2", ERE, "", 0},
};

static void run_case(const Case *test)
{
    regex_t compiled;
    char message[128];
    int code = regcomp(&compiled, test->pattern, test->flags);
    if (code != 0) {
        regerror(code, &compiled, message, sizeof(message));
        printf("[%s] flags %d: compile error %d: %s\n", test->pattern, test->flags, code, message);
        return;
    }
    regmatch_t groups[MAX_GROUPS];
    code = regexec(&compiled, test->text, MAX_GROUPS, groups, test->exec_flags);
    printf("[%s] flags %d on [%s]: ", test->pattern, test->flags, test->text);
    if (code != 0) {
        regerror(code, &compiled, message, sizeof(message));
        printf("no match (%d: %s)\n", code, message);
    } else {
        printf("nsub %d:", (int)compiled.re_nsub);
        for (size_t i = 0; i <= compiled.re_nsub && i < MAX_GROUPS; i++) {
            printf(" (%d,%d)", (int)groups[i].rm_so, (int)groups[i].rm_eo);
        }
        printf("\n");
    }
    regfree(&compiled);
}

int main(void)
{
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        run_case(&CASES[i]);
    }
    regex_t compiled;
    regcomp(&compiled, "b+", REG_EXTENDED | REG_NOSUB);
    printf("nosub match %d no match %d\n", regexec(&compiled, "abbc", 0, NULL, 0), regexec(&compiled, "xyz", 0, NULL, 0));
    regfree(&compiled);
    char small[8];
    size_t needed = regerror(REG_EPAREN, NULL, small, sizeof(small));
    printf("regerror needed %d truncated [%s]\n", (int)needed, small);
    return 0;
}
