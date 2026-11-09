// File: bus/tests/tcp.rs
// Purpose: End-to-end test of the daemon over real loopback TCP: nonce, handshake, Hello, names and a call between two clients
// Author: Akilesh
// Date: 2026-11-09

use std::fs;
use std::io::{BufRead, BufReader, Read, Write};
use std::net::TcpStream;
use std::path::PathBuf;
use std::sync::mpsc;
use std::thread;
use std::time::Duration;

use crossport_bus::message::{
    encode, total_length, Body, Fields, Message, FIXED_HEADER, METHOD_CALL, METHOD_RETURN,
};
use crossport_bus::server::{run, Config};

struct Daemon {
    port: u16,
    nonce: Vec<u8>,
}

fn start(label: &str) -> Daemon {
    let directory =
        std::env::temp_dir().join(format!("crossport-bus-test-{label}-{}", std::process::id()));
    let nonce_file: PathBuf = directory.join("nonce");
    let config = Config {
        nonce_file: nonce_file.clone(),
        advertised_nonce_file: None,
        address_file: None,
        idle_exit: Some(Duration::from_secs(2)),
        verbose: false,
    };
    let (sender, receiver) = mpsc::channel();
    thread::spawn(move || {
        let _ = run(&config, |address| {
            let _ = sender.send(address.to_string());
        });
    });
    let address = receiver
        .recv_timeout(Duration::from_secs(10))
        .expect("the daemon announces its address");
    assert!(
        address.starts_with("nonce-tcp:host=127.0.0.1,port="),
        "{address}"
    );
    let port: u16 = address["nonce-tcp:host=127.0.0.1,port=".len()..]
        .split(',')
        .next()
        .and_then(|text| text.parse().ok())
        .expect("a port");
    let nonce = fs::read(&nonce_file).expect("the nonce file");
    assert_eq!(nonce.len(), 16);
    Daemon { port, nonce }
}

struct Client {
    stream: TcpStream,
    reader: BufReader<TcpStream>,
    serial: u32,
}

fn connect(daemon: &Daemon) -> Client {
    let mut stream = TcpStream::connect(("127.0.0.1", daemon.port)).expect("connects");
    stream
        .set_read_timeout(Some(Duration::from_secs(10)))
        .expect("timeout");
    stream.write_all(&daemon.nonce).expect("nonce");
    stream
        .write_all(b"\0AUTH EXTERNAL 31303030\r\nBEGIN\r\n")
        .expect("auth");
    let reader = BufReader::new(stream.try_clone().expect("clone"));
    let mut client = Client {
        stream,
        reader,
        serial: 0,
    };
    let mut line = String::new();
    client.reader.read_line(&mut line).expect("an OK line");
    assert!(line.starts_with("OK "), "{line}");
    client
}

impl Client {
    fn send(&mut self, destination: &str, interface: &str, member: &str, body: Body) -> u32 {
        self.serial += 1;
        let (signature, bytes) = body.finish();
        let fields = Fields {
            path: Some("/org/freedesktop/DBus".into()),
            interface: Some(interface.into()),
            member: Some(member.into()),
            destination: Some(destination.into()),
            signature,
            ..Fields::default()
        };
        self.stream
            .write_all(&encode(METHOD_CALL, 0, self.serial, &fields, &bytes))
            .expect("sends");
        self.serial
    }

    fn receive(&mut self) -> Message {
        let mut fixed = [0u8; FIXED_HEADER];
        self.reader.read_exact(&mut fixed).expect("a header");
        let mut raw = vec![0u8; total_length(&fixed).expect("a length")];
        raw[..FIXED_HEADER].copy_from_slice(&fixed);
        self.reader
            .read_exact(&mut raw[FIXED_HEADER..])
            .expect("a body");
        Message::parse(raw).expect("parses")
    }

    fn reply_to(&mut self, serial: u32) -> Message {
        loop {
            let message = self.receive();
            if message.fields.reply_serial == Some(serial) {
                return message;
            }
        }
    }

    fn hello(&mut self) -> String {
        let serial = self.send(
            "org.freedesktop.DBus",
            "org.freedesktop.DBus",
            "Hello",
            Body::new(),
        );
        self.reply_to(serial)
            .string_argument(0)
            .expect("a unique name")
    }
}

#[test]
fn two_clients_say_hello_take_a_name_and_call_each_other_over_tcp() {
    let daemon = start("calls");
    let mut service = connect(&daemon);
    let mut caller = connect(&daemon);
    let service_name = service.hello();
    let caller_name = caller.hello();
    assert_ne!(service_name, caller_name);
    let serial = service.send(
        "org.freedesktop.DBus",
        "org.freedesktop.DBus",
        "RequestName",
        Body::new().string("org.example.Echo").u32(0),
    );
    assert_eq!(service.reply_to(serial).u32_argument(0), Some(1));
    let call_serial = caller.send(
        "org.example.Echo",
        "org.example.Echo",
        "Say",
        Body::new().string("ping"),
    );
    let incoming = loop {
        let message = service.receive();
        if message.kind == METHOD_CALL {
            break message;
        }
    };
    assert_eq!(incoming.string_argument(0).as_deref(), Some("ping"));
    assert_eq!(
        incoming.fields.sender.as_deref(),
        Some(caller_name.as_str())
    );
    let (signature, body) = Body::new().string("pong").finish();
    let fields = Fields {
        reply_serial: Some(incoming.serial),
        destination: Some(caller_name),
        signature,
        ..Fields::default()
    };
    service
        .stream
        .write_all(&encode(METHOD_RETURN, 1, 90, &fields, &body))
        .expect("reply");
    assert_eq!(
        caller.reply_to(call_serial).string_argument(0).as_deref(),
        Some("pong")
    );
}

#[test]
fn a_client_with_the_wrong_nonce_is_refused() {
    let daemon = start("nonce");
    let mut stream = TcpStream::connect(("127.0.0.1", daemon.port)).expect("connects");
    stream
        .set_read_timeout(Some(Duration::from_secs(10)))
        .expect("timeout");
    stream.write_all(&[0u8; 16]).expect("wrong nonce");
    let _ = stream.write_all(b"\0AUTH EXTERNAL 31303030\r\nBEGIN\r\n");
    let mut answer = Vec::new();
    let _ = stream.read_to_end(&mut answer);
    assert!(answer.is_empty(), "no handshake answer for a wrong nonce");
}

#[test]
fn list_names_includes_the_bus_and_every_connection() {
    let daemon = start("list");
    let mut first = connect(&daemon);
    let mut second = connect(&daemon);
    let first_name = first.hello();
    let second_name = second.hello();
    let serial = first.send(
        "org.freedesktop.DBus",
        "org.freedesktop.DBus",
        "ListNames",
        Body::new(),
    );
    let reply = first.reply_to(serial);
    let text = String::from_utf8_lossy(reply.body()).to_string();
    for name in [
        "org.freedesktop.DBus",
        first_name.as_str(),
        second_name.as_str(),
    ] {
        assert!(text.contains(name), "{name} in {text}");
    }
}

#[test]
fn a_closed_connection_is_noticed_by_those_watching_names() {
    let daemon = start("gone");
    let mut watcher = connect(&daemon);
    let mut other = connect(&daemon);
    watcher.hello();
    let other_name = other.hello();
    let serial = watcher.send(
        "org.freedesktop.DBus",
        "org.freedesktop.DBus",
        "AddMatch",
        Body::new().string("type='signal',member='NameOwnerChanged'"),
    );
    watcher.reply_to(serial);
    drop(other);
    let notice = loop {
        let message = watcher.receive();
        if message.fields.member.as_deref() == Some("NameOwnerChanged")
            && message.string_argument(0).as_deref() == Some(other_name.as_str())
        {
            break message;
        }
    };
    assert_eq!(notice.string_argument(2).as_deref(), Some(""));
}
