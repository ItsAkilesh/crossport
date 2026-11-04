/*
 * File: shim/xlib/xlib_clipboard.c
 * Purpose: Read and write the Windows clipboard as UTF-8 text, and tell whether its content came from this process
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * X programs use UTF-8 with LF line ends; Windows text is UTF-16 with CRLF, so both are converted. Windows hands out
 * a sequence number that changes whenever the clipboard does, which is how the shim notices that another program
 * has replaced what a translated program published. Only plain text is bridged.
 */

#include <string.h>

#include "xlib_internal.h"

#define OPEN_ATTEMPTS 20
#define OPEN_RETRY_MILLISECONDS 5
#define MAX_TEXT_BYTES (16 * 1024 * 1024)

static DWORD g_published_sequence;
static int g_has_published;
static INIT_ONCE g_owner_once = INIT_ONCE_STATIC_INIT;
static HWND g_owner_window;

static BOOL CALLBACK create_owner(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    g_owner_window = CreateWindowExA(0, "STATIC", "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
    return TRUE;
}

/* A hidden message-only window that owns what the shim publishes; Windows requires a window to own the clipboard. */
static HWND owner_window(void)
{
    InitOnceExecuteOnce(&g_owner_once, create_owner, NULL, NULL);
    return g_owner_window;
}

/* Another program may hold the clipboard for a moment, so opening it is retried briefly. */
static int open_clipboard(HWND owner)
{
    for (int attempt = 0; attempt < OPEN_ATTEMPTS; attempt++) {
        if (OpenClipboard(owner)) {
            return 1;
        }
        Sleep(OPEN_RETRY_MILLISECONDS);
    }
    return 0;
}

/* The text with every CRLF turned into LF, in place; returns the new length. */
static size_t collapse_line_ends(char *text, size_t length)
{
    size_t kept = 0;
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\r' && i + 1 < length && text[i + 1] == '\n') {
            continue;
        }
        text[kept++] = text[i];
    }
    return kept;
}

uint8_t *xl_clipboard_read(size_t *length)
{
    *length = 0;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !open_clipboard(NULL)) {
        return NULL;
    }
    char *result = NULL;
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    const wchar_t *wide = handle != NULL ? (const wchar_t *)GlobalLock(handle) : NULL;
    if (wide != NULL) {
        int wide_length = (int)(GlobalSize(handle) / sizeof(wchar_t));
        int units = 0;
        while (units < wide_length && wide[units] != L'\0') {
            units++;
        }
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, units, NULL, 0, NULL, NULL);
        result = bytes > 0 && bytes <= MAX_TEXT_BYTES ? (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)bytes + 1) : NULL;
        if (result != NULL) {
            WideCharToMultiByte(CP_UTF8, 0, wide, units, result, bytes, NULL, NULL);
            *length = collapse_line_ends(result, (size_t)bytes);
        }
        GlobalUnlock(handle);
    }
    CloseClipboard();
    return (uint8_t *)result;
}

/* The UTF-16 form of UTF-8 text with every lone LF widened to CRLF, in a movable global block; NULL when it cannot be made. */
static HGLOBAL windows_text(const uint8_t *text, size_t length)
{
    int units = MultiByteToWideChar(CP_UTF8, 0, (const char *)text, (int)length, NULL, 0);
    wchar_t *wide = (units > 0 && length <= MAX_TEXT_BYTES) ? (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (size_t)units * sizeof(wchar_t)) : NULL;
    if (wide == NULL) {
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, (const char *)text, (int)length, wide, units);
    size_t extra = 0;
    for (int i = 0; i < units; i++) {
        extra += (wide[i] == L'\n' && (i == 0 || wide[i - 1] != L'\r')) ? 1 : 0;
    }
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, ((size_t)units + extra + 1) * sizeof(wchar_t));
    wchar_t *target = block != NULL ? (wchar_t *)GlobalLock(block) : NULL;
    if (target != NULL) {
        size_t next = 0;
        for (int i = 0; i < units; i++) {
            if (wide[i] == L'\n' && (i == 0 || wide[i - 1] != L'\r')) {
                target[next++] = L'\r';
            }
            target[next++] = wide[i];
        }
        target[next] = L'\0';
        GlobalUnlock(block);
    }
    HeapFree(GetProcessHeap(), 0, wide);
    return target != NULL ? block : NULL;
}

int xl_clipboard_publish(const uint8_t *text, size_t length)
{
    HGLOBAL block = windows_text(text, length);
    if (block == NULL) {
        return 0;
    }
    int is_published = 0;
    if (open_clipboard(owner_window())) {
        EmptyClipboard();
        is_published = SetClipboardData(CF_UNICODETEXT, block) != NULL;
        CloseClipboard();
    }
    if (is_published) {
        g_published_sequence = GetClipboardSequenceNumber();
        g_has_published = 1;
    } else {
        GlobalFree(block);
    }
    return is_published;
}

int xl_clipboard_has_foreign_text(void)
{
    int is_ours = g_has_published && GetClipboardSequenceNumber() == g_published_sequence;
    return !is_ours && IsClipboardFormatAvailable(CF_UNICODETEXT);
}

int xl_clipboard_was_replaced(void)
{
    if (g_has_published && GetClipboardSequenceNumber() != g_published_sequence) {
        g_has_published = 0;
        return 1;
    }
    return 0;
}
