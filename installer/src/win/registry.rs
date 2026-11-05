// File: installer/src/win/registry.rs
// Purpose: Write and delete the registry entries planned in regplan, and edit the PATH variable
// Author: Akilesh
// Date: 2026-11-05

#![allow(unsafe_code)]

use windows::Win32::Foundation::ERROR_FILE_NOT_FOUND;
use windows::Win32::Foundation::{LPARAM, WPARAM};
use windows::Win32::UI::WindowsAndMessaging::{
    SendMessageTimeoutW, HWND_BROADCAST, SMTO_ABORTIFHUNG, WM_SETTINGCHANGE,
};
use windows::Win32::System::Registry::{
    RegCloseKey, RegCreateKeyExW, RegDeleteTreeW, RegOpenKeyExW, RegQueryValueExW, RegSetValueExW,
    HKEY, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE, KEY_QUERY_VALUE, KEY_SET_VALUE, KEY_WRITE,
    REG_DWORD, REG_EXPAND_SZ, REG_OPTION_NON_VOLATILE, REG_SZ, REG_VALUE_TYPE,
};

use super::{pcwstr, wide};
use crate::regplan::{Hive, RegEntry, RegValue};

const BROADCAST_TIMEOUT_MS: u32 = 2000;

/// A registry writer. With a `redirect`, every key lands under that key in `HKCU`, so tests never touch
/// real classes or uninstall entries.
#[derive(Debug, Clone, Default)]
pub struct Registry {
    pub redirect: Option<String>,
}

fn root(hive: Hive) -> HKEY {
    match hive {
        Hive::CurrentUser => HKEY_CURRENT_USER,
        Hive::LocalMachine => HKEY_LOCAL_MACHINE,
    }
}

fn bytes(text: &str) -> Vec<u8> {
    wide(text).iter().flat_map(|unit| unit.to_le_bytes()).collect()
}

impl Registry {
    fn place(&self, hive: Hive, key: &str) -> (HKEY, String) {
        match &self.redirect {
            Some(prefix) => (HKEY_CURRENT_USER, format!(r"{prefix}\{}\{key}", hive.name())),
            None => (root(hive), key.to_string()),
        }
    }

    /// Write every entry, creating keys as needed.
    pub fn apply(&self, entries: &[RegEntry]) -> Result<(), String> {
        entries.iter().try_for_each(|entry| self.write(entry))
    }

    fn write(&self, entry: &RegEntry) -> Result<(), String> {
        let (hive, key) = self.place(entry.hive, &entry.key);
        let key_w = wide(&key);
        let name_w = entry.name.as_deref().map(wide);
        let name = name_w.as_deref().map_or(windows::core::PCWSTR::null(), pcwstr);
        let (kind, data): (REG_VALUE_TYPE, Vec<u8>) = match &entry.value {
            RegValue::Text(text) => (REG_SZ, bytes(text)),
            RegValue::Number(n) => (REG_DWORD, n.to_le_bytes().to_vec()),
        };
        // SAFETY: all pointers refer to live buffers; the key is closed before returning.
        unsafe {
            let mut handle = HKEY::default();
            RegCreateKeyExW(hive, pcwstr(&key_w), None, None, REG_OPTION_NON_VOLATILE, KEY_WRITE, None, &mut handle, None)
                .ok()
                .map_err(|e| format!("create {key}: {e}"))?;
            let result = RegSetValueExW(handle, name, None, kind, Some(&data));
            let _ = RegCloseKey(handle);
            result.ok().map_err(|e| format!("set {key}: {e}"))
        }
    }

    /// Delete a key and everything under it. A key that is already gone is not an error.
    pub fn delete_tree(&self, hive: Hive, key: &str) -> Result<(), String> {
        let (hive, key) = self.place(hive, key);
        let key_w = wide(&key);
        // SAFETY: the key name buffer outlives the call.
        let result = unsafe { RegDeleteTreeW(hive, pcwstr(&key_w)) };
        if result == ERROR_FILE_NOT_FOUND {
            return Ok(());
        }
        result.ok().map_err(|e| format!("delete {key}: {e}"))
    }

    /// Read a text value, or `None` when the key or value is missing.
    pub fn read_text(&self, hive: Hive, key: &str, name: &str) -> Option<String> {
        let (hive, key) = self.place(hive, key);
        let (key_w, name_w) = (wide(&key), wide(name));
        // SAFETY: buffers are live for the calls; the key is closed before returning.
        unsafe {
            let mut handle = HKEY::default();
            RegOpenKeyExW(hive, pcwstr(&key_w), None, KEY_QUERY_VALUE, &mut handle).ok().ok()?;
            let mut size = 0u32;
            let mut kind = REG_VALUE_TYPE::default();
            let probe = RegQueryValueExW(handle, pcwstr(&name_w), None, Some(&mut kind), None, Some(&mut size));
            let mut buffer = vec![0u8; size as usize];
            let read = probe.ok().ok().map(|_| {
                RegQueryValueExW(handle, pcwstr(&name_w), None, Some(&mut kind), Some(buffer.as_mut_ptr()), Some(&mut size))
            });
            let _ = RegCloseKey(handle);
            read?.ok().ok()?;
            let units: Vec<u16> = buffer[..size as usize]
                .as_chunks::<2>()
                .0
                .iter()
                .map(|pair| u16::from_le_bytes(*pair))
                .collect();
            Some(String::from_utf16_lossy(&units).trim_end_matches('\0').to_string())
        }
    }

    /// Store the PATH variable as an expandable string, then tell running programs about the change.
    pub fn write_path(&self, hive: Hive, key: &str, value: &str) -> Result<(), String> {
        let (hive, key) = self.place(hive, key);
        let (key_w, name_w) = (wide(&key), wide("Path"));
        let data = bytes(value);
        // SAFETY: buffers are live for the calls; the key is closed before returning.
        unsafe {
            let mut handle = HKEY::default();
            RegCreateKeyExW(hive, pcwstr(&key_w), None, None, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, None, &mut handle, None)
                .ok()
                .map_err(|e| format!("open {key}: {e}"))?;
            let result = RegSetValueExW(handle, pcwstr(&name_w), None, REG_EXPAND_SZ, Some(&data));
            let _ = RegCloseKey(handle);
            result.ok().map_err(|e| format!("set Path: {e}"))
        }
    }
}

/// Tell running programs (Explorer, new terminals) that environment variables changed.
pub fn announce_environment_change() {
    let setting = wide("Environment");
    // SAFETY: the setting string outlives the call; the broadcast has a short timeout so a hung window cannot stall it.
    unsafe {
        let _ = SendMessageTimeoutW(
            HWND_BROADCAST,
            WM_SETTINGCHANGE,
            WPARAM(0),
            LPARAM(setting.as_ptr() as isize),
            SMTO_ABORTIFHUNG,
            BROADCAST_TIMEOUT_MS,
            None,
        );
    }
}
