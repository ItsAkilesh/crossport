// File: installer/src/flows/setup.rs
// Purpose: The Crossport setup wizard: welcome, licence, folder, options, progress, finish
// Author: Akilesh
// Date: 2026-11-05

use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use crate::cli::SetupSilent;
use crate::ops::run_setup;
use crate::paths::Scope;
use crate::ui::{is_checked, Action, Browse, Flow, Item, Kind, Page, Values};
use crate::win::roots;

const LICENCE: &str = include_str!("../../../LICENSE");
const PAGE_WELCOME: usize = 0;
const PAGE_LICENCE: usize = 1;
const PAGE_FOLDER: usize = 2;
const PAGE_OPTIONS: usize = 3;
const PAGE_WORKING: usize = 4;
const PAGE_DONE: usize = 5;

type Shared = Arc<Mutex<Option<Result<PathBuf, String>>>>;

/// The setup wizard for Crossport itself.
pub struct SetupFlow {
    setup_exe: PathBuf,
    dir: String,
    per_user: bool,
    outcome: Shared,
}

fn default_dir(per_user: bool) -> String {
    let scope = if per_user { Scope::User } else { Scope::Machine };
    roots().map(|r| r.product_dir(scope).to_string_lossy().into_owned()).unwrap_or_default()
}

impl SetupFlow {
    pub fn new(setup_exe: PathBuf) -> SetupFlow {
        SetupFlow { setup_exe, dir: default_dir(false), per_user: false, outcome: Arc::new(Mutex::new(None)) }
    }

    fn silent(&self, values: &Values) -> SetupSilent {
        SetupSilent {
            dir: Some(PathBuf::from(&self.dir)),
            per_user: self.per_user,
            start_menu: is_checked(values, "start_menu"),
            desktop: is_checked(values, "desktop"),
            add_to_path: is_checked(values, "path"),
            associate: is_checked(values, "associate"),
            result_file: None,
        }
    }

    fn done_page(&self) -> Page {
        let result = self.outcome.lock().ok().and_then(|mut o| o.take());
        let page = match result {
            Some(Ok(dir)) => Page::new("Crossport is installed", "You are ready to open AppImages.", vec![
                Item::Text(format!("Crossport is in {}.", dir.display())),
                Item::Text("Download an AppImage and double-click it. If Windows asks which app to use, choose Crossport and select Always.".into()),
            ]),
            Some(Err(message)) => Page::new("Setup could not finish", "Nothing was changed.", vec![Item::Text(message)]),
            None => Page::new("Setup stopped", "", vec![]),
        };
        let mut page = page.of_kind(Kind::Final).labelled("Finish");
        page.can_go_back = false;
        page
    }

    fn take_folder(&mut self, values: &Values) -> Result<bool, String> {
        let per_user = is_checked(values, "per_user");
        let dir = values.get("dir").map(|v| v.trim().to_string()).unwrap_or_default();
        if per_user != self.per_user && dir == self.dir {
            self.per_user = per_user;
            self.dir = default_dir(per_user);
            return Ok(true);
        }
        self.per_user = per_user;
        if !Path::new(&dir).is_absolute() {
            return Err("Enter a full folder path, such as C:\\Program Files\\Crossport.".into());
        }
        self.dir = dir;
        Ok(false)
    }
}

impl Flow for SetupFlow {
    fn page(&mut self, index: usize) -> Page {
        match index {
            PAGE_WELCOME => Page::new("Welcome to Crossport", "Run Linux AppImages as Windows programs", vec![
                Item::Text("This installs Crossport on your computer. Crossport turns an AppImage you download into a normal Windows app, with its own Start menu entry and uninstaller.".into()),
                Item::Text("No apps or settings come with it: you add apps by opening AppImage files.".into()),
            ]).first(),
            PAGE_LICENCE => Page::new("Licence", "Please read and accept to continue.", vec![
                Item::Box(LICENCE.to_string()),
                Item::Check { id: "accept", label: "I accept the licence".into(), checked: false },
            ]),
            PAGE_FOLDER => Page::new("Where to install", "Choose a folder for Crossport.", vec![
                Item::Field { id: "dir", label: "Install folder".into(), value: self.dir.clone(), browse: Browse::Folder },
                Item::Check { id: "per_user", label: "Install for me only (no administrator permission needed)".into(), checked: self.per_user },
                Item::Text("Apps you install later go in folders beside Crossport by default.".into()),
            ]),
            PAGE_OPTIONS => Page::new("Options", "Choose what else to set up.", vec![
                Item::Check { id: "start_menu", label: "Add Crossport to the Start menu".into(), checked: true },
                Item::Check { id: "desktop", label: "Add a desktop shortcut".into(), checked: false },
                Item::Check { id: "path", label: "Add the Crossport folder to PATH (for the crossport command)".into(), checked: false },
                Item::Check { id: "associate", label: "Open .AppImage files with Crossport".into(), checked: true },
            ]).labelled("Install"),
            PAGE_WORKING => Page::new("Installing", "Copying files and setting things up.", vec![Item::Progress]).of_kind(Kind::Busy),
            _ => self.done_page(),
        }
    }

    fn next(&mut self, index: usize, values: &Values) -> Result<Action, String> {
        match index {
            PAGE_LICENCE if !is_checked(values, "accept") => Err("Accept the licence to continue.".into()),
            PAGE_FOLDER => Ok(Action::Goto(if self.take_folder(values)? { PAGE_FOLDER } else { PAGE_OPTIONS })),
            PAGE_OPTIONS => {
                let (silent, exe, outcome) = (self.silent(values), self.setup_exe.clone(), self.outcome.clone());
                let job = move || {
                    let result = run_setup(&silent, &exe);
                    if let Ok(mut slot) = outcome.lock() {
                        *slot = Some(result);
                    }
                    PAGE_DONE
                };
                Ok(Action::Work(PAGE_WORKING, Box::new(job)))
            }
            PAGE_DONE => Ok(Action::Close),
            other => Ok(Action::Goto(other + 1)),
        }
    }
}
