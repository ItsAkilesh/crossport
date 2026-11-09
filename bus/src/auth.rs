// File: bus/src/auth.rs
// Purpose: The server side of the D-Bus authentication handshake (EXTERNAL and ANONYMOUS, no file descriptor passing)
// Author: Akilesh
// Date: 2026-11-09

use std::io::{self, BufRead, Read, Write};

/// A line longer than this is not a handshake line.
const MAX_LINE: u64 = 4096;
const MECHANISMS: &str = "EXTERNAL ANONYMOUS";

/// Runs the handshake. The client may try mechanisms in any order; the connection is trusted because it already
/// proved it knows the nonce, so EXTERNAL and ANONYMOUS are accepted whatever identity they carry.
/// Returns `Ok(true)` once the client sends BEGIN, `Ok(false)` when it hangs up first.
pub fn handshake<R: BufRead, W: Write>(
    reader: &mut R,
    writer: &mut W,
    guid: &str,
) -> io::Result<bool> {
    let mut zero = [0u8; 1];
    if reader.read(&mut zero)? == 0 {
        return Ok(false);
    }
    if zero[0] != 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "the stream does not start with a NUL byte",
        ));
    }
    let mut is_authenticated = false;
    loop {
        let mut line = String::new();
        let read = reader.by_ref().take(MAX_LINE).read_line(&mut line)?;
        if read == 0 {
            return Ok(false);
        }
        if !line.ends_with('\n') {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "a handshake line is too long or unfinished",
            ));
        }
        let mut words = line.trim_end().split(' ');
        match words.next().unwrap_or_default() {
            "AUTH" => {
                is_authenticated = matches!(words.next(), Some("EXTERNAL" | "ANONYMOUS"));
                if is_authenticated {
                    write!(writer, "OK {guid}\r\n")?;
                } else {
                    write!(writer, "REJECTED {MECHANISMS}\r\n")?;
                }
            }
            "NEGOTIATE_UNIX_FD" => write!(
                writer,
                "ERROR \"Unix file descriptor passing is not supported\"\r\n"
            )?,
            "CANCEL" => {
                is_authenticated = false;
                write!(writer, "REJECTED {MECHANISMS}\r\n")?;
            }
            "BEGIN" if is_authenticated => return Ok(true),
            "BEGIN" => {
                return Err(io::Error::new(
                    io::ErrorKind::PermissionDenied,
                    "BEGIN before authentication",
                ))
            }
            "ERROR" => {}
            _ => write!(writer, "ERROR\r\n")?,
        }
        writer.flush()?;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    fn run(input: &[u8]) -> (io::Result<bool>, String) {
        let mut reader = Cursor::new(input.to_vec());
        let mut output = Vec::new();
        let result = handshake(&mut reader, &mut output, "abc");
        (result, String::from_utf8(output).expect("text"))
    }

    #[test]
    fn external_with_a_uid_is_accepted_and_begin_ends_the_handshake() {
        let (result, output) = run(b"\0AUTH EXTERNAL 31303030\r\nNEGOTIATE_UNIX_FD\r\nBEGIN\r\n");
        assert!(result.expect("ok"));
        assert!(output.starts_with("OK abc\r\n"), "{output}");
        assert!(output.contains("ERROR"), "fd passing is refused");
    }

    #[test]
    fn a_bare_auth_lists_the_mechanisms_and_anonymous_is_accepted_after() {
        let (result, output) = run(b"\0AUTH\r\nAUTH ANONYMOUS 74657374\r\nBEGIN\r\n");
        assert!(result.expect("ok"));
        assert_eq!(output, "REJECTED EXTERNAL ANONYMOUS\r\nOK abc\r\n");
    }

    #[test]
    fn a_mechanism_that_is_not_offered_is_rejected() {
        let (result, output) = run(b"\0AUTH DBUS_COOKIE_SHA1 31303030\r\n");
        assert!(!result.expect("closed"));
        assert_eq!(output, "REJECTED EXTERNAL ANONYMOUS\r\n");
    }

    #[test]
    fn begin_before_authenticating_and_a_missing_nul_are_errors() {
        assert!(run(b"\0BEGIN\r\n").0.is_err());
        assert!(run(b"AUTH EXTERNAL\r\n").0.is_err());
    }

    #[test]
    fn a_line_without_an_end_or_that_is_too_long_is_an_error() {
        assert!(run(b"\0AUTH EXTERNAL 3130").0.is_err());
        let mut long = b"\0AUTH ".to_vec();
        long.extend(std::iter::repeat_n(b'a', 5000));
        assert!(run(&long).0.is_err());
    }

    #[test]
    fn hanging_up_early_is_not_an_error() {
        assert!(!run(b"").0.expect("ok"));
        assert!(!run(b"\0").0.expect("ok"));
    }
}
