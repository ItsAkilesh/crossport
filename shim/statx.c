/*
 * File: shim/statx.c
 * Purpose: statx(2), the extended stat that current coreutils (stat, ls, du, cp) call first
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Found by the second application sweep: Ubuntu's stat failed with "cannot statx: Function not implemented". The answer is built from
 * fstatat; the birth time is not reported (STATX_BTIME is left out of the returned mask), as on file systems that lack it.
 */

#include <string.h>

#include "libc_abi.h"

#define AT_EMPTY_PATH 0x1000
#define AT_SYMLINK_NOFOLLOW 0x100
#define STATX_BASIC_STATS 0x7ffu
#define STATX_BLOCK_BYTES 512

/* The Linux x86-64 struct stat (see stat.c). */
typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t pad;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime;
    int64_t st_atime_nsec;
    int64_t st_mtime;
    int64_t st_mtime_nsec;
    int64_t st_ctime;
    int64_t st_ctime_nsec;
    int64_t unused[3];
} LinuxStat;

typedef struct {
    int64_t seconds;
    uint32_t nanoseconds;
    int32_t padding;
} StatxTime;

/* struct statx: 256 bytes. */
typedef struct {
    uint32_t mask;
    uint32_t block_size;
    uint64_t attributes;
    uint32_t link_count;
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    uint16_t padding;
    uint64_t inode;
    uint64_t size;
    uint64_t blocks;
    uint64_t attributes_mask;
    StatxTime access_time;
    StatxTime birth_time;
    StatxTime change_time;
    StatxTime modify_time;
    uint32_t rdev_major;
    uint32_t rdev_minor;
    uint32_t dev_major;
    uint32_t dev_minor;
    uint64_t mount_id;
    uint32_t direct_io_memory_alignment;
    uint32_t direct_io_offset_alignment;
    uint64_t spare[12];
} Statx;

_Static_assert(sizeof(Statx) == 256, "struct statx must match the Linux layout");

LIBC_FN int sysv_fstatat(int dirfd, const char *path, void *stat_buffer, int flags);
LIBC_FN int sysv_fstat(int fd, void *stat_buffer);

static StatxTime time_of(int64_t seconds, int64_t nanoseconds)
{
    return (StatxTime){seconds, (uint32_t)nanoseconds, 0};
}

static unsigned device_major(uint64_t device)
{
    return (unsigned)((device >> 8) & 0xfff);
}

static unsigned device_minor(uint64_t device)
{
    return (unsigned)(device & 0xff);
}

LIBC_FN int sysv_statx(int dirfd, const char *path, int flags, unsigned mask, Statx *out)
{
    LinuxStat stat;
    (void)mask;
    int result;
    if ((flags & AT_EMPTY_PATH) && path != NULL && path[0] == '\0') {
        result = sysv_fstat(dirfd, &stat);
    } else {
        result = sysv_fstatat(dirfd, path, &stat, flags & AT_SYMLINK_NOFOLLOW);
    }
    if (result != 0) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->mask = STATX_BASIC_STATS;
    out->block_size = (uint32_t)stat.st_blksize;
    out->link_count = (uint32_t)stat.st_nlink;
    out->uid = stat.st_uid;
    out->gid = stat.st_gid;
    out->mode = (uint16_t)stat.st_mode;
    out->inode = stat.st_ino;
    out->size = (uint64_t)stat.st_size;
    out->blocks = (uint64_t)stat.st_blocks;
    out->access_time = time_of(stat.st_atime, stat.st_atime_nsec);
    out->change_time = time_of(stat.st_ctime, stat.st_ctime_nsec);
    out->modify_time = time_of(stat.st_mtime, stat.st_mtime_nsec);
    out->rdev_major = device_major(stat.st_rdev);
    out->rdev_minor = device_minor(stat.st_rdev);
    out->dev_major = device_major(stat.st_dev);
    out->dev_minor = device_minor(stat.st_dev);
    return 0;
}
