/*
 * File: tests/corpus/libc_gaps/libc_gaps.c
 * Purpose: Compares with Linux the C library functions the compatibility survey found missing: the err family, group and password
 *          lookups, alarm and setitimer, tmpfile, mkdtemp, mkstemps, wcswidth, rpmatch, stpncpy, glob64 and the IPv6 constants
 * Author: Akilesh
 * Date: 2026-11-24
 */

#define _GNU_SOURCE
#include <err.h>
#include <error.h>
#include <errno.h>
#include <glob.h>
#include <grp.h>
#include <locale.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <wchar.h>

#define ERROR_FILE "libc_gaps_error.tmp"
#define ALARM_WAIT_MICROSECONDS 20000
#define ALARM_WAIT_LIMIT 200
#define TIMER_MICROSECONDS 30000

static volatile sig_atomic_t g_alarms;

static void on_alarm(int signal_number)
{
    (void)signal_number;
    g_alarms++;
}

static void show_error_file(const char *short_prefix, const char *full_prefix)
{
    char text[512] = "";
    FILE *file = fopen(ERROR_FILE, "rb");
    size_t got = file != NULL ? fread(text, 1, sizeof(text) - 1, file) : 0;
    text[got] = '\0';
    if (file != NULL) {
        fclose(file);
    }
    for (char *line = text; *line != '\0';) {
        char *newline = strchr(line, '\n');
        size_t length = newline != NULL ? (size_t)(newline - line) : strlen(line);
        int has_short = strncmp(line, short_prefix, strlen(short_prefix)) == 0;
        int has_full = strncmp(line, full_prefix, strlen(full_prefix)) == 0;
        int has_prefix = has_short || has_full;
        size_t skip = has_short ? strlen(short_prefix) : (has_full ? strlen(full_prefix) : 0);
        printf("line has the program name %d: [%.*s]\n", has_prefix, (int)(length - skip), line + skip);
        line += length + (newline != NULL ? 1 : 0);
    }
}

static void check_err_family(const char *program)
{
    fflush(stdout);
    if (freopen(ERROR_FILE, "w", stderr) == NULL) {
        printf("cannot redirect\n");
        return;
    }
    errno = ENOENT;
    warn("cannot open %s", "file.txt");
    warn(NULL);
    warnx("a plain warning %d", 7);
    warnx(NULL);
    errno = EACCES;
    warn("denied");
    error_at_line(0, ENOENT, "source.c", 42, "problem %s", "here");
    fflush(stderr);
    show_error_file("libc_gaps: ", program);
}

static void check_groups(void)
{
    struct group *by_id = getgrgid(getgid());
    struct group *by_name = by_id != NULL ? getgrnam(by_id->gr_name) : NULL;
    printf("group by id %d by name %d same id %d\n", by_id != NULL, by_name != NULL, by_name != NULL && by_name->gr_gid == getgid());
    int entries = 0;
    setgrent();
    while (getgrent() != NULL && entries < 1000) {
        entries++;
    }
    endgrent();
    printf("group database has entries %d\n", entries > 0);
    struct passwd *user = getpwuid(getuid());
    printf("passwd by id %d initgroups ok %d\n", user != NULL, user != NULL && initgroups(user->pw_name, getgid()) == 0);
    printf("login name known %d\n", getlogin() != NULL || getpwuid(getuid()) != NULL);
}

static void check_alarm(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_alarm;
    sigaction(SIGALRM, &action, NULL);
    alarm(1);
    unsigned left = alarm(0);
    printf("alarm remaining rounds up to 1: %u\n", left);
    struct itimerval timer = {{0, 0}, {0, TIMER_MICROSECONDS}};
    setitimer(ITIMER_REAL, &timer, NULL);
    for (int waited = 0; g_alarms == 0 && waited < ALARM_WAIT_LIMIT; waited++) {
        usleep(ALARM_WAIT_MICROSECONDS);
    }
    printf("timer signal arrived %d\n", g_alarms == 1);
}

static void check_temporary_files(void)
{
    FILE *file = tmpfile();
    char back[16] = "";
    if (file != NULL) {
        fputs("scratch", file);
        rewind(file);
        if (fgets(back, sizeof(back), file) == NULL) {
            back[0] = 0;
        }
        fclose(file);
    }
    printf("tmpfile round trip [%s]\n", back);
    char directory[] = "libc_gaps_dirXXXXXX";
    char *made = mkdtemp(directory);
    struct stat info;
    printf("mkdtemp ok %d is directory %d keeps prefix %d\n", made != NULL, made != NULL && stat(made, &info) == 0 && S_ISDIR(info.st_mode),
           strncmp(directory, "libc_gaps_dir", 13) == 0);
    if (made != NULL) {
        rmdir(made);
    }
    char name[] = "libc_gaps_fileXXXXXX.dat";
    int fd = mkstemps(name, 4);
    printf("mkstemps ok %d keeps suffix %d file exists under that name %d\n", fd >= 0, strcmp(name + strlen(name) - 4, ".dat") == 0,
           access(name, F_OK) == 0);
    if (fd >= 0) {
        close(fd);
        unlink(name);
    }
}

static void check_strings(void)
{
    wchar_t wide[] = {L'a', L'b', 0x4e2d, 0};
    printf("wcswidth %d\n", wcswidth(wide, 3));
    printf("rpmatch %d %d %d\n", rpmatch("yes"), rpmatch("No"), rpmatch("maybe"));
    char buffer[8];
    memset(buffer, 'x', sizeof(buffer));
    char *end = stpncpy(buffer, "abc", sizeof(buffer));
    printf("stpncpy end offset %d padded %d\n", (int)(end - buffer), buffer[7] == '\0');
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
    end = stpncpy(buffer, "abcdefghij", 4);
#pragma GCC diagnostic pop
    printf("stpncpy cut offset %d\n", (int)(end - buffer));
}

static void check_network_constants(void)
{
    int is_any_zero = 1;
    for (int i = 0; i < 16; i++) {
        is_any_zero = is_any_zero && in6addr_any.s6_addr[i] == 0;
    }
    printf("in6addr_any zero %d loopback last %d loopback first %d\n", is_any_zero, in6addr_loopback.s6_addr[15], in6addr_loopback.s6_addr[0]);
}

static void check_glob(void)
{
    glob64_t found;
    int status = glob64("libc_gaps_no_such_*", 0, NULL, &found);
    printf("glob64 no match status %d\n", status == GLOB_NOMATCH);
    if (status == 0) {
        globfree64(&found);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    setlocale(LC_ALL, "C.UTF-8");
    check_groups();
    check_alarm();
    check_temporary_files();
    check_strings();
    check_network_constants();
    check_glob();
    check_err_family(argv[0]);
    remove(ERROR_FILE);
    return 0;
}
