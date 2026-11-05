// File: installer/src/flows/open.rs
// Purpose: The wizard shown when an .AppImage is opened: name, folder, shortcuts, progress, then a result report
// Author: Akilesh
// Date: 2026-11-05

use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use crate::appimage_meta::{self, AppMeta};
use crate::cli::OpenSilent;
use crate::engine::Tools;
use crate::ops::{display_name, run_open, OpenResult};
use crate::paths::{Roots, Scope};
use crate::report::Report;
use crate::ui::{is_checked, Action, Browse, Flow, Item, Kind, Page, Values};
use crate::win::roots;

#[derive(Clone, Copy, PartialEq, Eq)]
enum Step {
    Choose,
    Intro,
    Where,
    Shortcuts,
    Working,
    Done,
}

type Shared = Arc<Mutex<Option<Result<OpenResult, String>>>>;

/// The AppImage launcher wizard.
pub struct OpenFlow {
    steps: Vec<Step>,
    image: PathBuf,
    meta: AppMeta,
    name: String,
    dir: String,
    per_user: bool,
    launcher: PathBuf,
    tools: Tools,
    outcome: Shared,
    launch: Option<PathBuf>,
}

fn default_dir(name: &str, per_user: bool) -> String {
    let scope = if per_user { Scope::User } else { Scope::Machine };
    let from = |roots: Roots| roots.app_dir(scope, name).map(|d| d.to_string_lossy().into_owned()).ok();
    roots().and_then(from).unwrap_or_default()
}

impl OpenFlow {
    pub fn new(image: Option<PathBuf>, launcher: PathBuf, tools: Tools) -> OpenFlow {
        let mut steps = vec![Step::Intro, Step::Where, Step::Shortcuts, Step::Working, Step::Done];
        if image.is_none() {
            steps.insert(0, Step::Choose);
        }
        let mut flow = OpenFlow {
            steps,
            image: PathBuf::new(),
            meta: AppMeta::default(),
            name: String::new(),
            dir: String::new(),
            per_user: false,
            launcher,
            tools,
            outcome: Arc::new(Mutex::new(None)),
            launch: None,
        };
        if let Some(image) = image {
            flow.choose(&image);
        }
        flow
    }

    fn choose(&mut self, image: &Path) {
        self.image = image.to_path_buf();
        self.meta = appimage_meta::read(image).unwrap_or_default();
        self.name = display_name(None, self.meta.name.as_deref(), image);
        self.dir = default_dir(&self.name, self.per_user);
    }

    fn step_index(&self, step: Step) -> usize {
        self.steps.iter().position(|s| *s == step).unwrap_or(0)
    }

    fn silent(&self, values: &Values) -> OpenSilent {
        OpenSilent {
            image: self.image.clone(),
            name: Some(self.name.clone()),
            dir: Some(PathBuf::from(&self.dir)),
            per_user: self.per_user,
            start_menu: is_checked(values, "start_menu"),
            desktop: is_checked(values, "desktop"),
            stub_missing: is_checked(values, "stub_missing"),
            result_file: None,
        }
    }

    fn done_page(&mut self) -> Page {
        let result = self.outcome.lock().ok().and_then(|mut o| o.take());
        let mut page = match result {
            Some(Ok(done)) => self.finished(done),
            Some(Err(message)) => Page::new("Could not install", &self.name, vec![Item::Text(message)]),
            None => Page::new("Nothing happened", &self.name, vec![]),
        };
        page.can_go_back = false;
        page.of_kind(Kind::Final).labelled("Finish")
    }

    fn finished(&mut self, done: OpenResult) -> Page {
        let mut items = report_items(&done.report);
        if let Some(exe) = &done.exe {
            self.launch = Some(exe.clone());
            let label = format!("Start {} now", self.name);
            items.insert(0, Item::Check { id: "launch", label, checked: true });
            items.insert(0, Item::Text(format!("{} is installed in {}.", self.name, done.dir.display())));
            return Page::new("Installed", &self.name, items);
        }
        Page::new("This app cannot be converted yet", &self.name, items)
    }
}

fn report_items(report: &Report) -> Vec<Item> {
    if report.findings.is_empty() {
        return Vec::new();
    }
    let text: Vec<String> = report.findings.iter().map(|f| format!("{}\n    {}", f.summary, f.advice)).collect();
    vec![Item::Text("What Crossport found:".into()), Item::Box(text.join("\n\n"))]
}

impl Flow for OpenFlow {
    fn page(&mut self, index: usize) -> Page {
        match self.steps[index] {
            Step::Choose => Page::new("Install an AppImage", "Choose the AppImage file to install.", vec![
                Item::Field { id: "image", label: "AppImage file".into(), value: String::new(), browse: Browse::AppImage },
            ]).first(),
            Step::Intro => {
                let mut items = vec![Item::Text(format!("Crossport will convert this AppImage into a Windows app.\n{}", self.image.display()))];
                if let Some(png) = &self.meta.icon_png {
                    items.insert(0, Item::Icon(png.clone()));
                }
                items.push(Item::Field { id: "name", label: "Name".into(), value: self.name.clone(), browse: Browse::None });
                Page::new(&format!("Install {}", self.name), "Welcome", items).first()
            }
            Step::Where => Page::new("Where to install", "Choose a folder for this app.", vec![
                Item::Field { id: "dir", label: "Install folder".into(), value: self.dir.clone(), browse: Browse::Folder },
                Item::Check { id: "per_user", label: "Install for me only (no administrator permission needed)".into(), checked: self.per_user },
                Item::Text("Program Files asks for administrator permission when you press Next. A folder in your own account does not.".into()),
            ]),
            Step::Shortcuts => Page::new("Shortcuts", "Choose how to start the app.", vec![
                Item::Check { id: "start_menu", label: "Add to the Start menu".into(), checked: true },
                Item::Check { id: "desktop", label: "Add a desktop shortcut".into(), checked: false },
                Item::Check { id: "stub_missing", label: "Continue even if some Linux libraries are missing (the app may not work fully)".into(), checked: false },
            ]).labelled("Install"),
            Step::Working => Page::new("Installing", &format!("Converting {}. This can take a minute.", self.name), vec![Item::Progress]).of_kind(Kind::Busy),
            Step::Done => self.done_page(),
        }
    }

    fn next(&mut self, index: usize, values: &Values) -> Result<Action, String> {
        let step = self.steps[index];
        match step {
            Step::Choose => {
                let image = values.get("image").map(|v| v.trim().to_string()).unwrap_or_default();
                if !Path::new(&image).is_file() {
                    return Err("Choose an AppImage file that exists.".into());
                }
                self.choose(Path::new(&image));
            }
            Step::Intro => {
                let name = values.get("name").map(|v| v.trim().to_string()).unwrap_or_default();
                if name.is_empty() {
                    return Err("Give the app a name.".into());
                }
                if name != self.name {
                    self.name = name;
                    self.dir = default_dir(&self.name, self.per_user);
                }
            }
            Step::Where => {
                if self.take_location(values)? {
                    return Ok(Action::Goto(index));
                }
            }
            Step::Shortcuts => {
                let silent = self.silent(values);
                let (launcher, tools, outcome) = (self.launcher.clone(), self.tools.clone(), self.outcome.clone());
                let done = self.step_index(Step::Done);
                let job = move || {
                    let result = run_open(&silent, &launcher, &tools);
                    if let Ok(mut slot) = outcome.lock() {
                        *slot = Some(result);
                    }
                    done
                };
                return Ok(Action::Work(self.step_index(Step::Working), Box::new(job)));
            }
            Step::Done => {
                self.maybe_launch(values);
                return Ok(Action::Close);
            }
            Step::Working => {}
        }
        Ok(Action::Goto(index + 1))
    }

    fn back(&mut self, index: usize) -> usize {
        index.saturating_sub(1)
    }
}

impl OpenFlow {
    /// Store the folder choice. `Ok(true)` means the folder was swapped for the other scope's default: show the page again.
    fn take_location(&mut self, values: &Values) -> Result<bool, String> {
        let per_user = is_checked(values, "per_user");
        let dir = values.get("dir").map(|v| v.trim().to_string()).unwrap_or_default();
        if per_user != self.per_user && dir == self.dir {
            self.per_user = per_user;
            self.dir = default_dir(&self.name, per_user);
            return Ok(true);
        }
        self.per_user = per_user;
        if dir.is_empty() || !Path::new(&dir).is_absolute() {
            return Err("Enter a full folder path, such as C:\\Program Files\\Crossport\\App.".into());
        }
        self.dir = dir;
        Ok(false)
    }

    fn maybe_launch(&self, values: &Values) {
        if !is_checked(values, "launch") {
            return;
        }
        if let Some(exe) = &self.launch {
            let _ = std::process::Command::new(exe).current_dir(exe.parent().unwrap_or(Path::new("."))).spawn();
        }
    }
}
