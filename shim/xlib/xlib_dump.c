/*
 * File: shim/xlib/xlib_dump.c
 * Purpose: Debug hook that writes the pixels of every mapped window to PPM files, so a test can check a toolkit really drew
 * Author: Akilesh
 * Date: 2026-11-06
 *
 * Set CROSSPORT_DUMP_WINDOWS to an existing directory. A program that is stopped by a timeout never runs an exit
 * handler, so the dump does not wait for one: the first paint starts a thread that, while windows keep painting,
 * rewrites <directory>/window-<id>.ppm about twice a second. A file is written under a temporary name and moved
 * into place, so a reader never sees half of one. Without the variable nothing here runs.
 */

#include <string.h>

#include "xlib_internal.h"

#define DUMP_INTERVAL_MS 500
#define MAX_DUMPED_WINDOWS 32
#define MAX_DUMP_PIXELS (16 * 1024 * 1024)
#define MAX_PATH_BYTES 520
#define HEADER_BYTES 48
#define BYTES_PER_PIXEL 3

typedef struct {
    XlId id;
    size_t size;
    uint8_t *data;
} DumpedWindow;

static char g_directory[MAX_PATH_BYTES];
static LONG g_state = -1;
static volatile LONG g_is_dirty;
static volatile LONG g_is_running;

static int dump_enabled(void)
{
    if (g_state < 0) {
        DWORD length = GetEnvironmentVariableA("CROSSPORT_DUMP_WINDOWS", g_directory, sizeof(g_directory));
        InterlockedExchange(&g_state, length > 0 && length < sizeof(g_directory) ? 1 : 0);
    }
    return g_state == 1;
}

/* Converts a window's 0x00RRGGBB buffer to a P6 file image. Call with the lock held; returns 0 when it cannot. */
static int snapshot_window(const XlDrawable *drawable, DumpedWindow *out)
{
    size_t count = (size_t)drawable->width * (size_t)drawable->height;
    if (count == 0 || count > MAX_DUMP_PIXELS) {
        return 0;
    }
    uint8_t *data = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, HEADER_BYTES + count * BYTES_PER_PIXEL);
    if (data == NULL) {
        return 0;
    }
    int header = wsprintfA((char *)data, "P6\n%d %d\n255\n", (int)drawable->width, (int)drawable->height);
    uint8_t *at = data + header;
    for (size_t i = 0; i < count; i++) {
        uint32_t pixel = drawable->pixels[i];
        *at++ = (uint8_t)(pixel >> 16);
        *at++ = (uint8_t)(pixel >> 8);
        *at++ = (uint8_t)pixel;
    }
    out->id = drawable->id;
    out->size = (size_t)(at - data);
    out->data = data;
    return 1;
}

static size_t snapshot_all(DumpedWindow *windows)
{
    size_t count = 0;
    xl_lock();
    for (size_t bucket = 0; bucket < XL_BUCKET_COUNT && count < MAX_DUMPED_WINDOWS; bucket++) {
        const XlDrawable *drawable = xl_drawable_bucket(bucket);
        for (; drawable != NULL && count < MAX_DUMPED_WINDOWS; drawable = drawable->next) {
            if (drawable->is_window && drawable->is_mapped && snapshot_window(drawable, &windows[count])) {
                count++;
            }
        }
    }
    xl_unlock();
    return count;
}

static void write_window(const DumpedWindow *window)
{
    char final_path[MAX_PATH_BYTES + 32];
    char temporary_path[MAX_PATH_BYTES + 40];
    wsprintfA(final_path, "%s\\window-%x.ppm", g_directory, (unsigned)window->id);
    wsprintfA(temporary_path, "%s.tmp", final_path);
    HANDLE file = CreateFileA(temporary_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    BOOL is_written = WriteFile(file, window->data, (DWORD)window->size, &written, NULL);
    CloseHandle(file);
    if (is_written && written == window->size) {
        MoveFileExA(temporary_path, final_path, MOVEFILE_REPLACE_EXISTING);
    }
}

static void dump_all(void)
{
    DumpedWindow windows[MAX_DUMPED_WINDOWS];
    size_t count = snapshot_all(windows);
    for (size_t i = 0; i < count; i++) {
        write_window(&windows[i]);
        HeapFree(GetProcessHeap(), 0, windows[i].data);
    }
}

static DWORD WINAPI dump_thread(LPVOID unused)
{
    (void)unused;
    while (dump_enabled()) {
        Sleep(DUMP_INTERVAL_MS);
        if (InterlockedExchange(&g_is_dirty, 0)) {
            dump_all();
        }
    }
    return 0;
}

void xl_dump_notify_paint(void)
{
    if (!dump_enabled()) {
        return;
    }
    InterlockedExchange(&g_is_dirty, 1);
    if (InterlockedCompareExchange(&g_is_running, 1, 0) == 0) {
        HANDLE thread = CreateThread(NULL, 0, dump_thread, NULL, 0, NULL);
        if (thread != NULL) {
            CloseHandle(thread);
        }
    }
}
