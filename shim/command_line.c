/*
 * File: shim/command_line.c
 * Purpose: Windows command lines and environment blocks built from Linux argument and environment vectors
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "process.h"

static wchar_t *widen(const char *text)
{
    int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *wide = count > 0 ? (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (size_t)count * sizeof(wchar_t)) : NULL;
    if (wide != NULL) {
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, count);
    }
    return wide;
}

/* Appends one argument, quoted so CommandLineToArgvW reads back exactly the same text. */
static size_t quote_into(wchar_t *out, const wchar_t *argument)
{
    size_t length = 0;
    int needs_quotes = argument[0] == L'\0' || wcspbrk(argument, L" \t\n\v\"") != NULL;
    if (!needs_quotes) {
        wcscpy(out, argument);
        return wcslen(argument);
    }
    out[length++] = L'"';
    for (const wchar_t *at = argument;; at++) {
        size_t backslashes = 0;
        while (*at == L'\\') {
            backslashes++;
            at++;
        }
        if (*at == L'\0') {
            for (size_t i = 0; i < backslashes * 2; i++) {
                out[length++] = L'\\';
            }
            break;
        }
        size_t repeat = *at == L'"' ? backslashes * 2 + 1 : backslashes;
        for (size_t i = 0; i < repeat; i++) {
            out[length++] = L'\\';
        }
        out[length++] = *at;
    }
    out[length++] = L'"';
    out[length] = L'\0';
    return length;
}

wchar_t *shim_command_line(char *const argv[])
{
    size_t capacity = 1;
    for (size_t i = 0; argv[i] != NULL; i++) {
        capacity += 4 * strlen(argv[i]) + 3;
    }
    wchar_t *line = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(wchar_t));
    if (line == NULL) {
        shim_set_errno(LINUX_ENOMEM);
        return NULL;
    }
    size_t length = 0;
    line[0] = L'\0';
    for (size_t i = 0; argv[i] != NULL; i++) {
        wchar_t *wide = widen(argv[i]);
        if (wide == NULL) {
            HeapFree(GetProcessHeap(), 0, line);
            shim_set_errno(LINUX_EINVAL);
            return NULL;
        }
        if (i > 0) {
            line[length++] = L' ';
        }
        length += quote_into(line + length, wide);
        HeapFree(GetProcessHeap(), 0, wide);
    }
    return line;
}

/* Variables Windows programs cannot start without (DLL search, system paths), kept even when the caller omits them. */
static const wchar_t *const ESSENTIAL_VARIABLES[] = {L"SystemRoot", L"windir"};
#define ESSENTIAL_VALUE_MAX 512

static int names_variable(char *const envp[], const wchar_t *name)
{
    char narrow[64];
    WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof(narrow), NULL, NULL);
    size_t length = strlen(narrow);
    for (size_t i = 0; envp[i] != NULL; i++) {
        if (_strnicmp(envp[i], narrow, length) == 0 && envp[i][length] == '=') {
            return 1;
        }
    }
    return 0;
}

static size_t append_essentials(wchar_t *block, size_t length, size_t capacity, char *const envp[])
{
    for (size_t i = 0; i < sizeof(ESSENTIAL_VARIABLES) / sizeof(ESSENTIAL_VARIABLES[0]); i++) {
        wchar_t value[ESSENTIAL_VALUE_MAX];
        size_t name_length = wcslen(ESSENTIAL_VARIABLES[i]);
        DWORD value_length = GetEnvironmentVariableW(ESSENTIAL_VARIABLES[i], value, ESSENTIAL_VALUE_MAX);
        int is_room = value_length > 0 && value_length < ESSENTIAL_VALUE_MAX && length + name_length + value_length + 2 < capacity;
        if (is_room && !names_variable(envp, ESSENTIAL_VARIABLES[i])) {
            wcscpy(block + length, ESSENTIAL_VARIABLES[i]);
            block[length + name_length] = L'=';
            wcscpy(block + length + name_length + 1, value);
            length += name_length + 1 + value_length + 1;
        }
    }
    return length;
}

wchar_t *shim_environment_block(char *const envp[])
{
    if (envp == NULL) {
        return NULL;
    }
    size_t capacity = 2 + sizeof(ESSENTIAL_VARIABLES) / sizeof(ESSENTIAL_VARIABLES[0]) * (ESSENTIAL_VALUE_MAX + 64);
    for (size_t i = 0; envp[i] != NULL; i++) {
        capacity += strlen(envp[i]) + 1;
    }
    wchar_t *block = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(wchar_t));
    size_t length = 0;
    for (size_t i = 0; block != NULL && envp[i] != NULL; i++) {
        int written = MultiByteToWideChar(CP_UTF8, 0, envp[i], -1, block + length, (int)(capacity - length));
        length += written > 0 ? (size_t)written : 0;
    }
    if (block != NULL) {
        length = append_essentials(block, length, capacity, envp);
        block[length] = L'\0';
        block[length + (length == 0)] = L'\0';
    }
    return block;
}
