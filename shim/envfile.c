/*
 * File: shim/envfile.c
 * Purpose: Set HOME, USER and LOGNAME if missing, and apply the bundle's crossport.env (what a launcher script would have set)
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * An AppImage's AppRun script usually exports variables (APPDIR, plugin paths) before running the program.
 * Crossport does not run scripts, so `crossport convert` writes the useful ones to crossport.env beside
 * the executable. Each line is NAME=value; blank lines and lines starting with # are skipped, and
 * ${BUNDLE} stands for the bundle directory in Linux path form. Variables already in the user's
 * environment are not overridden.
 */

#include <string.h>

#include "env_internal.h"
#include "fd.h"

#define ENV_FILE_NAME L"crossport.env"
#define MAX_ENV_FILE_BYTES (64 * 1024)
#define MAX_LINE_BYTES 4096
#define BUNDLE_TOKEN "${BUNDLE}"

/* The directory of the running executable as a wide Windows path, without a trailing separator. */
static int bundle_directory(wchar_t *out, size_t capacity)
{
    DWORD length = GetModuleFileNameW(NULL, out, (DWORD)capacity);
    if (length == 0 || length >= capacity) {
        return 0;
    }
    wchar_t *last = wcsrchr(out, L'\\');
    if (last == NULL) {
        return 0;
    }
    *last = L'\0';
    return 1;
}

static char *read_file(const wchar_t *path, DWORD *size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    char *text = (char *)HeapAlloc(GetProcessHeap(), 0, MAX_ENV_FILE_BYTES + 1);
    DWORD got = 0;
    if (text == NULL || !ReadFile(file, text, MAX_ENV_FILE_BYTES, &got, NULL)) {
        HeapFree(GetProcessHeap(), 0, text);
        text = NULL;
    }
    CloseHandle(file);
    if (text != NULL) {
        text[got] = '\0';
        *size = got;
    }
    return text;
}

/* Copies `value` into `out` with every ${BUNDLE} replaced by `bundle`; false when it does not fit. */
static int expand(const char *value, const char *bundle, char *out, size_t capacity)
{
    size_t length = 0;
    size_t bundle_length = strlen(bundle);
    size_t token_length = strlen(BUNDLE_TOKEN);
    while (*value != '\0') {
        int is_token = strncmp(value, BUNDLE_TOKEN, token_length) == 0;
        size_t add = is_token ? bundle_length : 1;
        if (length + add + 1 > capacity) {
            return 0;
        }
        if (is_token) {
            memcpy(out + length, bundle, bundle_length);
            value += token_length;
        } else {
            out[length] = *value++;
        }
        length += add;
    }
    out[length] = '\0';
    return 1;
}

static void apply_line(char *line, const char *bundle)
{
    char value[MAX_LINE_BYTES];
    char *equals = strchr(line, '=');
    size_t end = strlen(line);
    while (end > 0 && (line[end - 1] == '\r' || line[end - 1] == ' ')) {
        line[--end] = '\0';
    }
    if (line[0] == '#' || equals == NULL || equals == line) {
        return;
    }
    *equals = '\0';
    if (expand(equals + 1, bundle, value, sizeof(value))) {
        shim_env_set(line, value, 0);
    }
}

/* The variables Linux programs expect to exist and Windows does not set: HOME, USER and LOGNAME. */
static void apply_defaults(void)
{
    wchar_t wide[SHIM_PATH_MAX];
    char text[SHIM_PATH_MAX];
    if (GetEnvironmentVariableW(L"USERPROFILE", wide, SHIM_PATH_MAX) > 0 && shim_wide_to_linux_path(wide, text, sizeof(text)) == 0) {
        shim_env_set("HOME", text, 0);
    }
    if (GetEnvironmentVariableW(L"USERNAME", wide, SHIM_PATH_MAX) > 0 && shim_wide_to_utf8(wide, text, sizeof(text)) == 0) {
        shim_env_set("USER", text, 0);
        shim_env_set("LOGNAME", text, 0);
    }
}

void shim_apply_env_file(void)
{
    apply_defaults();
    wchar_t directory[SHIM_PATH_MAX];
    wchar_t path[SHIM_PATH_MAX];
    char bundle[SHIM_PATH_MAX];
    DWORD size = 0;
    if (!bundle_directory(directory, SHIM_PATH_MAX) || wcslen(directory) + wcslen(ENV_FILE_NAME) + 2 >= SHIM_PATH_MAX ||
        shim_wide_to_linux_path(directory, bundle, sizeof(bundle)) != 0) {
        return;
    }
    wcscpy(path, directory);
    wcscat(path, L"\\");
    wcscat(path, ENV_FILE_NAME);
    char *text = read_file(path, &size);
    if (text == NULL) {
        return;
    }
    for (char *line = strtok(text, "\n"); line != NULL; line = strtok(NULL, "\n")) {
        apply_line(line, bundle);
    }
    HeapFree(GetProcessHeap(), 0, text);
}
