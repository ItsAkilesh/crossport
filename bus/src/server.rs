// File: bus/src/server.rs
// Purpose: The transport: listen on loopback TCP behind a nonce, run each connection's reader and writer, and exit when idle
// Author: Akilesh
// Date: 2026-11-09

use std::collections::hash_map::RandomState;
use std::fs::{self, File};
use std::hash::{BuildHasher, Hasher};
use std::io::{self, BufReader, Read, Write};
use std::net::{Ipv4Addr, Shutdown, TcpListener, TcpStream};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::mpsc::{sync_channel, SyncSender};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

use crate::auth;
use crate::bus::{Bus, Outbox};
use crate::message::{total_length, Message, FIXED_HEADER};

const NONCE_BYTES: usize = 16;
/// A client that does not finish the nonce and handshake in this long is dropped.
const HANDSHAKE_TIMEOUT: Duration = Duration::from_secs(10);
const MAX_CONNECTIONS: usize = 256;
/// Messages waiting for a slow client; past this the client is disconnected, as a real bus does.
const OUTGOING_QUEUE: usize = 1024;
const ACCEPT_POLL: Duration = Duration::from_millis(50);

pub struct Config {
    /// Where the nonce is written (the daemon's view of the path).
    pub nonce_file: PathBuf,
    /// The nonce file's path as clients see it, for the address; defaults to `nonce_file`.
    pub advertised_nonce_file: Option<String>,
    pub address_file: Option<PathBuf>,
    /// Exit when no client has been connected for this long (and after the same time if none ever connects).
    pub idle_exit: Option<Duration>,
    pub verbose: bool,
}

/// 16 random bytes. `RandomState` is seeded from the operating system's random source for every new value, and
/// SipHash is a keyed function, so what it returns for different inputs cannot be guessed without the keys.
pub fn random_bytes() -> [u8; NONCE_BYTES] {
    let mut bytes = [0u8; NONCE_BYTES];
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.as_nanos());
    for (index, chunk) in bytes.chunks_mut(8).enumerate() {
        let mut hasher = RandomState::new().build_hasher();
        hasher.write_u128(now);
        hasher.write_u32(std::process::id());
        hasher.write_usize(index);
        chunk.copy_from_slice(&hasher.finish().to_le_bytes());
    }
    bytes
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

/// A D-Bus address value: letters, digits and `-_/.\*` as they are, everything else as %XX.
pub fn escape_address_value(text: &str) -> String {
    let mut escaped = String::new();
    for byte in text.bytes() {
        if byte.is_ascii_alphanumeric() || b"-_/.\\*".contains(&byte) {
            escaped.push(char::from(byte));
        } else {
            escaped.push_str(&format!("%{byte:02x}"));
        }
    }
    escaped
}

fn write_private(path: &Path, contents: &[u8]) -> io::Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    let temporary = path.with_extension("new");
    let mut file = File::create(&temporary)?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        file.set_permissions(fs::Permissions::from_mode(0o600))?;
    }
    file.write_all(contents)?;
    file.sync_all()?;
    fs::rename(&temporary, path)
}

struct Socket(SyncSender<Arc<Message>>);

impl Outbox for Socket {
    fn deliver(&self, message: Arc<Message>) -> bool {
        self.0.try_send(message).is_ok()
    }
}

fn is_same(expected: &[u8], actual: &[u8]) -> bool {
    expected.len() == actual.len()
        && expected
            .iter()
            .zip(actual)
            .fold(0u8, |acc, (a, b)| acc | (a ^ b))
            == 0
}

fn read_message(reader: &mut impl Read) -> io::Result<Option<Message>> {
    let mut fixed = [0u8; FIXED_HEADER];
    match reader.read_exact(&mut fixed) {
        Ok(()) => {}
        Err(error) if error.kind() == io::ErrorKind::UnexpectedEof => return Ok(None),
        Err(error) => return Err(error),
    }
    let invalid = |error| io::Error::new(io::ErrorKind::InvalidData, error);
    let total = total_length(&fixed).map_err(invalid)?;
    let mut raw = vec![0u8; total];
    raw[..FIXED_HEADER].copy_from_slice(&fixed);
    reader.read_exact(&mut raw[FIXED_HEADER..])?;
    Message::parse(raw).map(Some).map_err(invalid)
}

fn write_messages(mut stream: TcpStream, messages: std::sync::mpsc::Receiver<Arc<Message>>) {
    for message in messages {
        if stream.write_all(message.bytes()).is_err() {
            break;
        }
    }
    let _ = stream.shutdown(Shutdown::Both);
}

fn serve(
    stream: TcpStream,
    bus: &Mutex<Bus>,
    nonce: [u8; NONCE_BYTES],
    guid: &str,
    verbose: bool,
) -> io::Result<()> {
    stream.set_read_timeout(Some(HANDSHAKE_TIMEOUT))?;
    stream.set_nodelay(true)?;
    let mut reader = BufReader::new(stream.try_clone()?);
    let mut writer = stream.try_clone()?;
    let mut offered = [0u8; NONCE_BYTES];
    reader.read_exact(&mut offered)?;
    if !is_same(&nonce, &offered) {
        return Err(io::Error::new(
            io::ErrorKind::PermissionDenied,
            "the nonce is wrong",
        ));
    }
    if !auth::handshake(&mut reader, &mut writer, guid)? {
        return Ok(());
    }
    stream.set_read_timeout(None)?;
    let (sender, receiver) = sync_channel(OUTGOING_QUEUE);
    let id = bus
        .lock()
        .map_err(poisoned)?
        .connect(Box::new(Socket(sender)));
    let write_half = stream.try_clone()?;
    thread::spawn(move || write_messages(write_half, receiver));
    let result = loop {
        match read_message(&mut reader) {
            Ok(Some(message)) => {
                if !bus.lock().map_err(poisoned)?.handle(id, message) {
                    break Ok(());
                }
            }
            Ok(None) => break Ok(()),
            Err(error) => break Err(error),
        }
    };
    if let Ok(mut locked) = bus.lock() {
        locked.disconnect(id);
    }
    let _ = stream.shutdown(Shutdown::Both);
    if verbose {
        eprintln!("crossport-bus: connection {id} closed");
    }
    result
}

fn poisoned<T>(_: T) -> io::Error {
    io::Error::other("the bus state is poisoned")
}

/// Binds, writes the nonce and address files, calls `announce` with the address, and serves until idle.
pub fn run(config: &Config, announce: impl FnOnce(&str)) -> io::Result<()> {
    let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0))?;
    listener.set_nonblocking(true)?;
    let port = listener.local_addr()?.port();
    let nonce = random_bytes();
    write_private(&config.nonce_file, &nonce)?;
    let advertised = config
        .advertised_nonce_file
        .clone()
        .unwrap_or_else(|| config.nonce_file.display().to_string());
    let address = format!(
        "nonce-tcp:host=127.0.0.1,port={port},noncefile={}",
        escape_address_value(&advertised)
    );
    if let Some(path) = &config.address_file {
        write_private(path, address.as_bytes())?;
    }
    announce(&address);
    let guid = hex(&random_bytes());
    let bus = Arc::new(Mutex::new(Bus::new(guid.clone())));
    let live = Arc::new(AtomicUsize::new(0));
    let mut idle_since = Instant::now();
    loop {
        match listener.accept() {
            Ok((stream, _)) => {
                // Windows hands the listener's non-blocking mode on to the accepted socket.
                if live.load(Ordering::SeqCst) >= MAX_CONNECTIONS
                    || stream.set_nonblocking(false).is_err()
                {
                    continue;
                }
                live.fetch_add(1, Ordering::SeqCst);
                let (bus, live, guid, verbose) = (
                    Arc::clone(&bus),
                    Arc::clone(&live),
                    guid.clone(),
                    config.verbose,
                );
                thread::spawn(move || {
                    if let Err(error) = serve(stream, &bus, nonce, &guid, verbose) {
                        if verbose {
                            eprintln!("crossport-bus: a connection ended: {error}");
                        }
                    }
                    live.fetch_sub(1, Ordering::SeqCst);
                });
            }
            Err(error) if error.kind() == io::ErrorKind::WouldBlock => thread::sleep(ACCEPT_POLL),
            Err(error) => return Err(error),
        }
        if live.load(Ordering::SeqCst) > 0 {
            idle_since = Instant::now();
        } else if config
            .idle_exit
            .is_some_and(|limit| idle_since.elapsed() >= limit)
        {
            return Ok(());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn address_values_escape_everything_but_the_safe_characters() {
        assert_eq!(
            escape_address_value("/run/user/1000/bus"),
            "/run/user/1000/bus"
        );
        assert_eq!(escape_address_value("C:\\Temp\\a b"), "C%3a\\Temp\\a%20b");
    }

    #[test]
    fn nonces_differ_and_compare_in_full() {
        assert_ne!(random_bytes(), random_bytes());
        let nonce = random_bytes();
        let mut wrong = nonce;
        wrong[15] ^= 1;
        assert!(is_same(&nonce, &nonce));
        assert!(!is_same(&nonce, &wrong));
        assert!(!is_same(&nonce, &nonce[..8]));
    }

    #[test]
    fn a_message_is_read_whole_and_end_of_stream_is_not_an_error() {
        use crate::message::{encode, Fields, METHOD_CALL};
        let fields = Fields {
            path: Some("/".into()),
            member: Some("M".into()),
            ..Fields::default()
        };
        let raw = encode(METHOD_CALL, 0, 3, &fields, &[]);
        let mut stream = io::Cursor::new(raw.clone());
        assert_eq!(
            read_message(&mut stream)
                .expect("ok")
                .expect("a message")
                .bytes(),
            raw.as_slice()
        );
        assert!(read_message(&mut stream).expect("ok").is_none());
        assert!(read_message(&mut io::Cursor::new(vec![b'B'; 20])).is_err());
    }
}
