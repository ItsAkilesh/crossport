/*
 * File: shim/xcb/xcb_events.c
 * Purpose: Turn Win32 window messages into X events and serve xcb_poll_for_event and xcb_wait_for_event
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * The window procedure runs inside the program's own call to PeekMessage or GetMessage, on the thread that
 * created the window, and appends X events to a queue that the polling functions then drain.
 */

#include <string.h>
#include <windowsx.h>

#include "xcb_internal.h"

#define QUEUE_CAPACITY 256
#define EVENT_WORDS 8
#define KEYCODE_OFFSET 8
#define SCANCODE_SHIFT 16
#define SCANCODE_MASK 0xff
#define BUTTON_LEFT 1
#define BUTTON_MIDDLE 2
#define BUTTON_RIGHT 3
#define BUTTON_WHEEL_UP 4
#define BUTTON_WHEEL_DOWN 5
#define WHEEL_STEP WHEEL_DELTA
#define CLIENT_MESSAGE_FORMAT 32
#define CURSOR_ARROW MAKEINTRESOURCEW(32512)

static XcbEvent g_queue[QUEUE_CAPACITY];
static size_t g_head;
static size_t g_count;
static SRWLOCK g_queue_lock = SRWLOCK_INIT;

void xcb_events_push(const XcbEvent *event)
{
    AcquireSRWLockExclusive(&g_queue_lock);
    if (g_count < QUEUE_CAPACITY) {
        g_queue[(g_head + g_count) % QUEUE_CAPACITY] = *event;
        g_queue[(g_head + g_count) % QUEUE_CAPACITY].sequence = (uint16_t)xcb_next_sequence();
        g_count++;
    }
    ReleaseSRWLockExclusive(&g_queue_lock);
}

/* Removes the oldest event into a heap block the program will free, or returns NULL when none is waiting. */
static XcbEvent *pop_event(void)
{
    XcbEvent *event = NULL;
    AcquireSRWLockExclusive(&g_queue_lock);
    if (g_count > 0) {
        event = (XcbEvent *)sysv_malloc(sizeof(*event));
        if (event != NULL) {
            *event = g_queue[g_head];
        }
        g_head = (g_head + 1) % QUEUE_CAPACITY;
        g_count--;
    }
    ReleaseSRWLockExclusive(&g_queue_lock);
    return event;
}

/* Queues an event of any layout: the structure is copied into a zero-filled 32-byte event. */
static void push_raw(const void *data, size_t size)
{
    XcbEvent event = {0};
    memcpy(&event, data, size);
    xcb_events_push(&event);
}

static void push_input(HWND window, uint8_t type, uint8_t detail, int x, int y)
{
    XcbInputEvent input = {.response_type = type, .detail = detail, .time = GetTickCount(),
                           .root = xcb_roots()->screens[0].root, .event = xcb_window_id(window),
                           .event_x = (int16_t)x, .event_y = (int16_t)y, .same_screen = 1};
    push_raw(&input, sizeof(input));
}

static void push_key(HWND window, LPARAM parameter, uint8_t type)
{
    uint8_t scancode = (uint8_t)((parameter >> SCANCODE_SHIFT) & SCANCODE_MASK);
    push_input(window, type, (uint8_t)(scancode + KEYCODE_OFFSET), 0, 0);
}

static void push_button(HWND window, LPARAM parameter, uint8_t type, uint8_t button)
{
    push_input(window, type, button, GET_X_LPARAM(parameter), GET_Y_LPARAM(parameter));
}

static void push_wheel(HWND window, WPARAM parameter, LPARAM position)
{
    POINT point = {GET_X_LPARAM(position), GET_Y_LPARAM(position)};
    ScreenToClient(window, &point);
    uint8_t button = GET_WHEEL_DELTA_WPARAM(parameter) > 0 ? BUTTON_WHEEL_UP : BUTTON_WHEEL_DOWN;
    push_input(window, XCB_EVENT_BUTTON_PRESS, button, point.x, point.y);
    push_input(window, XCB_EVENT_BUTTON_RELEASE, button, point.x, point.y);
}

static void push_expose(HWND window)
{
    RECT client;
    GetClientRect(window, &client);
    XcbExposeEvent expose = {.response_type = XCB_EVENT_EXPOSE, .window = xcb_window_id(window),
                             .width = (uint16_t)client.right, .height = (uint16_t)client.bottom};
    push_raw(&expose, sizeof(expose));
}

static void push_configure(HWND window)
{
    RECT client;
    GetClientRect(window, &client);
    XcbId id = xcb_window_id(window);
    XcbConfigureEvent configure = {.response_type = XCB_EVENT_CONFIGURE_NOTIFY, .event = id, .window = id,
                                   .width = (uint16_t)client.right, .height = (uint16_t)client.bottom};
    push_raw(&configure, sizeof(configure));
}

/* The close button: a program that asked for WM_DELETE_WINDOW gets a message, any other window is destroyed. */
static void request_close(HWND window)
{
    XcbId id = xcb_window_id(window);
    uint32_t delete_atom = xcb_window_delete_atom(id);
    if (delete_atom == 0) {
        DestroyWindow(window);
        return;
    }
    XcbClientMessageEvent message = {.response_type = XCB_EVENT_CLIENT_MESSAGE, .format = CLIENT_MESSAGE_FORMAT,
                                     .window = id, .type = xcb_atom_for("WM_PROTOCOLS", 12),
                                     .data32 = {delete_atom, 0, 0, 0, 0}};
    push_raw(&message, sizeof(message));
}

static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CLOSE:
        request_close(window);
        return 0;
    case WM_DESTROY:
        xcb_window_forget(window);
        return 0;
    case WM_PAINT:
        push_expose(window);
        break;
    case WM_SIZE:
        push_configure(window);
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        push_key(window, lparam, XCB_EVENT_KEY_PRESS);
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        push_key(window, lparam, XCB_EVENT_KEY_RELEASE);
        break;
    case WM_MOUSEMOVE:
        push_button(window, lparam, XCB_EVENT_MOTION_NOTIFY, 0);
        break;
    case WM_LBUTTONDOWN:
        push_button(window, lparam, XCB_EVENT_BUTTON_PRESS, BUTTON_LEFT);
        break;
    case WM_LBUTTONUP:
        push_button(window, lparam, XCB_EVENT_BUTTON_RELEASE, BUTTON_LEFT);
        break;
    case WM_MBUTTONDOWN:
        push_button(window, lparam, XCB_EVENT_BUTTON_PRESS, BUTTON_MIDDLE);
        break;
    case WM_MBUTTONUP:
        push_button(window, lparam, XCB_EVENT_BUTTON_RELEASE, BUTTON_MIDDLE);
        break;
    case WM_RBUTTONDOWN:
        push_button(window, lparam, XCB_EVENT_BUTTON_PRESS, BUTTON_RIGHT);
        break;
    case WM_RBUTTONUP:
        push_button(window, lparam, XCB_EVENT_BUTTON_RELEASE, BUTTON_RIGHT);
        break;
    case WM_MOUSEWHEEL:
        push_wheel(window, wparam, lparam);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static BOOL CALLBACK register_class(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    WNDCLASSEXW window_class = {.cbSize = sizeof(window_class), .lpfnWndProc = window_procedure,
                                .hInstance = GetModuleHandleW(NULL), .hCursor = LoadCursorW(NULL, CURSOR_ARROW),
                                .lpszClassName = XCB_WINDOW_CLASS};
    return RegisterClassExW(&window_class) != 0;
}

void xcb_events_register_class(void)
{
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, register_class, NULL, NULL);
}

static void pump_messages(void)
{
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

LIBC_FN XcbEvent *sysv_xcb_poll_for_event(void *connection)
{
    (void)connection;
    pump_messages();
    return pop_event();
}

LIBC_FN XcbEvent *sysv_xcb_wait_for_event(void *connection)
{
    (void)connection;
    XcbEvent *event = NULL;
    while (event == NULL) {
        pump_messages();
        event = pop_event();
        if (event == NULL) {
            WaitMessage();
        }
    }
    return event;
}
