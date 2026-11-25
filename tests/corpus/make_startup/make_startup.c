/*
 * File: tests/corpus/make_startup/make_startup.c
 * Purpose: Repeats the descriptor and directory calls GNU make makes at start-up (O_APPEND on stdout and stderr, an opened directory, a
 *          makefile opened and marked close-on-exec) and prints each result, so Linux and the translation can be compared
 * Author: Akilesh
 * Date: 2026-11-24
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAKEFILE "make_startup.mk"

static void set_append(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    int result = flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_APPEND);
    printf("fd %d: get %s set %s\n", fd, flags < 0 ? strerror(errno) : "ok", result < 0 ? strerror(errno) : "ok");
}

int main(void)
{
    FILE *file = fopen(MAKEFILE, "w");
    if (file != NULL) {
        fputs("all:\n", file);
        fclose(file);
    }
    set_append(1);
    set_append(2);
    printf("getfd on stdout: %s\n", fcntl(1, F_GETFD) < 0 ? strerror(errno) : "ok");
    DIR *directory = opendir(".");
    printf("opendir: %s\n", directory == NULL ? strerror(errno) : "ok");
    int seen = 0;
    for (struct dirent *entry = directory != NULL ? readdir(directory) : NULL; entry != NULL; entry = readdir(directory)) {
        seen += strcmp(entry->d_name, MAKEFILE) == 0;
    }
    printf("directory lists the makefile %d\n", seen);
    errno = 0;
    FILE *makefile = fopen(MAKEFILE, "r");
    printf("fopen: %s\n", makefile == NULL ? strerror(errno) : "ok");
    printf("a successful open leaves errno alone %d\n", errno == 0);
    if (makefile != NULL) {
        int fd = fileno(makefile);
        printf("cloexec: %s\n", fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 ? strerror(errno) : "ok");
        fclose(makefile);
    }
    if (directory != NULL) {
        rewinddir(directory);
        int again = 0;
        while (readdir(directory) != NULL) {
            again++;
        }
        printf("after rewinddir the listing has entries %d\n", again > 0);
        closedir(directory);
    }
    remove(MAKEFILE);
    return 0;
}
