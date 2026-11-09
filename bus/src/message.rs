// File: bus/src/message.rs
// Purpose: A D-Bus message: parse its header, read its string arguments, and build the replies, errors and signals the bus sends
// Author: Akilesh
// Date: 2026-11-09

use crate::wire::{align_up, Reader, WireError, Writer, ENDIAN_LITTLE};

pub const METHOD_CALL: u8 = 1;
pub const METHOD_RETURN: u8 = 2;
pub const ERROR: u8 = 3;
pub const SIGNAL: u8 = 4;
pub const FLAG_NO_REPLY_EXPECTED: u8 = 1;
pub const PROTOCOL_VERSION: u8 = 1;
/// The fixed part of a header: endianness, type, flags, version, body length, serial, fields length.
pub const FIXED_HEADER: usize = 16;
/// The biggest message accepted (the specification allows 128 MiB; no toolkit comes near this).
pub const MAX_MESSAGE: usize = 64 * 1024 * 1024;

const FIELD_PATH: u8 = 1;
const FIELD_INTERFACE: u8 = 2;
const FIELD_MEMBER: u8 = 3;
const FIELD_ERROR_NAME: u8 = 4;
const FIELD_REPLY_SERIAL: u8 = 5;
const FIELD_DESTINATION: u8 = 6;
const FIELD_SENDER: u8 = 7;
const FIELD_SIGNATURE: u8 = 8;

/// The header fields the bus reads or writes. Others (such as file descriptor counts) are dropped on the way through.
#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct Fields {
    pub path: Option<String>,
    pub interface: Option<String>,
    pub member: Option<String>,
    pub error_name: Option<String>,
    pub reply_serial: Option<u32>,
    pub destination: Option<String>,
    pub sender: Option<String>,
    pub signature: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Message {
    pub kind: u8,
    pub flags: u8,
    pub serial: u32,
    pub fields: Fields,
    raw: Vec<u8>,
    body_start: usize,
}

/// The size of the whole message, from its fixed header, so the reader knows how much more to read.
pub fn total_length(fixed: &[u8; FIXED_HEADER]) -> Result<usize, WireError> {
    if fixed[0] != ENDIAN_LITTLE {
        return Err(WireError("only little-endian messages are supported"));
    }
    if fixed[3] != PROTOCOL_VERSION {
        return Err(WireError("an unknown protocol version"));
    }
    let body = u32::from_le_bytes([fixed[4], fixed[5], fixed[6], fixed[7]]) as usize;
    let fields = u32::from_le_bytes([fixed[12], fixed[13], fixed[14], fixed[15]]) as usize;
    let total = align_up(FIXED_HEADER.saturating_add(fields), 8).saturating_add(body);
    if total > MAX_MESSAGE {
        return Err(WireError("the message is too large"));
    }
    Ok(total)
}

impl Message {
    pub fn parse(raw: Vec<u8>) -> Result<Self, WireError> {
        let fixed: [u8; FIXED_HEADER] = raw
            .get(..FIXED_HEADER)
            .and_then(|bytes| bytes.try_into().ok())
            .ok_or(WireError("a message is shorter than its header"))?;
        let total = total_length(&fixed)?;
        if total != raw.len() {
            return Err(WireError("a message's length does not match its header"));
        }
        let fields_length =
            u32::from_le_bytes([fixed[12], fixed[13], fixed[14], fixed[15]]) as usize;
        let mut reader = Reader::new(&raw, FIXED_HEADER);
        let fields = read_fields(&mut reader, FIXED_HEADER + fields_length)?;
        let message = Self {
            kind: fixed[1],
            flags: fixed[2],
            serial: u32::from_le_bytes([fixed[8], fixed[9], fixed[10], fixed[11]]),
            fields,
            body_start: align_up(FIXED_HEADER + fields_length, 8),
            raw,
        };
        message.check()?;
        Ok(message)
    }

    fn check(&self) -> Result<(), WireError> {
        let needs = |present: bool, what: &'static str| {
            if present {
                Ok(())
            } else {
                Err(WireError(what))
            }
        };
        if self.serial == 0 {
            return Err(WireError("a serial of zero"));
        }
        match self.kind {
            METHOD_CALL => needs(
                self.fields.path.is_some() && self.fields.member.is_some(),
                "a call without path or member",
            ),
            METHOD_RETURN => needs(
                self.fields.reply_serial.is_some(),
                "a reply without a reply serial",
            ),
            ERROR => needs(
                self.fields.reply_serial.is_some() && self.fields.error_name.is_some(),
                "an error without a reply serial or name",
            ),
            SIGNAL => needs(
                self.fields.path.is_some()
                    && self.fields.interface.is_some()
                    && self.fields.member.is_some(),
                "a signal without path, interface or member",
            ),
            _ => Ok(()),
        }
    }

    pub fn bytes(&self) -> &[u8] {
        &self.raw
    }

    pub fn body(&self) -> &[u8] {
        &self.raw[self.body_start..]
    }

    pub fn needs_reply(&self) -> bool {
        self.kind == METHOD_CALL && self.flags & FLAG_NO_REPLY_EXPECTED == 0
    }

    /// The message with the bus's `sender` in place of whatever the client wrote there.
    pub fn with_sender(&self, sender: &str) -> Self {
        let fields = Fields {
            sender: Some(sender.to_string()),
            ..self.fields.clone()
        };
        build(self.kind, self.flags, self.serial, fields, self.body())
    }

    /// The `index`th argument when it is an unsigned 32-bit number (a boolean is one on the wire: 0 or 1).
    pub fn u32_argument(&self, index: usize) -> Option<u32> {
        let mut reader = Reader::new(self.body(), 0);
        let mut rest = self.fields.signature.as_bytes();
        for _ in 0..index {
            rest = reader.skip(rest).ok()?;
        }
        matches!(rest.first(), Some(b'u' | b'b'))
            .then(|| reader.u32().ok())
            .flatten()
    }

    /// The `index`th argument when it is a string, object path or signature and every argument before it can be skipped.
    pub fn string_argument(&self, index: usize) -> Option<String> {
        let signature = self.fields.signature.as_bytes();
        let body = self.body();
        let mut reader = Reader::new(body, 0);
        let mut rest = signature;
        for _ in 0..index {
            rest = reader.skip(rest).ok()?;
        }
        match rest.first()? {
            b's' | b'o' => reader.string().ok().map(str::to_string),
            b'g' => reader.signature().ok().map(str::to_string),
            _ => None,
        }
    }
}

fn read_fields(reader: &mut Reader<'_>, end: usize) -> Result<Fields, WireError> {
    let mut fields = Fields::default();
    while reader.position() < end {
        reader.align(8)?;
        if reader.position() >= end {
            break;
        }
        let code = reader.u8()?;
        let variant = reader.signature()?;
        match (code, variant) {
            (FIELD_PATH, "o") => fields.path = Some(reader.string()?.to_string()),
            (FIELD_INTERFACE, "s") => fields.interface = Some(reader.string()?.to_string()),
            (FIELD_MEMBER, "s") => fields.member = Some(reader.string()?.to_string()),
            (FIELD_ERROR_NAME, "s") => fields.error_name = Some(reader.string()?.to_string()),
            (FIELD_REPLY_SERIAL, "u") => fields.reply_serial = Some(reader.u32()?),
            (FIELD_DESTINATION, "s") => fields.destination = Some(reader.string()?.to_string()),
            (FIELD_SENDER, "s") => fields.sender = Some(reader.string()?.to_string()),
            (FIELD_SIGNATURE, "g") => fields.signature = reader.signature()?.to_string(),
            (_, other) => {
                reader.skip(other.as_bytes())?;
            }
        }
    }
    if reader.position() != end {
        return Err(WireError("the header fields' length is wrong"));
    }
    Ok(fields)
}

fn write_string_field(writer: &mut Writer, code: u8, signature: &str, value: Option<&String>) {
    if let Some(text) = value {
        writer.align(8);
        writer.u8(code);
        writer.signature(signature);
        writer.string(text);
    }
}

/// A whole message: the fixed header, the fields, padding to 8 bytes, and the body.
pub fn encode(kind: u8, flags: u8, serial: u32, fields: &Fields, body: &[u8]) -> Vec<u8> {
    let mut writer = Writer::new();
    writer.u8(ENDIAN_LITTLE);
    writer.u8(kind);
    writer.u8(flags);
    writer.u8(PROTOCOL_VERSION);
    writer.u32(u32::try_from(body.len()).unwrap_or(u32::MAX));
    writer.u32(serial);
    let length_at = writer.len();
    writer.u32(0);
    write_string_field(&mut writer, FIELD_PATH, "o", fields.path.as_ref());
    write_string_field(&mut writer, FIELD_INTERFACE, "s", fields.interface.as_ref());
    write_string_field(&mut writer, FIELD_MEMBER, "s", fields.member.as_ref());
    write_string_field(
        &mut writer,
        FIELD_ERROR_NAME,
        "s",
        fields.error_name.as_ref(),
    );
    if let Some(serial) = fields.reply_serial {
        writer.align(8);
        writer.u8(FIELD_REPLY_SERIAL);
        writer.signature("u");
        writer.u32(serial);
    }
    write_string_field(
        &mut writer,
        FIELD_DESTINATION,
        "s",
        fields.destination.as_ref(),
    );
    write_string_field(&mut writer, FIELD_SENDER, "s", fields.sender.as_ref());
    if !fields.signature.is_empty() {
        writer.align(8);
        writer.u8(FIELD_SIGNATURE);
        writer.signature("g");
        writer.signature(&fields.signature);
    }
    let fields_length = writer.len() - FIXED_HEADER;
    writer.patch_u32(length_at, u32::try_from(fields_length).unwrap_or(u32::MAX));
    writer.align(8);
    writer.raw(body);
    writer.into_bytes()
}

/// The body of a message under construction: arguments written in order.
#[derive(Default)]
pub struct Body {
    writer: Writer,
    signature: String,
}

impl Body {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn string(mut self, text: &str) -> Self {
        self.writer.string(text);
        self.signature.push('s');
        self
    }

    pub fn u32(mut self, value: u32) -> Self {
        self.writer.u32(value);
        self.signature.push('u');
        self
    }

    pub fn boolean(mut self, value: bool) -> Self {
        self.writer.u32(u32::from(value));
        self.signature.push('b');
        self
    }

    pub fn strings(mut self, items: &[String]) -> Self {
        let length_at = self.writer.len();
        self.writer.u32(0);
        let start = self.writer.len();
        for item in items {
            self.writer.string(item);
        }
        let length = self.writer.len() - start;
        self.writer
            .patch_u32(length_at, u32::try_from(length).unwrap_or(u32::MAX));
        self.signature.push_str("as");
        self
    }

    pub fn finish(self) -> (String, Vec<u8>) {
        (self.signature, self.writer.into_bytes())
    }
}

/// What the bus puts in the headers of a message it makes itself.
pub const BUS_NAME: &str = "org.freedesktop.DBus";
pub const BUS_PATH: &str = "/org/freedesktop/DBus";

pub fn method_return(serial: u32, reply_to: &Message, destination: &str, body: Body) -> Message {
    let (signature, bytes) = body.finish();
    let fields = Fields {
        reply_serial: Some(reply_to.serial),
        destination: Some(destination.to_string()),
        sender: Some(BUS_NAME.to_string()),
        signature,
        ..Fields::default()
    };
    build(
        METHOD_RETURN,
        FLAG_NO_REPLY_EXPECTED,
        serial,
        fields,
        &bytes,
    )
}

pub fn error(
    serial: u32,
    reply_to: &Message,
    destination: &str,
    name: &str,
    text: &str,
) -> Message {
    let (signature, bytes) = Body::new().string(text).finish();
    let fields = Fields {
        error_name: Some(name.to_string()),
        reply_serial: Some(reply_to.serial),
        destination: Some(destination.to_string()),
        sender: Some(BUS_NAME.to_string()),
        signature,
        ..Fields::default()
    };
    build(ERROR, FLAG_NO_REPLY_EXPECTED, serial, fields, &bytes)
}

/// A signal from the bus itself; `destination` makes it a unicast one.
pub fn signal(serial: u32, destination: Option<&str>, member: &str, body: Body) -> Message {
    let (signature, bytes) = body.finish();
    let fields = Fields {
        path: Some(BUS_PATH.to_string()),
        interface: Some(BUS_NAME.to_string()),
        member: Some(member.to_string()),
        destination: destination.map(str::to_string),
        sender: Some(BUS_NAME.to_string()),
        signature,
        ..Fields::default()
    };
    build(SIGNAL, FLAG_NO_REPLY_EXPECTED, serial, fields, &bytes)
}

fn build(kind: u8, flags: u8, serial: u32, fields: Fields, body: &[u8]) -> Message {
    let raw = encode(kind, flags, serial, &fields, body);
    // Everything encoded here parses back: the fields are the ones the parser reads.
    Message::parse(raw.clone()).unwrap_or(Message {
        kind,
        flags,
        serial,
        fields,
        raw,
        body_start: 0,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn call(member: &str, body: Body) -> Message {
        let (signature, bytes) = body.finish();
        let fields = Fields {
            path: Some("/a".into()),
            interface: Some("x.Y".into()),
            member: Some(member.into()),
            destination: Some("org.example.Service".into()),
            signature,
            ..Fields::default()
        };
        Message::parse(encode(METHOD_CALL, 0, 7, &fields, &bytes)).expect("parses")
    }

    #[test]
    fn a_message_parses_back_into_what_was_encoded() {
        let message = call("Do", Body::new().string("one").u32(2));
        assert_eq!(message.kind, METHOD_CALL);
        assert_eq!(message.serial, 7);
        assert_eq!(message.fields.member.as_deref(), Some("Do"));
        assert_eq!(message.fields.signature, "su");
        assert!(message.needs_reply());
    }

    #[test]
    fn string_arguments_are_found_after_skipping_earlier_ones() {
        let message = call(
            "Do",
            Body::new()
                .u32(5)
                .strings(&["a".into(), "b".into()])
                .string("third"),
        );
        assert_eq!(message.string_argument(2).as_deref(), Some("third"));
        assert_eq!(message.string_argument(0), None, "argument 0 is a number");
        assert_eq!(message.string_argument(9), None);
    }

    #[test]
    fn the_sender_is_replaced_and_the_body_is_untouched() {
        let message = call("Do", Body::new().string("payload"));
        let forwarded = message.with_sender(":1.5");
        assert_eq!(forwarded.fields.sender.as_deref(), Some(":1.5"));
        assert_eq!(forwarded.body(), message.body());
        assert_eq!(forwarded.serial, message.serial);
        assert_eq!(forwarded.body_start % 8, 0, "the body stays 8-aligned");
    }

    #[test]
    fn a_client_cannot_choose_its_own_sender() {
        let fields = Fields {
            path: Some("/".into()),
            interface: Some("a.B".into()),
            member: Some("S".into()),
            sender: Some(":1.99".into()),
            ..Fields::default()
        };
        let message = Message::parse(encode(SIGNAL, 0, 1, &fields, &[])).expect("parses");
        assert_eq!(
            message.with_sender(":1.2").fields.sender.as_deref(),
            Some(":1.2")
        );
    }

    #[test]
    fn a_wrong_length_or_missing_field_is_refused() {
        let mut bytes = encode(METHOD_CALL, 0, 1, &Fields::default(), &[]);
        assert!(
            Message::parse(bytes.clone()).is_err(),
            "a call without path and member"
        );
        bytes.push(0);
        assert!(Message::parse(bytes).is_err(), "trailing bytes");
        assert!(Message::parse(vec![b'B'; 16]).is_err(), "big endian");
    }

    #[test]
    fn a_serial_of_zero_is_refused() {
        let fields = Fields {
            path: Some("/".into()),
            member: Some("M".into()),
            ..Fields::default()
        };
        assert!(Message::parse(encode(METHOD_CALL, 0, 0, &fields, &[])).is_err());
    }

    #[test]
    fn unknown_header_fields_are_skipped() {
        let message = call("Do", Body::new());
        let mut raw = message.bytes().to_vec();
        // Add a field 200 of type "u" at the end of the fields and fix the length.
        let fields_length = u32::from_le_bytes([raw[12], raw[13], raw[14], raw[15]]) as usize;
        let at = align_up(FIXED_HEADER + fields_length, 8);
        let mut extra = Writer::new();
        extra.u8(200);
        extra.signature("u");
        extra.u32(1);
        let extra = extra.into_bytes();
        let body = message.body().to_vec();
        raw.truncate(FIXED_HEADER + fields_length);
        raw.resize(at, 0);
        raw.extend_from_slice(&extra);
        let new_fields = raw.len() - FIXED_HEADER;
        raw[12..16].copy_from_slice(&u32::try_from(new_fields).expect("small").to_le_bytes());
        raw.resize(align_up(raw.len(), 8), 0);
        raw.extend_from_slice(&body);
        let parsed = Message::parse(raw).expect("parses");
        assert_eq!(parsed.fields.member.as_deref(), Some("Do"));
    }

    #[test]
    fn replies_and_signals_made_by_the_bus_carry_its_name() {
        let request = call("Hello", Body::new());
        let reply = method_return(1, &request, ":1.1", Body::new().string(":1.1"));
        assert_eq!(reply.fields.reply_serial, Some(7));
        assert_eq!(reply.fields.sender.as_deref(), Some(BUS_NAME));
        let failure = error(2, &request, ":1.1", "x.Error", "no");
        assert_eq!(failure.fields.error_name.as_deref(), Some("x.Error"));
        let broadcast = signal(
            3,
            None,
            "NameOwnerChanged",
            Body::new().string("a").string("").string(":1.1"),
        );
        assert_eq!(broadcast.fields.destination, None);
    }
}
