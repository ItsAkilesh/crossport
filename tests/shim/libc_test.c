/*
 * File: tests/shim/libc_test.c
 * Purpose: Checks the shim's C library functions against outputs recorded from glibc 2.39
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * The shim exports System V functions under their libc names, so each is declared here with an alias to its
 * real symbol and the sysv_abi attribute, exactly as translated code would call it. Only functions that need
 * no process startup (no stdio streams, no TLS) are tested; the corpus apps cover the rest end to end.
 * Names the shim also exports (strcmp, memcmp, printf...) must not be called directly here: the linker
 * would bind them to the System V versions. The test therefore has its own comparison and output helpers.
 */

#include <stdarg.h>
#include <stdint.h>
#include <windows.h>

#define SYSV __attribute__((sysv_abi))
#define SHIM_FUNCTIONS(X) \
    X(int, snprintf, (char *, size_t, const char *, ...)) \
    X(int, sscanf, (const char *, const char *, ...)) \
    X(double, strtod, (const char *, char **)) \
    X(int64_t, strtol, (const char *, char **, int)) \
    X(size_t, strftime, (char *, size_t, const char *, const void *)) \
    X(void *, gmtime_r, (const int64_t *, void *)) \
    X(void, qsort, (void *, size_t, size_t, int (SYSV *)(const void *, const void *))) \
    X(size_t, strlen, (const char *)) \
    X(char *, strchr, (const char *, int)) \
    X(char *, strrchr, (const char *, int)) \
    X(char *, strstr, (const char *, const char *)) \
    X(size_t, strcspn, (const char *, const char *)) \
    X(int, strcasecmp, (const char *, const char *)) \
    X(void *, memmove, (void *, const void *, size_t)) \
    X(int, memcmp, (const void *, const void *, size_t)) \
    X(char *, getcwd, (char *, size_t))     X(void *, aligned_alloc, (size_t, size_t))     X(int, posix_memalign, (void **, size_t, size_t))     X(void *, realloc, (void *, size_t))     X(void, free, (void *))     X(int, pthread_rwlock_rdlock, (void *))     X(int, pthread_rwlock_wrlock, (void *))     X(int, pthread_rwlock_tryrdlock, (void *))     X(int, pthread_rwlock_trywrlock, (void *))     X(int, pthread_rwlock_unlock, (void *))     X(void *, dlopen, (const char *, int))     X(void *, dlsym, (void *, const char *))     X(char *, dlerror, (void))

#define DECLARE(ret, name, params) static ret (SYSV *shim_##name) params;
SHIM_FUNCTIONS(DECLARE)

#define LOAD(ret, name, params) \
    if ((shim_##name = (void *)GetProcAddress(module, #name)) == NULL) { \
        return #name; \
    }

/* Binds every function by name; returns the first missing name, or NULL. */
static const char *load_all(HMODULE module)
{
    SHIM_FUNCTIONS(LOAD)
    return NULL;
}

static int g_failures;
static int g_checks;

static void report(const char *format, ...)
{
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    int length = wvsprintfA(line, format, arguments);
    va_end(arguments);
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line, (DWORD)length, &written, NULL);
}

static int same_text(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static int same_bytes(const void *left, const void *right, size_t count)
{
    const uint8_t *a = left;
    const uint8_t *b = right;
    for (size_t i = 0; i < count; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static void check(int condition, const char *what, int line)
{
    g_checks++;
    if (!condition) {
        g_failures++;
        report("FAIL line %d: %s\n", line, what);
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)
#define CHECK_TEXT(actual, expected) check(same_text((actual), (expected)), #actual " == " #expected, __LINE__)

static char g_buffer[256];

static const char *format_int(const char *format, long long value)
{
    shim_snprintf(g_buffer, sizeof(g_buffer), format, value);
    return g_buffer;
}

static const char *format_double(const char *format, double value)
{
    shim_snprintf(g_buffer, sizeof(g_buffer), format, value);
    return g_buffer;
}

static void test_printf_integers(void)
{
    shim_snprintf(g_buffer, sizeof(g_buffer), "[%5lld|%-5lld|%05lld|%+lld]", 42LL, 42LL, 42LL, 42LL);
    CHECK_TEXT(g_buffer, "[   42|42   |00042|+42]");
    shim_snprintf(g_buffer, sizeof(g_buffer), "[%llx|%#llx|%#llo|%llX]", 255LL, 255LL, 255LL, 255LL);
    CHECK_TEXT(g_buffer, "[ff|0xff|0377|FF]");
    CHECK_TEXT(format_int("%lld", INT64_MIN), "-9223372036854775808");
    CHECK_TEXT(format_int("%llu", -1), "18446744073709551615");
    shim_snprintf(g_buffer, sizeof(g_buffer), "[%.5lld|%8.3lld]", 7LL, 7LL);
    CHECK_TEXT(g_buffer, "[00007|     007]");
    shim_snprintf(g_buffer, sizeof(g_buffer), "%d %hd %hhd %lld %zu", -5, 70000, 300, -7LL, (size_t)9);
    CHECK_TEXT(g_buffer, "-5 4464 44 -7 9");
}

static void test_printf_text_and_pointers(void)
{
    shim_snprintf(g_buffer, sizeof(g_buffer), "[%s|%.3s|%10.4s|%-6s|%c|%%]", "hello", "abcdef", "abcdef", "ab", 'x');
    CHECK_TEXT(g_buffer, "[hello|abc|      abcd|ab    |x|%]");
    shim_snprintf(g_buffer, sizeof(g_buffer), "%s %p %*d", (char *)0, (void *)0, 4, 9);
    CHECK_TEXT(g_buffer, "(null) (nil)    9");
    CHECK(shim_snprintf(g_buffer, 4, "abcdefgh") == 8);
    CHECK_TEXT(g_buffer, "abc");
}

static void test_printf_floats(void)
{
    CHECK_TEXT(format_double("%e", 12345.6789), "1.234568e+04");
    CHECK_TEXT(format_double("%g", 0.0001), "0.0001");
    CHECK_TEXT(format_double("%g", 0.00001), "1e-05");
    CHECK_TEXT(format_double("%.0f", 2.5), "2");
    CHECK_TEXT(format_double("%5.1f", 3.14159), "  3.1");
    CHECK_TEXT(format_double("%.14g", 1.0 / 3.0), "0.33333333333333");
    CHECK_TEXT(format_double("%.17g", 0.1), "0.10000000000000001");
    CHECK_TEXT(format_double("%f", 1e15), "1000000000000000.000000");
    CHECK_TEXT(format_double("%g", 1e100), "1e+100");
    CHECK_TEXT(format_double("%f", 1.0 / 0.0), "inf");
    CHECK_TEXT(format_double("%F", -1.0 / 0.0), "-INF");
    CHECK_TEXT(format_double("%+.2f", 0.0), "+0.00");
}

static void test_scanf(void)
{
    int number = 0;
    char word[16] = "";
    double real = 0;
    CHECK(shim_sscanf("12 abc 3.5", "%d %15s %lf", &number, word, &real) == 3);
    CHECK(number == 12 && same_text(word, "abc") && real == 3.5);
    CHECK(shim_sscanf("0x1f", "%i", &number) == 1 && number == 31);
    CHECK(shim_sscanf("-17", "%d", &number) == 1 && number == -17);
    CHECK(shim_sscanf("abc", "%d", &number) == 0);
    CHECK(shim_sscanf("", "%d", &number) == -1);
    int consumed = 0;
    CHECK(shim_sscanf("key=value", "%[a-z]=%n", word, &consumed) == 1 && consumed == 4 && same_text(word, "key"));
    char letters[3] = "";
    CHECK(shim_sscanf("xyz", "%2c", letters) == 1 && same_bytes(letters, "xy", 2));
    CHECK(shim_sscanf("7,8", "%d,%d", &number, &consumed) == 2 && number == 7 && consumed == 8);
    long long wide = 0;
    CHECK(shim_sscanf("9223372036854775807", "%lld", &wide) == 1 && wide == INT64_MAX);
    CHECK(shim_sscanf("5 6", "%*d %d", &number) == 1 && number == 6);
}

static void test_number_parsing(void)
{
    char *end = NULL;
    CHECK(shim_strtod("1e3xyz", &end) == 1000.0 && same_text(end, "xyz"));
    CHECK(shim_strtod("  -0x1p4", &end) == -16.0);
    CHECK(shim_strtod("0.1", &end) == 0.1);
    CHECK(shim_strtod("INF", &end) > 1e308);
    double not_a_number = shim_strtod("nan", &end);
    CHECK(not_a_number != not_a_number);
    CHECK(shim_strtol("0x7fffffffffffffff", &end, 0) == INT64_MAX);
    CHECK(shim_strtol("  -42abc", &end, 10) == -42 && same_text(end, "abc"));
    CHECK(shim_strtol("777", &end, 8) == 511);
    CHECK(shim_strtol("zz", &end, 36) == 1295);
}

static void test_time(void)
{
    int64_t when = 951782400 + 3600 * 5 + 61; /* 2000-02-29 05:01:01 UTC, a Tuesday */
    uint8_t tm[64] = {0};
    shim_gmtime_r(&when, tm);
    shim_strftime(g_buffer, sizeof(g_buffer), "%Y-%m-%d %H:%M:%S|%a %A|%b %B|%j|%e|%u %w|%y|%p %I", tm);
    CHECK_TEXT(g_buffer, "2000-02-29 05:01:01|Tue Tuesday|Feb February|060|29|2 2|00|AM 05");
    shim_strftime(g_buffer, sizeof(g_buffer), "%D %T %R %F %%", tm);
    CHECK_TEXT(g_buffer, "02/29/00 05:01:01 05:01 2000-02-29 %");
    CHECK(shim_strftime(g_buffer, 5, "%Y-%m-%d", tm) == 0);
}

typedef struct {
    int key;
    int order;
} Item;

static int SYSV compare_keys(const void *left, const void *right)
{
    return ((const Item *)left)->key - ((const Item *)right)->key;
}

static void test_qsort_is_stable(void)
{
    Item items[9] = {{3, 0}, {1, 1}, {3, 2}, {2, 3}, {1, 4}, {3, 5}, {2, 6}, {1, 7}, {2, 8}};
    shim_qsort(items, 9, sizeof(Item), compare_keys);
    const int expected[9] = {1, 4, 7, 3, 6, 8, 0, 2, 5};
    for (int i = 0; i < 9; i++) {
        CHECK(items[i].order == expected[i]);
    }
}

static void test_strings_and_memory(void)
{
    const char *text = "the quick brown fox";
    CHECK(shim_strlen(text) == 19);
    CHECK(shim_strchr(text, 'q') == text + 4);
    CHECK(shim_strrchr(text, 'o') == text + 17);
    CHECK(shim_strstr(text, "brown") == text + 10);
    CHECK(shim_strstr(text, "cat") == NULL);
    CHECK(shim_strcspn(text, "xq") == 4);
    CHECK(shim_strcasecmp("HeLLo", "hello") == 0);
    CHECK(shim_strcasecmp("a", "b") < 0);
    char overlap[16] = "abcdefgh";
    shim_memmove(overlap + 2, overlap, 6);
    CHECK_TEXT(overlap, "ababcdef");
    shim_memmove(overlap, overlap + 2, 6);
    CHECK(same_bytes(overlap, "abcdefef", 8));
    CHECK(shim_memcmp("abc", "abd", 3) < 0 && shim_memcmp("abc", "abc", 3) == 0);
}

/* Linux programs split absolute paths at '/', so a Windows drive must appear as the first component. */
static void test_working_directory_is_an_absolute_linux_path(void)
{
    char directory[512];
    CHECK(shim_getcwd(directory, sizeof(directory)) == directory);
    CHECK(directory[0] == '/' && directory[2] == ':' && directory[3] == '/');
    CHECK(shim_strchr(directory, '\\') == NULL);
}

#define EBUSY_CODE 16
#define PAGE_ALIGNMENT 4096

static void test_aligned_allocation(void)
{
    void *page = shim_aligned_alloc(PAGE_ALIGNMENT, 100);
    CHECK(page != NULL && ((uintptr_t)page & (PAGE_ALIGNMENT - 1)) == 0);
    ((char *)page)[99] = 'x';
    char *grown = shim_realloc(page, 10000);
    CHECK(grown != NULL && grown[99] == 'x');
    shim_free(grown);
    void *small = shim_aligned_alloc(16, 40);
    CHECK(small != NULL && ((uintptr_t)small & 15) == 0);
    shim_free(small);
    void *posix = NULL;
    CHECK(shim_posix_memalign(&posix, 64, 10) == 0 && ((uintptr_t)posix & 63) == 0);
    shim_free(posix);
    CHECK(shim_posix_memalign(&posix, 3, 10) != 0);
}

/* A zeroed object is an unlocked lock, as glibc's static initialiser makes it. */
static void test_rwlock(void)
{
    uint64_t lock[7] = {0};
    CHECK(shim_pthread_rwlock_rdlock(lock) == 0);
    CHECK(shim_pthread_rwlock_tryrdlock(lock) == 0);
    CHECK(shim_pthread_rwlock_trywrlock(lock) == EBUSY_CODE);
    CHECK(shim_pthread_rwlock_unlock(lock) == 0 && shim_pthread_rwlock_unlock(lock) == 0);
    CHECK(shim_pthread_rwlock_wrlock(lock) == 0);
    CHECK(shim_pthread_rwlock_tryrdlock(lock) == EBUSY_CODE);
    CHECK(shim_pthread_rwlock_unlock(lock) == 0);
    CHECK(shim_pthread_rwlock_trywrlock(lock) == 0 && shim_pthread_rwlock_unlock(lock) == 0);
}

static void test_dynamic_lookup(void)
{
    void *self = shim_dlopen(NULL, 0);
    CHECK(self != NULL && shim_dlsym(self, "strlen") != NULL);
    CHECK(shim_dlsym(self, "definitely_not_a_function") == NULL && shim_dlerror() != NULL && shim_dlerror() == NULL);
    CHECK(shim_dlopen("libc.so.6", 0) == self);
    CHECK(shim_dlopen("libmissing.so.1", 0) == NULL && shim_dlerror() != NULL);
}

int main(void)
{
    HMODULE module = LoadLibraryA("crossport_libc.dll");
    const char *missing = module != NULL ? load_all(module) : "crossport_libc.dll";
    if (missing != NULL) {
        report("cannot load %s\n", missing);
        return 2;
    }
    test_printf_integers();
    test_printf_text_and_pointers();
    test_printf_floats();
    test_scanf();
    test_number_parsing();
    test_time();
    test_qsort_is_stable();
    test_strings_and_memory();
    test_working_directory_is_an_absolute_linux_path();
    test_aligned_allocation();
    test_rwlock();
    test_dynamic_lookup();
    report("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
