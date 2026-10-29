/*
 * File: tests/corpus/mmap_files/mmap_files.c
 * Purpose: Exercise mmap on files and anonymous memory: shared and private views, offsets, msync, mremap, madvise
 * Author: Akilesh
 * Date: 2026-10-29
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE 4096
#define FILE_PAGES 20
#define FILE_NAME "mapped.bin"
#define ODD_OFFSET (PAGE * 3)

static int g_failures;

static void expect(int condition, const char *what)
{
    printf("%s: %s\n", what, condition ? "ok" : "FAILED");
    g_failures += !condition;
}

/* A file whose byte at position i is (i / PAGE) + 'A', so each page is recognisable. */
static int make_file(void)
{
    int fd = open(FILE_NAME, O_RDWR | O_CREAT | O_TRUNC, 0644);
    char page[PAGE];
    for (int i = 0; i < FILE_PAGES; i++) {
        memset(page, 'A' + i, sizeof(page));
        if (write(fd, page, sizeof(page)) != sizeof(page)) {
            return -1;
        }
    }
    return fd;
}

static void test_shared_mapping(int fd)
{
    char *map = mmap(NULL, PAGE * FILE_PAGES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    expect(map != MAP_FAILED, "shared file mapping");
    expect(map[0] == 'A' && map[PAGE * 5] == 'F' && map[PAGE * FILE_PAGES - 1] == 'T', "mapping shows the file contents");
    memcpy(map + 10, "WRITTEN", 7);
    expect(msync(map, PAGE * FILE_PAGES, MS_SYNC) == 0, "msync");
    expect(munmap(map, PAGE * FILE_PAGES) == 0, "munmap");
    char check[8] = {0};
    expect(pread(fd, check, 7, 10) == 7 && strcmp(check, "WRITTEN") == 0, "a shared write reaches the file");
}

static void test_private_mapping(int fd)
{
    char *map = mmap(NULL, PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, PAGE * 2);
    expect(map != MAP_FAILED && map[0] == 'C', "private mapping at an offset");
    map[0] = 'z';
    expect(map[0] == 'z', "a private write is visible to the mapping");
    char original;
    expect(pread(fd, &original, 1, PAGE * 2) == 1 && original == 'C', "a private write does not reach the file");
    munmap(map, PAGE);
}

static void test_unaligned_offset(int fd)
{
    char *map = mmap(NULL, PAGE * 2, PROT_READ, MAP_SHARED, fd, ODD_OFFSET);
    expect(map != MAP_FAILED, "mapping at a page offset that is not a multiple of 64 KiB");
    expect(map[0] == 'D' && map[PAGE] == 'E' && map[PAGE * 2 - 1] == 'E', "contents start at the requested offset");
    munmap(map, PAGE * 2);
}

static void test_anonymous(void)
{
    char *block = mmap(NULL, PAGE * 4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    expect(block != MAP_FAILED, "anonymous mapping");
    int is_zero = 1;
    for (int i = 0; i < PAGE * 4; i++) {
        is_zero &= block[i] == 0;
    }
    expect(is_zero, "anonymous memory starts zeroed");
    memset(block, 'x', PAGE * 4);
    char *grown = mremap(block, PAGE * 4, PAGE * 16, MREMAP_MAYMOVE);
    expect(grown != MAP_FAILED && grown[0] == 'x' && grown[PAGE * 4 - 1] == 'x', "mremap keeps the contents");
    grown[PAGE * 16 - 1] = 'y';
    expect(madvise(grown, PAGE * 4, MADV_DONTNEED) == 0 && grown[0] == 0, "MADV_DONTNEED zeroes private pages");
    expect(munmap(grown, PAGE * 16) == 0, "munmap of the grown block");
}

int main(void)
{
    int fd = make_file();
    expect(fd >= 0, "test file written");
    test_shared_mapping(fd);
    test_private_mapping(fd);
    test_unaligned_offset(fd);
    test_anonymous();
    close(fd);
    unlink(FILE_NAME);
    printf("failures: %d\n", g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
