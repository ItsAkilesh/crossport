// File: installer/src/appimage_meta.rs
// Purpose: Read an AppImage's display name and icon straight from its SquashFS, without unpacking it
// Author: Akilesh
// Date: 2026-11-05

use std::collections::HashMap;
use std::fs::File;
use std::io::{BufReader, Read};
use std::path::Path;

use backhand::{FilesystemReader, InnerNode};

const ELF_MAGIC: [u8; 4] = *b"\x7fELF";
const HEADER_BYTES: usize = 64;
const E_SHOFF: usize = 0x28;
const E_SHENTSIZE: usize = 0x3A;
const E_SHNUM: usize = 0x3C;
const MAX_ICON_BYTES: usize = 4 * 1024 * 1024;
const PNG_SIGNATURE: [u8; 8] = [0x89, b'P', b'N', b'G', 0x0D, 0x0A, 0x1A, 0x0A];
const MAX_LINKS: usize = 8;
const MAX_ICON_SIDE: u32 = 256;

/// What a launcher can show before converting an AppImage.
#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct AppMeta {
    pub name: Option<String>,
    pub icon_png: Option<Vec<u8>>,
}

/// Where the SquashFS starts: the end of the ELF runtime's section header table.
pub fn squashfs_start(header: &[u8]) -> Option<u64> {
    if header.len() < HEADER_BYTES || header[..4] != ELF_MAGIC {
        return None;
    }
    let u16_at = |at: usize| u64::from(u16::from_le_bytes([header[at], header[at + 1]]));
    let mut table = [0u8; 8];
    table.copy_from_slice(&header[E_SHOFF..E_SHOFF + 8]);
    u64::from_le_bytes(table).checked_add(u16_at(E_SHENTSIZE) * u16_at(E_SHNUM))
}

/// The `Name` and `Icon` of a desktop entry (the unlocalised ones in the `[Desktop Entry]` group).
pub fn parse_desktop(text: &str) -> (Option<String>, Option<String>) {
    let (mut name, mut icon, mut in_entry) = (None, None, false);
    for line in text.lines().map(str::trim) {
        if line.starts_with('[') {
            in_entry = line == "[Desktop Entry]";
        } else if in_entry {
            if let Some(value) = line.strip_prefix("Name=") {
                name.get_or_insert_with(|| value.trim().to_string());
            } else if let Some(value) = line.strip_prefix("Icon=") {
                icon.get_or_insert_with(|| value.trim().to_string());
            }
        }
    }
    (name.filter(|n| !n.is_empty()), icon.filter(|i| !i.is_empty()))
}

fn read_limited(reader: impl Read) -> Result<Vec<u8>, String> {
    let mut data = Vec::new();
    reader
        .take(MAX_ICON_BYTES as u64 + 1)
        .read_to_end(&mut data)
        .map_err(|e| e.to_string())?;
    if data.len() > MAX_ICON_BYTES {
        return Err("entry is too large".into());
    }
    Ok(data)
}

type Node = backhand::Node<backhand::SquashfsFileReader>;

/// The archive's entries by path, with symlinks followed.
struct Tree<'a, 'b> {
    fs: &'a FilesystemReader<'b>,
    nodes: HashMap<String, &'a Node>,
}

impl<'a, 'b> Tree<'a, 'b> {
    fn new(fs: &'a FilesystemReader<'b>) -> Self {
        let nodes = fs.files().map(|n| (n.fullpath.to_string_lossy().replace('\\', "/"), n)).collect();
        Tree { fs, nodes }
    }

    /// The bytes of the file at `path`, following up to `MAX_LINKS` symlinks.
    fn read(&self, path: &str) -> Option<Vec<u8>> {
        let mut current = path.to_string();
        for _ in 0..MAX_LINKS {
            match &self.nodes.get(&current)?.inner {
                InnerNode::File(reader) => return read_limited(self.fs.file(reader).reader()).ok(),
                InnerNode::Symlink(link) => current = join_link(&current, &link.link.to_string_lossy()),
                _ => return None,
            }
        }
        None
    }

    fn root_with_extension(&self, extension: &str) -> Option<&str> {
        let mut found: Vec<&str> = self
            .nodes
            .keys()
            .map(String::as_str)
            .filter(|p| p.matches('/').count() == 1 && p.ends_with(extension))
            .collect();
        found.sort_unstable();
        found.first().copied()
    }

    /// The largest PNG (at most 256 pixels) called `<icon>.png` in the icon theme folders, else `.DirIcon`.
    fn icon(&self, icon: Option<&str>) -> Option<Vec<u8>> {
        let wanted = icon.map(|i| format!("/{i}.png"));
        let mut best: Option<(u32, &str)> = None;
        for path in self.nodes.keys().map(String::as_str) {
            let matches_name = wanted.as_ref().is_some_and(|w| path.ends_with(w.as_str()));
            let side = path.split('/').find_map(side_of).unwrap_or(0);
            if matches_name && side <= MAX_ICON_SIDE && best.is_none_or(|(s, _)| side > s) {
                best = Some((side, path));
            }
        }
        let from_theme = best.and_then(|(_, p)| self.read(p));
        from_theme.or_else(|| self.read("/.DirIcon")).filter(|d| d.starts_with(&PNG_SIGNATURE))
    }
}

fn side_of(folder: &str) -> Option<u32> {
    let (w, h) = folder.split_once('x')?;
    let (w, h) = (w.parse::<u32>().ok()?, h.parse::<u32>().ok()?);
    (w == h).then_some(w)
}

fn join_link(from: &str, link: &str) -> String {
    if let Some(absolute) = link.strip_prefix('/') {
        return format!("/{absolute}");
    }
    let mut parts: Vec<&str> = from.split('/').collect();
    parts.pop();
    for part in link.split('/') {
        match part {
            "." | "" => {}
            ".." => {
                if parts.len() > 1 {
                    parts.pop();
                }
            }
            other => parts.push(other),
        }
    }
    parts.join("/")
}

/// Read the name and icon of an AppImage. Anything unreadable is left out rather than failing.
pub fn read(image: &Path) -> Result<AppMeta, String> {
    let mut header = vec![0u8; HEADER_BYTES];
    File::open(image).and_then(|mut f| f.read_exact(&mut header)).map_err(|e| e.to_string())?;
    let offset = squashfs_start(&header).ok_or("not an AppImage")?;
    let file = File::open(image).map_err(|e| e.to_string())?;
    let fs = FilesystemReader::from_reader_with_offset(BufReader::new(file), offset).map_err(|e| e.to_string())?;
    let tree = Tree::new(&fs);
    let text = tree.root_with_extension(".desktop").and_then(|p| tree.read(p));
    let (name, icon_name) = text.map(|t| parse_desktop(&String::from_utf8_lossy(&t))).unwrap_or_default();
    Ok(AppMeta { name, icon_png: tree.icon(icon_name.as_deref()) })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn finds_the_end_of_the_runtime() {
        let mut header = vec![0u8; HEADER_BYTES];
        header[..4].copy_from_slice(&ELF_MAGIC);
        header[E_SHOFF..E_SHOFF + 8].copy_from_slice(&1000u64.to_le_bytes());
        header[E_SHENTSIZE..E_SHENTSIZE + 2].copy_from_slice(&64u16.to_le_bytes());
        header[E_SHNUM..E_SHNUM + 2].copy_from_slice(&3u16.to_le_bytes());
        assert_eq!(squashfs_start(&header), Some(1192));
        assert_eq!(squashfs_start(&header[..10]), None);
        header[0] = 0;
        assert_eq!(squashfs_start(&header), None);
    }

    #[test]
    fn parses_the_desktop_entry_group_only() {
        let text = "[Desktop Entry]\nName=KeePassXC\nName[de]=X\nIcon=keepassxc\n[Desktop Action a]\nName=Other\n";
        assert_eq!(parse_desktop(text), (Some("KeePassXC".into()), Some("keepassxc".into())));
        assert_eq!(parse_desktop("[Other]\nName=No\n"), (None, None));
        assert_eq!(parse_desktop("[Desktop Entry]\nName=\n"), (None, None));
    }

    #[test]
    fn joins_relative_and_absolute_links() {
        assert_eq!(join_link("/a.desktop", "usr/share/x.desktop"), "/usr/share/x.desktop");
        assert_eq!(join_link("/usr/share/a", "../b"), "/usr/b");
        assert_eq!(join_link("/a", "/usr/b"), "/usr/b");
        assert_eq!(join_link("/a", "../../../b"), "/b");
    }

    #[test]
    fn reads_icon_folder_sizes() {
        assert_eq!(side_of("256x256"), Some(256));
        assert_eq!(side_of("scalable"), None);
        assert_eq!(side_of("16x32"), None);
    }

    #[test]
    fn rejects_files_that_are_not_appimages() {
        let dir = std::env::temp_dir().join(format!("crossport-meta-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let file = dir.join("plain.AppImage");
        std::fs::write(&file, vec![0u8; 200]).unwrap();
        assert!(read(&file).is_err());
        std::fs::remove_dir_all(&dir).unwrap();
    }
}
