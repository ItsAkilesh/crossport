// File: installer/src/ui/build.rs
// Purpose: Create the child controls of a wizard page from its items, and read the values back
// Author: Akilesh
// Date: 2026-11-05

#![allow(unsafe_code)]

use std::ffi::c_void;

use windows::core::PCWSTR;
use windows::Win32::Foundation::{HINSTANCE, HWND, LPARAM, WPARAM};
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::UI::Controls::{InitCommonControlsEx, ICC_PROGRESS_CLASS, INITCOMMONCONTROLSEX, PBM_SETMARQUEE, PBS_MARQUEE};
use windows::Win32::UI::WindowsAndMessaging::{
    CreateIconFromResourceEx, CreateWindowExW, GetWindowTextLengthW, GetWindowTextW, SendMessageW, BM_GETCHECK,
    BM_SETCHECK, BS_AUTOCHECKBOX, BS_PUSHBUTTON, ES_AUTOHSCROLL, ES_AUTOVSCROLL, ES_MULTILINE, ES_READONLY, HMENU,
    LR_DEFAULTCOLOR, STM_SETICON, WINDOW_EX_STYLE, WINDOW_STYLE, WM_SETFONT, WS_BORDER, WS_CHILD,
    WS_TABSTOP, WS_VISIBLE, WS_VSCROLL,
};

use super::{Browse, Item, Values};
use crate::win::{pcwstr, wide};

pub const BROWSE_BASE: i32 = 3000;
const LEFT: i32 = 24;
const BODY_TOP: i32 = 86;
const BODY_BOTTOM: i32 = 370;
const FULL_WIDTH: i32 = 572;
const BROWSE_WIDTH: i32 = 84;
const LINE: i32 = 18;
const CHARS_PER_LINE: usize = 76;
const ICON_SIZE: i32 = 48;
const PNG_RESOURCE_VERSION: u32 = 0x0003_0000;
const BST_CHECKED: usize = 1;
const SS_ICON: u32 = 3;

/// A control that holds a value.
pub struct Input {
    pub id: &'static str,
    pub hwnd: HWND,
    pub is_check: bool,
}

/// A Browse button and the field it fills.
pub struct BrowseButton {
    pub command: i32,
    pub kind: Browse,
    pub field: HWND,
}

#[derive(Default)]
pub struct Built {
    pub controls: Vec<HWND>,
    pub inputs: Vec<Input>,
    pub browses: Vec<BrowseButton>,
    pub progress: Option<HWND>,
}

fn create(class: &str, text: &str, style: u32, (x, y, w, h): (i32, i32, i32, i32), parent: HWND, id: i32) -> HWND {
    let (class, text) = (wide(class), wide(text));
    // SAFETY: strings outlive the call; the control id travels in the menu parameter as Windows expects.
    unsafe {
        let instance = GetModuleHandleW(None).map(|m| HINSTANCE(m.0)).unwrap_or_default();
        CreateWindowExW(
            WINDOW_EX_STYLE(0),
            pcwstr(&class),
            PCWSTR(text.as_ptr()),
            WINDOW_STYLE(style) | WS_CHILD | WS_VISIBLE,
            x,
            y,
            w,
            h,
            Some(parent),
            Some(HMENU(id as isize as *mut c_void)),
            Some(instance),
            None,
        )
        .unwrap_or_default()
    }
}

fn text_height(text: &str) -> i32 {
    let lines: usize = text.lines().map(|l| l.chars().count().div_ceil(CHARS_PER_LINE).max(1)).sum();
    (lines.max(1) as i32) * LINE + 8
}

fn add_field(b: &mut Built, parent: HWND, y: &mut i32, field: (&'static str, &str, &str, Browse), index: usize) {
    let (id, label, value, browse) = field;
    b.controls.push(create("STATIC", label, 0, (LEFT, *y, FULL_WIDTH, LINE), parent, 0));
    *y += LINE + 4;
    let width = if browse == Browse::None { FULL_WIDTH } else { FULL_WIDTH - BROWSE_WIDTH - 8 };
    let style = (WS_BORDER | WS_TABSTOP).0 | ES_AUTOHSCROLL as u32;
    let edit = create("EDIT", value, style, (LEFT, *y, width, 24), parent, 2000 + index as i32);
    b.controls.push(edit);
    b.inputs.push(Input { id, hwnd: edit, is_check: false });
    if browse != Browse::None {
        let command = BROWSE_BASE + b.browses.len() as i32;
        let button = create("BUTTON", "Browse...", BS_PUSHBUTTON as u32 | WS_TABSTOP.0, (LEFT + width + 8, *y - 1, BROWSE_WIDTH, 26), parent, command);
        b.controls.push(button);
        b.browses.push(BrowseButton { command, kind: browse, field: edit });
    }
    *y += 24 + 14;
}

fn add_icon(b: &mut Built, parent: HWND, y: i32, png: &[u8]) {
    // SAFETY: the PNG bytes are valid for the call; Windows copies them into the icon it creates.
    unsafe {
        let icon = CreateIconFromResourceEx(png, true, PNG_RESOURCE_VERSION, ICON_SIZE, ICON_SIZE, LR_DEFAULTCOLOR);
        let Ok(icon) = icon else { return };
        let control = create("STATIC", "", SS_ICON, (FULL_WIDTH - ICON_SIZE + LEFT, y, ICON_SIZE, ICON_SIZE), parent, 0);
        SendMessageW(control, STM_SETICON, Some(WPARAM(icon.0 as usize)), Some(LPARAM(0)));
        b.controls.push(control);
    }
}

/// Create the controls for `items` under `parent`.
pub fn build(parent: HWND, items: &[Item]) -> Built {
    // SAFETY: plain registration of the progress class; the structure is fully initialised.
    unsafe {
        let init = INITCOMMONCONTROLSEX { dwSize: std::mem::size_of::<INITCOMMONCONTROLSEX>() as u32, dwICC: ICC_PROGRESS_CLASS };
        let _ = InitCommonControlsEx(&init);
    }
    let mut built = Built::default();
    let mut y = BODY_TOP;
    for (index, item) in items.iter().enumerate() {
        match item {
            Item::Text(text) => {
                let height = text_height(text);
                built.controls.push(create("STATIC", text, 0, (LEFT, y, FULL_WIDTH, height), parent, 0));
                y += height + 8;
            }
            Item::Field { id, label, value, browse } => add_field(&mut built, parent, &mut y, (id, label, value, *browse), index),
            Item::Check { id, label, checked } => {
                let hwnd = create("BUTTON", label, BS_AUTOCHECKBOX as u32 | WS_TABSTOP.0, (LEFT, y, FULL_WIDTH, 22), parent, 2000 + index as i32);
                // SAFETY: a plain check-state message to a control just created.
                unsafe { SendMessageW(hwnd, BM_SETCHECK, Some(WPARAM(usize::from(*checked))), Some(LPARAM(0))) };
                built.controls.push(hwnd);
                built.inputs.push(Input { id, hwnd, is_check: true });
                y += 28;
            }
            Item::Progress => {
                let bar = create("msctls_progress32", "", PBS_MARQUEE, (LEFT, y, FULL_WIDTH, 18), parent, 0);
                // SAFETY: starts the marquee animation on the bar just created.
                unsafe { SendMessageW(bar, PBM_SETMARQUEE, Some(WPARAM(1)), Some(LPARAM(30))) };
                built.controls.push(bar);
                built.progress = Some(bar);
                y += 30;
            }
            Item::Box(text) => {
                let style = (ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL) as u32 | (WS_BORDER | WS_VSCROLL | WS_TABSTOP).0;
                let height = (BODY_BOTTOM - y).clamp(60, 250);
                built.controls.push(create("EDIT", &text.replace('\n', "\r\n"), style, (LEFT, y, FULL_WIDTH, height), parent, 0));
                y += height + 8;
            }
            Item::Icon(png) => add_icon(&mut built, parent, y, png),
        }
    }
    built
}

fn text_of(hwnd: HWND) -> String {
    // SAFETY: the buffer is sized from the control's own length plus the terminator.
    unsafe {
        let length = GetWindowTextLengthW(hwnd).max(0) as usize;
        let mut buffer = vec![0u16; length + 1];
        let copied = GetWindowTextW(hwnd, &mut buffer).max(0) as usize;
        String::from_utf16_lossy(&buffer[..copied])
    }
}

/// Read every input: text for fields, "1" or "0" for check boxes.
pub fn read_values(built: &Built) -> Values {
    built
        .inputs
        .iter()
        .map(|input| {
            let value = if input.is_check {
                // SAFETY: a plain check-state query to a live control.
                let state = unsafe { SendMessageW(input.hwnd, BM_GETCHECK, Some(WPARAM(0)), Some(LPARAM(0))) };
                if state.0 as usize == BST_CHECKED { "1".to_string() } else { "0".to_string() }
            } else {
                text_of(input.hwnd)
            };
            (input.id, value)
        })
        .collect()
}

pub fn set_text(hwnd: HWND, text: &str) {
    let text = wide(text);
    // SAFETY: the string is NUL-terminated and outlives the call.
    unsafe {
        let _ = windows::Win32::UI::WindowsAndMessaging::SetWindowTextW(hwnd, pcwstr(&text));
    }
}

/// Apply the font to a control (the wizard calls this for every child it creates).
pub fn set_font(hwnd: HWND, font: windows::Win32::Graphics::Gdi::HFONT) {
    // SAFETY: the font handle stays alive for the life of the window.
    unsafe { SendMessageW(hwnd, WM_SETFONT, Some(WPARAM(font.0 as usize)), Some(LPARAM(1))) };
}
