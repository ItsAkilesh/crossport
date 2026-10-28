// File: translator/src/appimage/extract.rs
// Purpose: Extract an AppImage's SquashFS payload to a directory under strict size, path and collision limits
// Author: Akilesh
// Date: 2026-10-28

use std::collections::{HashMap, HashSet};
use std::fs::{self, File, OpenOptions};
use std::io::{BufReader, Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

use backhand::{FilesystemReader, InnerNode};

use super::header::{squashfs_offset, PREFIX_BYTES};
use super::paths::{case_key, resolve_link, safe_components};
use crate::error::TranslateError;

const APPRUN: &str = "AppRun";
const ELF_MAGIC: [u8; 4] = [0x7f, b'E', b'L', b'F'];
const MAX_LINK_HOPS: usize = 16;

/// Upper bounds that keep a hostile archive from exhausting disk or memory.
#[derive(Debug, Clone, Copy)]
pub struct Limits {
    pub max_entries: usize,
    pub max_file_bytes: u64,
    pub max_total_bytes: u64,
}

impl Default for Limits {
    fn default() -> Self {
        Self {
            max_entries: 200_000,
            max_file_bytes: 2 * 1024 * 1024 * 1024,
            max_total_bytes: 4 * 1024 * 1024 * 1024,
        }
    }
}

#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct UnpackReport {
    pub files: usize,
    pub directories: usize,
    /// Symlinks Windows cannot hold natively; each became a copy of its target.
    pub links_materialized: usize,
    /// Entries left out, with the reason: devices, directory symlinks, dangling or external links.
    pub skipped: Vec<String>,
    pub total_bytes: u64,
}

#[derive(Debug)]
pub struct AppDir {
    pub root: PathBuf,
    /// Archive-relative components of the program `AppRun` runs (itself, when it is a regular file).
    pub apprun_target: Vec<String>,
    pub report: UnpackReport,
}

fn io_error(context: String, source: std::io::Error) -> TranslateError {
    TranslateError::Io { context, source }
}

fn unreadable(error: impl std::fmt::Display) -> TranslateError {
    TranslateError::Elf(format!("SquashFS: {error}"))
}

pub fn unpack(image: &Path, out_dir: &Path, limits: &Limits) -> Result<AppDir, TranslateError> {
    let offset = payload_offset(image)?;
    prepare_destination(out_dir)?;
    let file =
        File::open(image).map_err(|e| io_error(format!("opening {}", image.display()), e))?;
    let filesystem = FilesystemReader::from_reader_with_offset(BufReader::new(file), offset)
        .map_err(unreadable)?;
    let mut state = Extraction::new(out_dir, limits);
    state.extract_entries(&filesystem)?;
    state.materialize_links()?;
    let apprun_target = state.check_apprun()?;
    Ok(AppDir {
        root: out_dir.to_path_buf(),
        apprun_target,
        report: state.report,
    })
}

fn payload_offset(image: &Path) -> Result<u64, TranslateError> {
    let mut file =
        File::open(image).map_err(|e| io_error(format!("opening {}", image.display()), e))?;
    let length = file
        .metadata()
        .map_err(|e| io_error(format!("inspecting {}", image.display()), e))?
        .len();
    let mut prefix = vec![0u8; PREFIX_BYTES.min(length as usize)];
    file.read_exact(&mut prefix)
        .map_err(|e| io_error(format!("reading {}", image.display()), e))?;
    squashfs_offset(&prefix, length, |at| {
        let mut magic = [0u8; 4];
        file.seek(SeekFrom::Start(at)).ok()?;
        file.read_exact(&mut magic).ok()?;
        Some(magic)
    })
}

/// The destination must be new or empty, so nothing already there can be overwritten or mixed in.
fn prepare_destination(out_dir: &Path) -> Result<(), TranslateError> {
    if out_dir.exists() {
        let mut entries = fs::read_dir(out_dir)
            .map_err(|e| io_error(format!("listing {}", out_dir.display()), e))?;
        if entries.next().is_some() {
            return Err(TranslateError::Usage(format!(
                "{} is not empty",
                out_dir.display()
            )));
        }
    }
    fs::create_dir_all(out_dir).map_err(|e| io_error(format!("creating {}", out_dir.display()), e))
}

struct Extraction<'a> {
    root: &'a Path,
    limits: &'a Limits,
    report: UnpackReport,
    /// Case-folded path -> the name as stored, to catch Windows case collisions.
    seen: HashMap<String, String>,
    files: HashMap<String, Vec<String>>,
    links: Vec<(Vec<String>, PathBuf)>,
    directories: HashSet<String>,
    /// Case-folded link path -> the regular file it was copied from.
    link_origin: HashMap<String, Vec<String>>,
}

impl<'a> Extraction<'a> {
    fn new(root: &'a Path, limits: &'a Limits) -> Self {
        Self {
            root,
            limits,
            report: UnpackReport::default(),
            seen: HashMap::new(),
            files: HashMap::new(),
            links: Vec::new(),
            directories: HashSet::new(),
            link_origin: HashMap::new(),
        }
    }

    fn destination(&self, components: &[String]) -> PathBuf {
        components
            .iter()
            .fold(self.root.to_path_buf(), |path, part| path.join(part))
    }

    fn register(&mut self, components: &[String]) -> Result<(), TranslateError> {
        let shown = components.join("/");
        if let Some(previous) = self.seen.insert(case_key(components), shown.clone()) {
            return Err(TranslateError::Unsupported(format!(
                "archive entries {previous} and {shown} differ only by case, which Windows cannot store"
            )));
        }
        Ok(())
    }

    fn extract_entries(&mut self, filesystem: &FilesystemReader<'_>) -> Result<(), TranslateError> {
        for (index, node) in filesystem.files().enumerate() {
            if index >= self.limits.max_entries {
                return Err(TranslateError::LimitExceeded(format!(
                    "more than {} archive entries",
                    self.limits.max_entries
                )));
            }
            let components = safe_components(&node.fullpath)?;
            if components.is_empty() {
                continue;
            }
            self.register(&components)?;
            match &node.inner {
                InnerNode::Dir(_) => self.make_directory(&components)?,
                InnerNode::File(reader) => {
                    self.write_file(
                        &components,
                        reader.file_len() as u64,
                        filesystem.file(reader).reader(),
                    )?;
                }
                InnerNode::Symlink(link) => self.links.push((components, link.link.clone())),
                _ => self
                    .report
                    .skipped
                    .push(format!("{}: special file", components.join("/"))),
            }
        }
        Ok(())
    }

    fn make_directory(&mut self, components: &[String]) -> Result<(), TranslateError> {
        let path = self.destination(components);
        fs::create_dir_all(&path)
            .map_err(|e| io_error(format!("creating {}", path.display()), e))?;
        self.directories.insert(case_key(components));
        self.report.directories += 1;
        Ok(())
    }

    fn write_file(
        &mut self,
        components: &[String],
        size: u64,
        mut source: impl Read,
    ) -> Result<(), TranslateError> {
        let shown = components.join("/");
        if size > self.limits.max_file_bytes {
            return Err(TranslateError::LimitExceeded(format!(
                "{shown} is larger than {} bytes",
                self.limits.max_file_bytes
            )));
        }
        self.report.total_bytes += size;
        if self.report.total_bytes > self.limits.max_total_bytes {
            return Err(TranslateError::LimitExceeded(format!(
                "archive expands beyond {} bytes",
                self.limits.max_total_bytes
            )));
        }
        let path = self.destination(components);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)
                .map_err(|e| io_error(format!("creating {}", parent.display()), e))?;
        }
        let mut output = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&path)
            .map_err(|e| io_error(format!("creating {}", path.display()), e))?;
        let copied = std::io::copy(&mut (&mut source).take(size + 1), &mut output)
            .map_err(|e| io_error(format!("extracting {shown}"), e))?;
        if copied != size {
            return Err(TranslateError::Elf(format!(
                "SquashFS: {shown} is {copied} bytes, its inode says {size}"
            )));
        }
        self.files.insert(case_key(components), components.to_vec());
        self.report.files += 1;
        Ok(())
    }

    /// Windows cannot create symlinks without privileges, so each file link becomes a copy of its
    /// target. Links that leave the archive, dangle, or point at directories are reported and skipped.
    fn materialize_links(&mut self) -> Result<(), TranslateError> {
        let links = std::mem::take(&mut self.links);
        let by_key: HashMap<String, (Vec<String>, PathBuf)> = links
            .iter()
            .map(|(c, t)| (case_key(c), (c.clone(), t.clone())))
            .collect();
        for (components, target) in &links {
            match self.final_file(components, target, &by_key) {
                Ok(source) => self.copy_link(components, &source)?,
                Err(reason) => self
                    .report
                    .skipped
                    .push(format!("{}: {reason}", components.join("/"))),
            }
        }
        Ok(())
    }

    fn final_file(
        &self,
        link: &[String],
        target: &Path,
        links: &HashMap<String, (Vec<String>, PathBuf)>,
    ) -> Result<Vec<String>, &'static str> {
        let mut current = (link.to_vec(), target.to_path_buf());
        for _ in 0..MAX_LINK_HOPS {
            let resolved = resolve_link(&current.0, &current.1)
                .ok_or("link leaves the archive or is absolute")?;
            let key = case_key(&resolved);
            if let Some(next) = links.get(&key) {
                current = next.clone();
            } else if self.files.contains_key(&key) {
                return Ok(self.files[&key].clone());
            } else if self.directories.contains(&key) {
                return Err("link to a directory");
            } else {
                return Err("dangling link");
            }
        }
        Err("too many chained links")
    }

    fn copy_link(&mut self, link: &[String], source: &[String]) -> Result<(), TranslateError> {
        let to = self.destination(link);
        let from = self.destination(source);
        if let Some(parent) = to.parent() {
            fs::create_dir_all(parent)
                .map_err(|e| io_error(format!("creating {}", parent.display()), e))?;
        }
        let bytes = fs::copy(&from, &to)
            .map_err(|e| io_error(format!("copying {} to {}", from.display(), to.display()), e))?;
        self.report.total_bytes += bytes;
        if self.report.total_bytes > self.limits.max_total_bytes {
            return Err(TranslateError::LimitExceeded(
                "links expand the archive beyond the size limit".into(),
            ));
        }
        self.files.insert(case_key(link), link.to_vec());
        self.link_origin.insert(case_key(link), source.to_vec());
        self.report.links_materialized += 1;
        Ok(())
    }

    /// `AppRun` must be, or resolve to, an ELF program. Shell-script launchers need a shell, which
    /// Crossport does not provide, so they are refused with a clear message.
    fn check_apprun(&self) -> Result<Vec<String>, TranslateError> {
        let apprun = self.destination(&[APPRUN.to_string()]);
        let mut magic = [0u8; 4];
        let read = File::open(&apprun).and_then(|mut file| file.read_exact(&mut magic));
        if read.is_err() {
            return Err(TranslateError::Unsupported(
                "the AppImage has no usable AppRun".into(),
            ));
        }
        if magic != ELF_MAGIC {
            let reason = if magic[..2] == *b"#!" {
                "AppRun is a script, and scripts need a shell"
            } else {
                "AppRun is not an ELF program"
            };
            return Err(TranslateError::Unsupported(reason.into()));
        }
        Ok(self.apprun_source())
    }

    fn apprun_source(&self) -> Vec<String> {
        let apprun = vec![APPRUN.to_string()];
        self.link_origin
            .get(&case_key(&apprun))
            .cloned()
            .unwrap_or(apprun)
    }
}
