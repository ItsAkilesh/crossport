// File: installer/src/ui/mod.rs
// Purpose: A small wizard framework: pages described as data, driven by a Flow that decides what comes next
// Author: Akilesh
// Date: 2026-11-05

pub mod build;
pub mod dialogs;
pub mod window;

use std::collections::HashMap;

/// What a Browse button opens.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Browse {
    None,
    Folder,
    AppImage,
}

/// One thing on a page, laid out top to bottom.
#[derive(Debug, Clone)]
pub enum Item {
    Text(String),
    Field { id: &'static str, label: String, value: String, browse: Browse },
    Check { id: &'static str, label: String, checked: bool },
    Progress,
    /// A scrolling, read-only text box.
    Box(String),
    /// A PNG shown at the left of the page body.
    Icon(Vec<u8>),
}

/// How a page behaves.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Normal,
    /// Work is running: no buttons until it finishes.
    Busy,
    /// The last page: only a finish button.
    Final,
}

#[derive(Debug, Clone)]
pub struct Page {
    pub title: String,
    pub subtitle: String,
    pub items: Vec<Item>,
    pub kind: Kind,
    pub next_label: String,
    pub can_go_back: bool,
}

impl Page {
    pub fn new(title: &str, subtitle: &str, items: Vec<Item>) -> Page {
        Page { title: title.into(), subtitle: subtitle.into(), items, kind: Kind::Normal, next_label: "Next".into(), can_go_back: true }
    }
    pub fn labelled(mut self, next: &str) -> Page {
        self.next_label = next.into();
        self
    }
    pub fn first(mut self) -> Page {
        self.can_go_back = false;
        self
    }
    pub fn of_kind(mut self, kind: Kind) -> Page {
        self.kind = kind;
        self
    }
}

/// What the entered values were when Next was pressed: text for fields, "1" or "0" for check boxes.
pub type Values = HashMap<&'static str, String>;

pub fn is_checked(values: &Values, id: &str) -> bool {
    values.get(id).is_some_and(|v| v == "1")
}

/// What to do after Next.
pub enum Action {
    Goto(usize),
    /// Show page `usize` (normally a busy page) and run the job on another thread; the number it returns is the
    /// page to show when it finishes.
    Work(usize, Box<dyn FnOnce() -> usize + Send>),
    Close,
}

/// A wizard's logic. The window asks it for pages and tells it what the user did.
pub trait Flow {
    fn page(&mut self, index: usize) -> Page;
    /// Next was pressed on page `index`. An `Err` is shown to the user and the page stays.
    fn next(&mut self, index: usize, values: &Values) -> Result<Action, String>;
    /// Back was pressed on page `index`; returns the page to show.
    fn back(&mut self, index: usize) -> usize {
        index.saturating_sub(1)
    }
    /// The window closed without finishing (Cancel).
    fn cancelled(&mut self) {}
}

/// Window size in pixels at 96 DPI.
pub const CLIENT_WIDTH: i32 = 620;
pub const CLIENT_HEIGHT: i32 = 440;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn page_builders_set_what_they_say() {
        let page = Page::new("T", "S", vec![]).labelled("Install").first().of_kind(Kind::Final);
        assert_eq!((page.next_label.as_str(), page.can_go_back, page.kind), ("Install", false, Kind::Final));
    }

    #[test]
    fn checked_reads_the_value_string() {
        let mut values = Values::new();
        values.insert("a", "1".into());
        values.insert("b", "0".into());
        assert!(is_checked(&values, "a") && !is_checked(&values, "b") && !is_checked(&values, "c"));
    }
}
