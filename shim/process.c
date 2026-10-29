/*
 * File: shim/process.c
 * Purpose: Start child processes on Windows: command lines, environment blocks, standard handle redirection and the child table
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * fork has no efficient Windows counterpart (see docs/DESIGN.md), so programs start children with the
 * spawn family, which maps well onto CreateProcess. Only descriptors 0, 1 and 2 can be given to a child.
 */

#include <string.h>

#include "process.h"

#define MAX_CHILDREN 256
#define SIGNAL_SEGV 11
#define SIGNAL_INT 2
#define STATUS_EXIT_SHIFT 8
#define STATUS_CODE_MASK 0xff
#define EXIT_KILLED_BASE 128

typedef struct {
    int pid;
    HANDLE process;
    int killed_by; /* signal number when shim_process_kill ended it, else 0 */
} Child;

static Child g_children[MAX_CHILDREN];
static SRWLOCK g_lock = SRWLOCK_INIT;

static int remember(int pid, HANDLE process)
{
    int is_stored = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < MAX_CHILDREN && !is_stored; i++) {
        if (g_children[i].pid == 0) {
            g_children[i] = (Child){.pid = pid, .process = process};
            is_stored = 1;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return is_stored;
}

static void shim_process_mark_killed(int pid, int signal_number)
{
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < MAX_CHILDREN; i++) {
        if (g_children[i].pid == pid) {
            g_children[i].killed_by = signal_number;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

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

/* An inheritable copy of what `fd` refers to, or of the NUL device for -1. Returns INVALID_HANDLE_VALUE on failure. */
static HANDLE inheritable(int fd, int is_input)
{
    SECURITY_ATTRIBUTES inherit = {.nLength = sizeof(inherit), .bInheritHandle = TRUE};
    if (fd < 0) {
        return CreateFileW(L"NUL", is_input ? GENERIC_READ : GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    HANDLE source = shim_fd_handle(fd);
    HANDLE copy = INVALID_HANDLE_VALUE;
    if (source == INVALID_HANDLE_VALUE ||
        !DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &copy, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        return INVALID_HANDLE_VALUE;
    }
    return copy;
}

static int launch(const wchar_t *application, wchar_t *command_line, const wchar_t *environment, HANDLE handles[3],
                  PROCESS_INFORMATION *info)
{
    STARTUPINFOEXW startup = {.StartupInfo = {.cb = sizeof(startup), .dwFlags = STARTF_USESTDHANDLES,
                                              .hStdInput = handles[0], .hStdOutput = handles[1], .hStdError = handles[2]}};
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &size);
    startup.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, size);
    if (startup.lpAttributeList == NULL || !InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size) ||
        !UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                   3 * sizeof(HANDLE), NULL, NULL)) {
        HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
        return shim_fail_from_win();
    }
    startup.StartupInfo.cb = sizeof(startup);
    DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
    BOOL ok = CreateProcessW(application, command_line, NULL, NULL, TRUE, flags, (LPVOID)environment, NULL,
                             &startup.StartupInfo, info);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
    return ok ? 0 : shim_fail_from_win();
}

int shim_process_start(const wchar_t *application, wchar_t *command_line, const wchar_t *environment,
                       const ChildStreams *streams)
{
    int numbers[3] = {streams->input, streams->output, streams->error};
    HANDLE handles[3];
    for (int i = 0; i < 3; i++) {
        handles[i] = inheritable(numbers[i], i == 0);
        if (handles[i] == INVALID_HANDLE_VALUE) {
            for (int j = 0; j < i; j++) {
                CloseHandle(handles[j]);
            }
            return shim_fail(LINUX_EBADF);
        }
    }
    PROCESS_INFORMATION info;
    int status = launch(application, command_line, environment, handles, &info);
    for (int i = 0; i < 3; i++) {
        CloseHandle(handles[i]);
    }
    if (status != 0) {
        return -1;
    }
    CloseHandle(info.hThread);
    if (!remember((int)info.dwProcessId, info.hProcess)) {
        CloseHandle(info.hProcess);
        return shim_fail(LINUX_EAGAIN);
    }
    return (int)info.dwProcessId;
}

/* The wait status of an exited child: exit code in the high byte, or the signal that ended it. */
static int encode_status(DWORD exit_code, int killed_by)
{
    if (killed_by != 0) {
        return killed_by;
    }
    if (exit_code == STATUS_ACCESS_VIOLATION) {
        return SIGNAL_SEGV;
    }
    if (exit_code == (DWORD)STATUS_CONTROL_C_EXIT) {
        return SIGNAL_INT;
    }
    return (int)((exit_code & STATUS_CODE_MASK) << STATUS_EXIT_SHIFT);
}

static int finish(Child *child, int *status)
{
    DWORD code = 0;
    GetExitCodeProcess(child->process, &code);
    int pid = child->pid;
    if (status != NULL) {
        *status = encode_status(code, child->killed_by);
    }
    CloseHandle(child->process);
    memset(child, 0, sizeof(*child));
    return pid;
}

int shim_process_wait(int pid, int *status, int no_wait)
{
    for (;;) {
        HANDLE handles[MAX_CHILDREN];
        size_t slots[MAX_CHILDREN];
        size_t count = 0;
        int result = -1;
        AcquireSRWLockExclusive(&g_lock);
        for (size_t i = 0; i < MAX_CHILDREN && count < MAXIMUM_WAIT_OBJECTS; i++) {
            if (g_children[i].pid != 0 && (pid == -1 || g_children[i].pid == pid)) {
                handles[count] = g_children[i].process;
                slots[count++] = i;
            }
        }
        for (size_t i = 0; i < count && result < 0; i++) {
            if (WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0) {
                result = finish(&g_children[slots[i]], status);
            }
        }
        ReleaseSRWLockExclusive(&g_lock);
        if (count == 0) {
            return shim_fail(LINUX_ECHILD);
        }
        if (result >= 0) {
            return result;
        }
        if (no_wait) {
            return 0;
        }
        WaitForMultipleObjects((DWORD)count, handles, FALSE, INFINITE);
    }
}

/* Ends a process the way a fatal signal would; a child of ours then reports that signal to waitpid. */
int shim_process_terminate(int pid, int signal_number)
{
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (process == NULL) {
        return shim_fail(GetLastError() == ERROR_INVALID_PARAMETER ? LINUX_ESRCH : LINUX_EPERM);
    }
    shim_process_mark_killed(pid, signal_number);
    BOOL ok = TerminateProcess(process, (UINT)(EXIT_KILLED_BASE + signal_number));
    CloseHandle(process);
    return ok ? 0 : shim_fail_from_win();
}
