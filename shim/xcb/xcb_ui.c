/*
 * File: shim/xcb/xcb_ui.c
 * Purpose: The window thread: the one thread that owns every X window and runs their message loop
 * Author: Akilesh
 * Date: 2026-11-05
 *
 * A Win32 window belongs to the thread that created it, and only that thread's message loop delivers its
 * messages. X programs do not work that way: a toolkit creates windows on one thread and reads events on another,
 * and the thread that created a window may sit in a poll for as long as it likes. So the shim keeps its own thread
 * for windows. Creating, destroying and focusing a window run there (other calls, such as ShowWindow, may be made
 * from any thread and are carried out by the window thread on the caller's behalf), and the window procedure
 * turns messages into X events on that thread, whatever the program's threads are doing.
 *
 * The thread starts the first time a window is made. It is the shim's own thread, so it gets a thread-local block
 * like any other, but it never runs translated code.
 */

#include "tls.h"
#include "xcb_internal.h"

#define WM_UI_TASK (WM_APP + 1)

typedef struct {
    XcbUiTask task;
    void *argument;
    HANDLE done;
} UiRequest;

static DWORD g_thread_id;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static DWORD WINAPI window_thread(LPVOID parameter)
{
    HANDLE ready = (HANDLE)parameter;
    MSG message;
    /* Peeking creates the thread's message queue, so a task can be posted as soon as the creator is released. */
    PeekMessageW(&message, NULL, WM_USER, WM_USER, PM_NOREMOVE);
    tls_attach_thread();
    SetEvent(ready);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (message.hwnd == NULL && message.message == WM_UI_TASK) {
            UiRequest *request = (UiRequest *)message.lParam;
            request->task(request->argument);
            SetEvent(request->done);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

static BOOL CALLBACK start_thread(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    HANDLE ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE thread = CreateThread(NULL, 0, window_thread, ready, 0, &g_thread_id);
    if (thread == NULL || ready == NULL) {
        return FALSE;
    }
    WaitForSingleObject(ready, INFINITE);
    CloseHandle(ready);
    CloseHandle(thread);
    return TRUE;
}

int xcb_ui_is_window_thread(void)
{
    return g_thread_id != 0 && GetCurrentThreadId() == g_thread_id;
}

void xcb_ui_run(XcbUiTask task, void *argument)
{
    if (!InitOnceExecuteOnce(&g_once, start_thread, NULL, NULL) || xcb_ui_is_window_thread()) {
        task(argument);
        return;
    }
    UiRequest request = {.task = task, .argument = argument, .done = CreateEventW(NULL, FALSE, FALSE, NULL)};
    if (request.done == NULL || !PostThreadMessageW(g_thread_id, WM_UI_TASK, 0, (LPARAM)&request)) {
        task(argument);
    } else {
        WaitForSingleObject(request.done, INFINITE);
    }
    if (request.done != NULL) {
        CloseHandle(request.done);
    }
}

static void focus_task(void *argument)
{
    SetFocus((HWND)argument);
}

static void capture_task(void *argument)
{
    SetCapture((HWND)argument);
}

static void release_task(void *argument)
{
    (void)argument;
    ReleaseCapture();
}

void xcb_ui_set_focus(HWND window)
{
    xcb_ui_run(focus_task, window);
}

void xcb_ui_set_capture(HWND window)
{
    xcb_ui_run(capture_task, window);
}

void xcb_ui_release_capture(void)
{
    xcb_ui_run(release_task, NULL);
}
