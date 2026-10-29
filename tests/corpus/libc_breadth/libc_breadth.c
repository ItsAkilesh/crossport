/*
 * File: tests/corpus/libc_breadth/libc_breadth.c
 * Purpose: Exercise everyday libc functions command-line tools use: getopt, string splitting, drand48, qsort_r, tsearch, hsearch, getline
 * Author: Akilesh
 * Date: 2026-10-29
 */

#define _GNU_SOURCE
#include <getopt.h>
#include <search.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define LINE_BYTES 128

static int g_failures;

static void expect(int condition, const char *what)
{
    printf("%s: %s\n", what, condition ? "ok" : "FAILED");
    g_failures += !condition;
}

static void test_getopt(void)
{
    char *argv[] = {"tool", "-a", "file1", "-b", "value", "--long=3", "file2", "-cX", "--", "-notopt", NULL};
    int argc = 10;
    static struct option options[] = {{"long", required_argument, NULL, 'l'}, {NULL, 0, NULL, 0}};
    int letter;
    optind = 1;
    while ((letter = getopt_long(argc, argv, "ab:c::", options, NULL)) != -1) {
        printf("option %c arg=%s\n", letter, optarg != NULL ? optarg : "(none)");
    }
    printf("remaining:");
    for (int i = optind; i < argc; i++) {
        printf(" %s", argv[i]);
    }
    printf("\n");
}

static void test_strings(void)
{
    char text[] = "one,two;;three";
    char *saved = NULL;
    for (char *token = strtok_r(text, ",;", &saved); token != NULL; token = strtok_r(NULL, ",;", &saved)) {
        printf("token <%s>\n", token);
    }
    char list[] = "a,,b";
    char *cursor = list;
    char *field;
    while ((field = strsep(&cursor, ",")) != NULL) {
        printf("field <%s>\n", field);
    }
    char *copy = strndup("truncate me", 8);
    expect(copy != NULL && strcmp(copy, "truncate") == 0, "strndup");
    free(copy);
    expect(strverscmp("file9", "file10") < 0 && strverscmp("a2", "a2") == 0, "strverscmp compares numbers");
    expect(strcasestr("Hello World", "WORLD") != NULL, "strcasestr");
    expect(memrchr("abcabc", 'b', 6) != NULL, "memrchr");
    expect(ffs(0x40) == 7, "ffs");
    printf("basename <%s>\n", basename("/usr/local/bin/tool"));
    printf("signal: %s\n", strsignal(SIGINT));
    char message[64];
    printf("error: %s\n", strerror_r(2, message, sizeof(message)));
}

static void test_random(void)
{
    srand48(42);
    printf("lrand48: %ld %ld\n", lrand48(), lrand48());
    unsigned short seed[3] = {1, 2, 3};
    printf("erand48: %.6f\n", erand48(seed));
}

static int compare_with_scale(const void *first, const void *second, void *scale)
{
    return (*(const int *)first - *(const int *)second) * *(int *)scale;
}

static int compare_ints(const void *first, const void *second)
{
    return *(const int *)first - *(const int *)second;
}

static void print_node(const void *node, VISIT order, int depth)
{
    (void)depth;
    if (order == postorder || order == leaf) {
        printf(" %d", **(int *const *)node);
    }
}

static void test_search(void)
{
    int values[] = {5, 3, 9, 1, 7};
    int scale = -1;
    qsort_r(values, 5, sizeof(int), compare_with_scale, &scale);
    printf("qsort_r descending:");
    for (int i = 0; i < 5; i++) {
        printf(" %d", values[i]);
    }
    printf("\n");
    int keys[] = {40, 10, 30, 20};
    void *root = NULL;
    for (int i = 0; i < 4; i++) {
        tsearch(&keys[i], &root, compare_ints);
    }
    printf("twalk:");
    twalk(root, print_node);
    printf("\n");
    int probe = 30;
    expect(tfind(&probe, &root, compare_ints) != NULL, "tfind");
    expect(tdelete(&probe, &root, compare_ints) != NULL && tfind(&probe, &root, compare_ints) == NULL, "tdelete");
    hcreate(10);
    ENTRY item = {"alpha", "one"};
    expect(hsearch(item, ENTER) != NULL, "hsearch enter");
    ENTRY look = {"alpha", NULL};
    ENTRY *found = hsearch(look, FIND);
    expect(found != NULL && strcmp(found->data, "one") == 0, "hsearch find");
    ENTRY missing = {"beta", NULL};
    expect(hsearch(missing, FIND) == NULL, "hsearch miss");
    hdestroy();
    size_t count = 4;
    int absent = 99;
    expect(lfind(&probe, keys, &count, sizeof(int), compare_ints) != NULL, "lfind hit");
    expect(lfind(&absent, keys, &count, sizeof(int), compare_ints) == NULL, "lfind miss");
}

static void test_getline_and_asprintf(void)
{
    FILE *file = fopen("lines.txt", "w");
    fputs("first\nsecond line\nlast", file);
    fclose(file);
    file = fopen("lines.txt", "r");
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    while ((length = getline(&line, &capacity, file)) != -1) {
        printf("line (%zd) <%s>\n", length, line);
    }
    free(line);
    fclose(file);
    unlink("lines.txt");
    char *formatted = NULL;
    expect(asprintf(&formatted, "%s-%d", "item", 7) == 6 && strcmp(formatted, "item-7") == 0, "asprintf");
    free(formatted);
    char template[] = "tmpXXXXXX";
    int fd = mkstemp(template);
    expect(fd >= 0 && strncmp(template, "tmp", 3) == 0 && template[3] != 'X', "mkstemp");
    close(fd);
    unlink(template);
    div_t result = div(17, 5);
    expect(result.quot == 3 && result.rem == 2, "div");
}

int main(void)
{
    test_getopt();
    test_strings();
    test_random();
    test_search();
    test_getline_and_asprintf();
    printf("failures: %d\n", g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
