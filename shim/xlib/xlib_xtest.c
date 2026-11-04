/*
 * File: shim/xlib/xlib_xtest.c
 * Purpose: libXtst on Windows input: a program injects keys, pointer motion and buttons into its own display
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * XTest is how test and automation programs drive an X server. Here each call becomes real Windows input
 * (SendInput and SetCursorPos), so it travels the same path as a user's: the window procedure turns it into
 * X events, with modifier and button state from the system. Windows delivers keys only to the foreground
 * window, so the first call brings one of the program's own top-level windows forward, using the documented
 * way for a process to do that without a user gesture (attaching to the foreground thread's input).
 * Coordinates are root coordinates, which are screen coordinates of the primary monitor.
 */

#include "xlib_internal.h"

#define XTEST_MAJOR 2
#define XTEST_MINOR 2
#define POINTER_SETTLE_MILLISECONDS 500
#define BUTTON_LEFT 1
#define BUTTON_MIDDLE 2
#define BUTTON_RIGHT 3
#define BUTTON_WHEEL_UP 4
#define BUTTON_WHEEL_DOWN 5

static BOOL CALLBACK pick_window(HWND window, LPARAM parameter)
{
    if (GetParent(window) == NULL && IsWindowVisible(window) && xcb_window_id(window) != 0) {
        *(HWND *)parameter = window;
        return FALSE;
    }
    return TRUE;
}

/* Makes sure the foreground window is one of ours, so injected input reaches the program. */
static void focus_own_window(void)
{
    HWND foreground = GetForegroundWindow();
    HWND target = NULL;
    if (foreground != NULL && xcb_window_id(foreground) != 0) {
        return;
    }
    EnumThreadWindows(GetCurrentThreadId(), pick_window, (LPARAM)&target);
    if (target == NULL) {
        return;
    }
    DWORD own_thread = GetCurrentThreadId();
    DWORD foreground_thread = foreground != NULL ? GetWindowThreadProcessId(foreground, NULL) : 0;
    int is_attached = foreground_thread != 0 && foreground_thread != own_thread &&
                      AttachThreadInput(own_thread, foreground_thread, TRUE);
    BringWindowToTop(target);
    SetForegroundWindow(target);
    if (is_attached) {
        AttachThreadInput(own_thread, foreground_thread, FALSE);
    }
}

static void delay_for(unsigned long milliseconds)
{
    if (milliseconds > 0) {
        Sleep((DWORD)milliseconds);
    }
}

static int is_over_own_window(int x, int y)
{
    POINT point = {x, y};
    HWND window = WindowFromPoint(point);
    return window != NULL && xcb_window_id(window) != 0;
}

/*
 * Windows delivers a pointer move as a low-priority message, after any queued key and button input, while an X
 * server handles XTest requests strictly in order. So the program's messages are dispatched here until its window
 * has reported the move, which keeps the next injected event after it. This only waits when the pointer lands over
 * one of the program's windows (otherwise no event will come) and gives up after a bounded time.
 */
static void let_window_see_pointer(uint32_t motions_before, int x, int y)
{
    if (!is_over_own_window(x, y)) {
        return;
    }
    DWORD deadline = GetTickCount() + POINTER_SETTLE_MILLISECONDS;
    while (xcb_motion_count() == motions_before && (LONG)(deadline - GetTickCount()) > 0) {
        xcb_events_pump();
        Sleep(1);
    }
}

static void send_mouse(DWORD flags, DWORD data)
{
    INPUT input = {.type = INPUT_MOUSE, .mi = {.dwFlags = flags, .mouseData = data}};
    SendInput(1, &input, sizeof(input));
}

LIBC_FN int sysv_XTestQueryExtension(XlDisplay *display, int *event_base, int *error_base, int *major, int *minor)
{
    (void)display;
    *event_base = 0;
    *error_base = 0;
    *major = XTEST_MAJOR;
    *minor = XTEST_MINOR;
    return 1;
}

LIBC_FN int sysv_XTestGrabControl(XlDisplay *display, int is_impervious)
{
    (void)display;
    (void)is_impervious;
    return 1;
}

LIBC_FN int sysv_XTestFakeKeyEvent(XlDisplay *display, unsigned keycode, int is_press, unsigned long delay)
{
    (void)display;
    int is_extended = 0;
    int scancode = xcb_scancode_from_keycode((uint8_t)keycode, &is_extended);
    if (scancode < 0) {
        return 0;
    }
    focus_own_window();
    DWORD flags = KEYEVENTF_SCANCODE | (is_extended ? KEYEVENTF_EXTENDEDKEY : 0) | (is_press ? 0 : KEYEVENTF_KEYUP);
    INPUT input = {.type = INPUT_KEYBOARD, .ki = {.wScan = (WORD)scancode, .dwFlags = flags}};
    delay_for(delay);
    return SendInput(1, &input, sizeof(input)) == 1;
}

LIBC_FN int sysv_XTestFakeMotionEvent(XlDisplay *display, int screen, int x, int y, unsigned long delay)
{
    (void)display;
    (void)screen;
    delay_for(delay);
    POINT before = {0, 0};
    GetCursorPos(&before);
    uint32_t motions_before = xcb_motion_count();
    BOOL is_moved = SetCursorPos(x, y);
    /* A zero move makes Windows post the window its WM_MOUSEMOVE for the new position. */
    send_mouse(MOUSEEVENTF_MOVE, 0);
    if (before.x != x || before.y != y) {
        let_window_see_pointer(motions_before, x, y);
    }
    return is_moved;
}

LIBC_FN int sysv_XTestFakeButtonEvent(XlDisplay *display, unsigned button, int is_press, unsigned long delay)
{
    (void)display;
    delay_for(delay);
    switch (button) {
    case BUTTON_LEFT:
        send_mouse(is_press ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP, 0);
        return 1;
    case BUTTON_MIDDLE:
        send_mouse(is_press ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP, 0);
        return 1;
    case BUTTON_RIGHT:
        send_mouse(is_press ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP, 0);
        return 1;
    case BUTTON_WHEEL_UP:
    case BUTTON_WHEEL_DOWN:
        /* One wheel notch is a press and a release of the button in X; the window procedure reports both, so the release is ignored. */
        if (is_press) {
            send_mouse(MOUSEEVENTF_WHEEL, (DWORD)(button == BUTTON_WHEEL_UP ? WHEEL_DELTA : -WHEEL_DELTA));
        }
        return 1;
    default:
        return 0;
    }
}
