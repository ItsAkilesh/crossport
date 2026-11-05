// File: installer/src/ui/window.rs
// Purpose: The wizard window: header, page area, Back/Next/Cancel buttons, message loop and background work
// Author: Akilesh
// Date: 2026-11-05

#![allow(unsafe_code)]

use std::ffi::c_void;

use windows::core::{w, PCWSTR};
use windows::Win32::Foundation::{COLORREF, HINSTANCE, HWND, LPARAM, LRESULT, RECT, WPARAM};
use windows::Win32::Graphics::Gdi::{
    BeginPaint, CreateFontW, CreateSolidBrush, DrawTextW, EndPaint, FillRect, GetStockObject, InvalidateRect, SetBkColor,
    SetBkMode, SetTextColor, CLEARTYPE_QUALITY, DEFAULT_CHARSET, DRAW_TEXT_FORMAT, DT_LEFT, DT_SINGLELINE, DT_VCENTER,
    HBRUSH, HDC, HFONT, PAINTSTRUCT, TRANSPARENT, WHITE_BRUSH, SelectObject,
};
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::UI::Input::KeyboardAndMouse::EnableWindow;
use windows::Win32::UI::WindowsAndMessaging::*;

use super::build::{build, read_values, set_font, Built, BROWSE_BASE};
use super::dialogs;
use super::{Action, Flow, Kind, Page, CLIENT_HEIGHT, CLIENT_WIDTH};
use crate::win::{pcwstr, wide};

const ID_BACK: i32 = 1001;
const ID_NEXT: i32 = 1002;
const ID_CANCEL: i32 = 1003;
const WM_WORK_DONE: u32 = WM_APP + 1;
const HEADER_HEIGHT: i32 = 70;
const FOOTER_TOP: i32 = 384;
const BUTTON_WIDTH: i32 = 100;
const BUTTON_HEIGHT: i32 = 30;
const HEADER_TEXT_LEFT: i32 = 24;

struct State {
    flow: Box<dyn Flow>,
    page_index: usize,
    page: Page,
    built: Built,
    buttons: [HWND; 3],
    font: HFONT,
    title_font: HFONT,
    is_busy: bool,
}

fn rgb(r: u8, g: u8, b: u8) -> COLORREF {
    COLORREF(u32::from(r) | u32::from(g) << 8 | u32::from(b) << 16)
}

fn make_font(height: i32, weight: i32) -> HFONT {
    // SAFETY: the face name outlives the call; the font is kept for the life of the window.
    unsafe { CreateFontW(height, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, Default::default(), Default::default(), CLEARTYPE_QUALITY, 0, w!("Segoe UI")) }
}

fn state<'a>(hwnd: HWND) -> Option<&'a mut State> {
    // SAFETY: GWLP_USERDATA holds the pointer set in WM_NCCREATE and cleared in WM_NCDESTROY, so it is either null or live.
    unsafe { (GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut State).as_mut() }
}

fn button(parent: HWND, text: &str, id: i32, x: i32) -> HWND {
    let text = wide(text);
    // SAFETY: strings outlive the call; the id travels in the menu parameter.
    unsafe {
        let instance = GetModuleHandleW(None).map(|m| HINSTANCE(m.0)).unwrap_or_default();
        CreateWindowExW(
            WINDOW_EX_STYLE(0),
            w!("BUTTON"),
            pcwstr(&text),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WINDOW_STYLE(BS_PUSHBUTTON as u32),
            x,
            FOOTER_TOP + 14,
            BUTTON_WIDTH,
            BUTTON_HEIGHT,
            Some(parent),
            Some(HMENU(id as isize as *mut c_void)),
            Some(instance),
            None,
        )
        .unwrap_or_default()
    }
}

fn show_page(hwnd: HWND, st: &mut State, index: usize) {
    for control in st.built.controls.drain(..) {
        // SAFETY: the control belongs to this window and is not used again.
        unsafe { let _ = DestroyWindow(control); }
    }
    st.page_index = index;
    st.page = st.flow.page(index);
    st.built = build(hwnd, &st.page.items);
    for control in &st.built.controls {
        set_font(*control, st.font);
    }
    let [back, next, cancel] = st.buttons;
    let (is_busy, is_final) = (st.page.kind == Kind::Busy, st.page.kind == Kind::Final);
    st.is_busy = is_busy;
    super::build::set_text(next, &st.page.next_label);
    // SAFETY: plain enable and show calls on this window's own buttons.
    unsafe {
        let _ = EnableWindow(back, st.page.can_go_back && !is_busy);
        let _ = EnableWindow(next, !is_busy);
        let _ = EnableWindow(cancel, !is_busy);
        let _ = ShowWindow(back, if is_final { SW_HIDE } else { SW_SHOW });
        let _ = ShowWindow(cancel, if is_final { SW_HIDE } else { SW_SHOW });
        let _ = InvalidateRect(Some(hwnd), None, true);
    }
}

fn on_next(hwnd: HWND, st: &mut State) {
    let values = read_values(&st.built);
    match st.flow.next(st.page_index, &values) {
        Ok(Action::Goto(index)) => show_page(hwnd, st, index),
        Ok(Action::Close) => unsafe { let _ = DestroyWindow(hwnd); },
        Ok(Action::Work(busy_page, job)) => {
            show_page(hwnd, st, busy_page);
            let raw = hwnd.0 as isize;
            std::thread::spawn(move || {
                let next = job();
                // SAFETY: posting to a window handle is thread safe, and a closed window just drops the message.
                unsafe { let _ = PostMessageW(Some(HWND(raw as *mut c_void)), WM_WORK_DONE, WPARAM(0), LPARAM(next as isize)); }
            });
        }
        Err(message) => dialogs::error(Some(hwnd), &message),
    }
}

fn on_browse(hwnd: HWND, st: &State, command: i32) {
    let Some(target) = st.built.browses.iter().find(|b| b.command == command) else { return };
    if let Some(path) = dialogs::pick(hwnd, target.kind, "Choose") {
        super::build::set_text(target.field, &path.to_string_lossy());
    }
}

fn on_cancel(hwnd: HWND, st: &mut State) {
    if st.is_busy {
        return;
    }
    let is_final = st.page.kind == Kind::Final;
    if is_final || dialogs::confirm(Some(hwnd), "Cancel and close this window?") {
        if !is_final {
            st.flow.cancelled();
        }
        // SAFETY: destroys this window; the loop ends in WM_DESTROY.
        unsafe { let _ = DestroyWindow(hwnd); }
    }
}

fn paint(hwnd: HWND, st: &State) {
    // SAFETY: standard paint sequence; all handles are created and released inside.
    unsafe {
        let mut ps = PAINTSTRUCT::default();
        let hdc: HDC = BeginPaint(hwnd, &mut ps);
        let white = HBRUSH(GetStockObject(WHITE_BRUSH).0);
        let footer = CreateSolidBrush(rgb(240, 240, 240));
        let line = CreateSolidBrush(rgb(218, 218, 218));
        let band = |top: i32, bottom: i32, brush: HBRUSH| {
            let rect = RECT { left: 0, top, right: CLIENT_WIDTH, bottom };
            FillRect(hdc, &rect, brush);
        };
        band(0, HEADER_HEIGHT, white);
        band(HEADER_HEIGHT, HEADER_HEIGHT + 1, line);
        band(FOOTER_TOP, CLIENT_HEIGHT, footer);
        band(FOOTER_TOP, FOOTER_TOP + 1, line);
        SetBkMode(hdc, TRANSPARENT);
        draw(hdc, &st.page.title, st.title_font, (HEADER_TEXT_LEFT, 10, 38), rgb(0, 0, 0));
        draw(hdc, &st.page.subtitle, st.font, (HEADER_TEXT_LEFT, 44, 22), rgb(96, 96, 96));
        let _ = windows::Win32::Graphics::Gdi::DeleteObject(footer.into());
        let _ = windows::Win32::Graphics::Gdi::DeleteObject(line.into());
        let _ = EndPaint(hwnd, &ps);
    }
}

unsafe fn draw(hdc: HDC, text: &str, font: HFONT, (left, top, height): (i32, i32, i32), color: COLORREF) {
    SelectObject(hdc, font.into());
    SetTextColor(hdc, color);
    let mut rect = RECT { left, top, right: CLIENT_WIDTH - left, bottom: top + height };
    let mut buffer: Vec<u16> = text.encode_utf16().collect();
    DrawTextW(hdc, &mut buffer, &mut rect, DRAW_TEXT_FORMAT(DT_LEFT.0 | DT_SINGLELINE.0 | DT_VCENTER.0));
}

unsafe extern "system" fn procedure(hwnd: HWND, message: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    if message == WM_NCCREATE {
        let create = lparam.0 as *const CREATESTRUCTW;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (*create).lpCreateParams as isize);
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    let Some(st) = state(hwnd) else { return DefWindowProcW(hwnd, message, wparam, lparam) };
    match message {
        WM_PAINT => paint(hwnd, st),
        WM_COMMAND => match (wparam.0 & 0xffff) as i32 {
            ID_NEXT => on_next(hwnd, st),
            ID_BACK => {
                let index = st.flow.back(st.page_index);
                show_page(hwnd, st, index);
            }
            ID_CANCEL => on_cancel(hwnd, st),
            command if command >= BROWSE_BASE => on_browse(hwnd, st, command),
            _ => {}
        },
        WM_WORK_DONE => show_page(hwnd, st, lparam.0 as usize),
        WM_CTLCOLORSTATIC | WM_CTLCOLORBTN => {
            let hdc = HDC(wparam.0 as *mut c_void);
            SetBkColor(hdc, rgb(255, 255, 255));
            return LRESULT(GetStockObject(WHITE_BRUSH).0 as isize);
        }
        WM_CLOSE => {
            on_cancel(hwnd, st);
            return LRESULT(0);
        }
        WM_DESTROY => PostQuitMessage(0),
        _ => return DefWindowProcW(hwnd, message, wparam, lparam),
    }
    LRESULT(0)
}

/// Show the wizard and run it until it closes.
pub fn run(flow: Box<dyn Flow>, caption: &str) {
    // SAFETY: the state box is leaked into the window and reclaimed after the message loop; every handle used
    // is created here and lives until the loop ends.
    unsafe {
        let instance = GetModuleHandleW(None).map(|m| HINSTANCE(m.0)).unwrap_or_default();
        let class = w!("CrossportWizard");
        let wc = WNDCLASSEXW {
            cbSize: std::mem::size_of::<WNDCLASSEXW>() as u32,
            lpfnWndProc: Some(procedure),
            hInstance: instance,
            hCursor: LoadCursorW(None, IDC_ARROW).unwrap_or_default(),
            hbrBackground: HBRUSH(GetStockObject(WHITE_BRUSH).0),
            lpszClassName: class,
            ..Default::default()
        };
        RegisterClassExW(&wc);
        let style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        let mut frame = RECT { left: 0, top: 0, right: CLIENT_WIDTH, bottom: CLIENT_HEIGHT };
        let _ = AdjustWindowRectEx(&mut frame, style, false, WINDOW_EX_STYLE(0));
        let (width, height) = (frame.right - frame.left, frame.bottom - frame.top);
        let x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        let y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
        let mut st = Box::new(State {
            flow,
            page_index: 0,
            page: Page::new("", "", vec![]),
            built: Built::default(),
            buttons: [HWND::default(); 3],
            font: make_font(-12, 400),
            title_font: make_font(-22, 600),
            is_busy: false,
        });
        let raw: *mut State = &mut *st;
        let caption = wide(caption);
        let hwnd = CreateWindowExW(WINDOW_EX_STYLE(0), class, PCWSTR(caption.as_ptr()), style, x, y, width, height, None, None, Some(instance), Some(raw as *const c_void));
        let Ok(hwnd) = hwnd else { return };
        let right = CLIENT_WIDTH - 24 - BUTTON_WIDTH;
        st.buttons = [
            button(hwnd, "Back", ID_BACK, right - 2 * (BUTTON_WIDTH + 8)),
            button(hwnd, "Next", ID_NEXT, right - (BUTTON_WIDTH + 8)),
            button(hwnd, "Cancel", ID_CANCEL, right),
        ];
        for b in st.buttons {
            set_font(b, st.font);
        }
        show_page(hwnd, &mut st, 0);
        let _ = ShowWindow(hwnd, SW_SHOW);
        let mut msg = MSG::default();
        while GetMessageW(&mut msg, None, 0, 0).as_bool() {
            if !IsDialogMessageW(hwnd, &msg).as_bool() {
                let _ = TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
}
