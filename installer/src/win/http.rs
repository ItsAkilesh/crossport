// File: installer/src/win/http.rs
// Purpose: One HTTPS GET through WinHTTP: allowlisted host, short timeouts, no redirects, capped body
// Author: Akilesh
// Date: 2026-11-05

use std::ffi::c_void;

use windows::core::{w, PCWSTR};
use windows::Win32::Networking::WinHttp::{
    WinHttpCloseHandle, WinHttpConnect, WinHttpOpen, WinHttpOpenRequest, WinHttpQueryHeaders,
    WinHttpReadData, WinHttpReceiveResponse, WinHttpSendRequest, WinHttpSetOption,
    WinHttpSetTimeouts, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_FLAG_SECURE,
    WINHTTP_OPTION_REDIRECT_POLICY, WINHTTP_OPTION_REDIRECT_POLICY_NEVER,
    WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_QUERY_STATUS_CODE,
};

use super::{pcwstr, wide};
use crate::update::split_https;

const TIMEOUT_MS: i32 = 8_000;
const HTTPS_PORT: u16 = 443;
const READ_CHUNK: usize = 16 * 1024;
const STATUS_OK: u32 = 200;
const USER_AGENT: &str = concat!("crossport-setup/", env!("CARGO_PKG_VERSION"));

/// A WinHTTP handle that is closed when dropped.
struct Handle(*mut c_void);

impl Handle {
    fn new(raw: *mut c_void, what: &str) -> Result<Handle, String> {
        if raw.is_null() {
            Err(format!(
                "could not {what}: {}",
                windows::core::Error::from_win32()
            ))
        } else {
            Ok(Handle(raw))
        }
    }
}

impl Drop for Handle {
    fn drop(&mut self) {
        // SAFETY: the handle came from a WinHTTP open call and is closed exactly once.
        unsafe {
            let _ = WinHttpCloseHandle(self.0);
        }
    }
}

fn describe(what: &str, error: windows::core::Error) -> String {
    format!("{what}: {error}")
}

/// GET `url` (https only, host must equal `allowed_host`) and return the body as text, at most `max_bytes` long.
pub fn get_text(url: &str, allowed_host: &str, max_bytes: usize) -> Result<String, String> {
    let (host, path) = split_https(url).ok_or("the address is not a plain https address")?;
    if host != allowed_host {
        return Err(format!("{host} is not an address Crossport contacts"));
    }
    let agent = wide(USER_AGENT);
    // SAFETY: every pointer passed below points into a buffer that outlives the call it is passed to.
    unsafe {
        let session = Handle::new(
            WinHttpOpen(
                pcwstr(&agent),
                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                PCWSTR::null(),
                PCWSTR::null(),
                0,
            ),
            "start a connection",
        )?;
        WinHttpSetTimeouts(session.0, TIMEOUT_MS, TIMEOUT_MS, TIMEOUT_MS, TIMEOUT_MS)
            .map_err(|e| describe("setting timeouts", e))?;
        let host_w = wide(host);
        let connection = Handle::new(
            WinHttpConnect(session.0, pcwstr(&host_w), HTTPS_PORT, 0),
            "connect",
        )?;
        let path_w = wide(path);
        let request = Handle::new(
            WinHttpOpenRequest(
                connection.0,
                w!("GET"),
                pcwstr(&path_w),
                PCWSTR::null(),
                PCWSTR::null(),
                std::ptr::null(),
                WINHTTP_FLAG_SECURE,
            ),
            "make the request",
        )?;
        let never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER.to_le_bytes();
        WinHttpSetOption(
            Some(request.0),
            WINHTTP_OPTION_REDIRECT_POLICY,
            Some(&never),
        )
        .map_err(|e| describe("refusing redirects", e))?;
        let headers = wide("Accept: application/vnd.github+json\r\n");
        WinHttpSendRequest(
            request.0,
            Some(&headers[..headers.len() - 1]),
            None,
            0,
            0,
            0,
        )
        .map_err(|e| describe("sending the request", e))?;
        WinHttpReceiveResponse(request.0, std::ptr::null_mut())
            .map_err(|e| describe("waiting for the answer", e))?;
        let status = status_code(&request)?;
        if status != STATUS_OK {
            return Err(format!("the server answered {status}"));
        }
        read_body(&request, max_bytes)
    }
}

/// SAFETY: `request` must be a live request handle whose response has been received.
unsafe fn status_code(request: &Handle) -> Result<u32, String> {
    let (mut status, mut size) = (0u32, std::mem::size_of::<u32>() as u32);
    WinHttpQueryHeaders(
        request.0,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        PCWSTR::null(),
        Some(&mut status as *mut u32 as *mut c_void),
        &mut size,
        std::ptr::null_mut(),
    )
    .map_err(|e| describe("reading the status", e))?;
    Ok(status)
}

/// SAFETY: `request` must be a live request handle whose response has been received.
unsafe fn read_body(request: &Handle, max_bytes: usize) -> Result<String, String> {
    let mut body = Vec::new();
    let mut chunk = vec![0u8; READ_CHUNK];
    loop {
        let mut read = 0u32;
        WinHttpReadData(
            request.0,
            chunk.as_mut_ptr() as *mut c_void,
            READ_CHUNK as u32,
            &mut read,
        )
        .map_err(|e| describe("reading the answer", e))?;
        if read == 0 {
            break;
        }
        body.extend_from_slice(&chunk[..read as usize]);
        if body.len() > max_bytes {
            return Err("the answer was larger than expected".to_string());
        }
    }
    String::from_utf8(body).map_err(|_| "the answer was not text".to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn refuses_other_hosts_and_schemes_before_connecting() {
        assert!(get_text("https://example.com/x", "api.github.com", 10)
            .unwrap_err()
            .contains("not an address"));
        assert!(get_text("http://api.github.com/x", "api.github.com", 10).is_err());
        assert!(get_text(
            "https://api.github.com@evil.example/x",
            "api.github.com",
            10
        )
        .is_err());
    }
}
