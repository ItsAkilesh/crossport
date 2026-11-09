// File: bus/src/bus.rs
// Purpose: The bus itself: connections, who is who, and routing every message to the connection that should get it
// Author: Akilesh
// Date: 2026-11-09

use std::collections::HashMap;
use std::sync::Arc;

use crate::matching::MatchRule;
use crate::message::{self, Body, Message, BUS_NAME, ERROR, METHOD_CALL, METHOD_RETURN, SIGNAL};
use crate::names::{Change, ConnectionId, Registry};

pub const ERROR_SERVICE_UNKNOWN: &str = "org.freedesktop.DBus.Error.ServiceUnknown";
pub const ERROR_NO_REPLY: &str = "org.freedesktop.DBus.Error.NoReply";
pub const ERROR_ACCESS_DENIED: &str = "org.freedesktop.DBus.Error.AccessDenied";

/// Where a connection's messages go. `deliver` says whether the connection took the message: false means it is
/// closed or too slow, and the bus drops it, as a real bus does.
pub trait Outbox: Send {
    fn deliver(&self, message: Arc<Message>) -> bool;
}

pub(crate) struct Connection {
    pub(crate) unique: Option<String>,
    pub(crate) outbox: Box<dyn Outbox>,
    pub(crate) rules: Vec<MatchRule>,
}

/// What a bus call causes besides its reply, done after the reply is sent (a client sees the reply first).
pub(crate) enum Effect {
    Hello(String),
    Changed(Change),
}

/// A method call waiting for its reply, so a dying service can be answered for and a forged reply refused.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Pending {
    caller: ConnectionId,
    serial: u32,
    callee: ConnectionId,
}

pub struct Bus {
    pub(crate) connections: HashMap<ConnectionId, Connection>,
    pub(crate) names: Registry,
    pub(crate) guid: String,
    next_connection: ConnectionId,
    next_unique: u64,
    serial: u32,
    pending: Vec<Pending>,
    /// Connections whose outbox refused a message; closed once the current routing is done.
    doomed: Vec<ConnectionId>,
    pub(crate) effects: Vec<Effect>,
}

impl Bus {
    pub fn new(guid: String) -> Self {
        Self {
            connections: HashMap::new(),
            names: Registry::default(),
            guid,
            next_connection: 1,
            next_unique: 1,
            serial: 0,
            pending: Vec::new(),
            doomed: Vec::new(),
            effects: Vec::new(),
        }
    }

    pub fn connect(&mut self, outbox: Box<dyn Outbox>) -> ConnectionId {
        let id = self.next_connection;
        self.next_connection += 1;
        self.connections.insert(
            id,
            Connection {
                unique: None,
                outbox,
                rules: Vec::new(),
            },
        );
        id
    }

    pub fn connections_count(&self) -> usize {
        self.connections.len()
    }

    pub(crate) fn next_serial(&mut self) -> u32 {
        self.serial = self.serial.wrapping_add(1).max(1);
        self.serial
    }

    pub(crate) fn assign_unique(&mut self, id: ConnectionId) -> Option<String> {
        let unique = format!(":1.{}", self.next_unique);
        self.next_unique += 1;
        self.connections.get_mut(&id)?.unique = Some(unique.clone());
        Some(unique)
    }

    pub(crate) fn unique_of(&self, id: ConnectionId) -> Option<&str> {
        self.connections.get(&id)?.unique.as_deref()
    }

    pub(crate) fn find_unique(&self, name: &str) -> Option<ConnectionId> {
        self.connections
            .iter()
            .find(|(_, c)| c.unique.as_deref() == Some(name))
            .map(|(&id, _)| id)
    }

    /// The connection a destination names: a unique name, or whoever owns a well-known one.
    pub(crate) fn resolve(&self, name: &str) -> Option<ConnectionId> {
        if name.starts_with(':') {
            self.find_unique(name)
        } else {
            self.names.owner_of(name)
        }
    }

    /// Every name a connection answers to, for `sender=` in match rules.
    fn sender_names(&self, id: ConnectionId) -> Vec<String> {
        let mut names: Vec<String> = self.unique_of(id).map(str::to_string).into_iter().collect();
        names.extend(self.names.names_of(id));
        names
    }

    fn send(&mut self, to: ConnectionId, message: Arc<Message>) {
        let is_taken = self
            .connections
            .get(&to)
            .is_some_and(|connection| connection.outbox.deliver(message));
        if !is_taken && self.connections.contains_key(&to) {
            self.doomed.push(to);
        }
    }

    pub(crate) fn send_new(&mut self, to: ConnectionId, message: Message) {
        self.send(to, Arc::new(message));
    }

    /// A signal from the bus to everyone whose match rules want it.
    pub(crate) fn broadcast_from_bus(&mut self, member: &str, body: Body) {
        let serial = self.next_serial();
        let signal = Arc::new(message::signal(serial, None, member, body));
        let sender = vec![BUS_NAME.to_string()];
        let wanting: Vec<ConnectionId> = self
            .connections
            .iter()
            .filter(|(_, c)| {
                c.unique.is_some() && c.rules.iter().any(|rule| rule.matches(&signal, &sender))
            })
            .map(|(&id, _)| id)
            .collect();
        for id in wanting {
            self.send(id, Arc::clone(&signal));
        }
    }

    pub(crate) fn unicast_from_bus(&mut self, to: ConnectionId, member: &str, body: Body) {
        let Some(unique) = self.unique_of(to).map(str::to_string) else {
            return;
        };
        let serial = self.next_serial();
        self.send_new(to, message::signal(serial, Some(&unique), member, body));
    }

    /// The signals a change of name owner causes: the broadcast, and the notices to the two connections involved.
    pub(crate) fn announce(&mut self, change: &Change) {
        let name_of = |bus: &Self, id: Option<ConnectionId>| {
            id.and_then(|id| bus.unique_of(id))
                .unwrap_or_default()
                .to_string()
        };
        let (old, new) = (name_of(self, change.old), name_of(self, change.new));
        let body = Body::new().string(&change.name).string(&old).string(&new);
        self.broadcast_from_bus("NameOwnerChanged", body);
        if let Some(old_owner) = change.old.filter(|old| Some(*old) != change.new) {
            self.unicast_from_bus(old_owner, "NameLost", Body::new().string(&change.name));
        }
        if let Some(new_owner) = change.new.filter(|new| Some(*new) != change.old) {
            self.unicast_from_bus(new_owner, "NameAcquired", Body::new().string(&change.name));
        }
    }

    /// Sends the signals the last bus call caused.
    pub(crate) fn flush_effects(&mut self, caller: ConnectionId) {
        for effect in std::mem::take(&mut self.effects) {
            match effect {
                Effect::Hello(unique) => {
                    let announcement = Body::new().string(&unique).string("").string(&unique);
                    self.broadcast_from_bus("NameOwnerChanged", announcement);
                    self.unicast_from_bus(caller, "NameAcquired", Body::new().string(&unique));
                }
                Effect::Changed(change) => self.announce(&change),
            }
        }
    }

    pub fn disconnect(&mut self, id: ConnectionId) {
        let Some(connection) = self.connections.remove(&id) else {
            return;
        };
        let changes = self.names.drop_connection(id);
        if let Some(unique) = connection.unique {
            let body = Body::new().string(&unique).string(&unique).string("");
            self.broadcast_from_bus("NameOwnerChanged", body);
        }
        for change in changes {
            self.announce(&change);
        }
        self.fail_pending_for(id);
        self.close_doomed();
    }

    /// Answers calls that were waiting on a connection that has gone, and forgets calls it made.
    fn fail_pending_for(&mut self, gone: ConnectionId) {
        self.pending.retain(|pending| pending.caller != gone);
        let waiting: Vec<Pending> = self
            .pending
            .iter()
            .filter(|p| p.callee == gone)
            .copied()
            .collect();
        self.pending.retain(|pending| pending.callee != gone);
        for call in waiting {
            let Some(unique) = self.unique_of(call.caller).map(str::to_string) else {
                continue;
            };
            let serial = self.next_serial();
            let request = message_with_serial(call.serial);
            let failure = message::error(
                serial,
                &request,
                &unique,
                ERROR_NO_REPLY,
                "The connection that was asked went away",
            );
            self.send_new(call.caller, failure);
        }
    }

    fn close_doomed(&mut self) {
        while let Some(id) = self.doomed.pop() {
            self.disconnect(id);
        }
    }

    /// Takes a message from a connection. Returns false when the connection must be closed.
    pub fn handle(&mut self, id: ConnectionId, message: Message) -> bool {
        let has_hello = self.unique_of(id).is_some();
        let is_hello = message.kind == METHOD_CALL
            && message.fields.member.as_deref() == Some("Hello")
            && message.fields.destination.as_deref() == Some(BUS_NAME);
        if !has_hello && !is_hello {
            return false;
        }
        let sender = self.unique_of(id).map(str::to_string).unwrap_or_default();
        let message = if has_hello {
            message.with_sender(&sender)
        } else {
            message
        };
        match message.fields.destination.clone() {
            Some(destination) if destination != BUS_NAME => self.route(id, message, &destination),
            Some(_) => self.call_bus(id, &message),
            None if message.kind == SIGNAL => self.broadcast(id, message),
            None => self.call_bus(id, &message),
        }
        self.close_doomed();
        self.connections.contains_key(&id)
    }

    fn route(&mut self, from: ConnectionId, message: Message, destination: &str) {
        let target = self.resolve(destination);
        match (target, message.kind) {
            (Some(to), METHOD_CALL) => {
                if message.needs_reply() {
                    self.pending.push(Pending {
                        caller: from,
                        serial: message.serial,
                        callee: to,
                    });
                }
                self.send_new(to, message);
            }
            (Some(to), METHOD_RETURN | ERROR) => {
                let reply_serial = message.fields.reply_serial.unwrap_or(0);
                let before = self.pending.len();
                self.pending
                    .retain(|p| !(p.caller == to && p.callee == from && p.serial == reply_serial));
                if self.pending.len() != before {
                    self.send_new(to, message);
                }
            }
            (Some(to), SIGNAL) => self.send_new(to, message),
            (None, METHOD_CALL) if message.needs_reply() => {
                let unique = self.unique_of(from).unwrap_or_default().to_string();
                let serial = self.next_serial();
                let text = format!("The name {destination} is not owned by anyone");
                self.send_new(
                    from,
                    message::error(serial, &message, &unique, ERROR_SERVICE_UNKNOWN, &text),
                );
            }
            _ => {}
        }
    }

    fn broadcast(&mut self, from: ConnectionId, message: Message) {
        let names = self.sender_names(from);
        let wanting: Vec<ConnectionId> = self
            .connections
            .iter()
            .filter(|(_, c)| {
                c.unique.is_some() && c.rules.iter().any(|rule| rule.matches(&message, &names))
            })
            .map(|(&id, _)| id)
            .collect();
        let shared = Arc::new(message);
        for id in wanting {
            self.send(id, Arc::clone(&shared));
        }
    }
}

/// A stand-in for the call an error answers: only its serial is read.
fn message_with_serial(serial: u32) -> Message {
    let fields = message::Fields {
        path: Some("/".to_string()),
        member: Some("Call".to_string()),
        ..message::Fields::default()
    };
    let raw = message::encode(METHOD_CALL, 0, serial.max(1), &fields, &[]);
    // A call with a path, a member and a nonzero serial always parses.
    Message::parse(raw).expect("a call with a path and member parses")
}
