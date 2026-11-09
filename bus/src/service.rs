// File: bus/src/service.rs
// Purpose: The bus's own methods: org.freedesktop.DBus (names, matches, ids), org.freedesktop.DBus.Peer and Introspectable
// Author: Akilesh
// Date: 2026-11-09

use crate::bus::{Bus, Effect, ERROR_SERVICE_UNKNOWN};
use crate::matching::MatchRule;
use crate::message::{self, Body, Message, BUS_NAME, METHOD_CALL};
use crate::names::{is_well_known, ReleaseResult, RequestResult};

const ERROR_UNKNOWN_METHOD: &str = "org.freedesktop.DBus.Error.UnknownMethod";
const ERROR_INVALID_ARGS: &str = "org.freedesktop.DBus.Error.InvalidArgs";
const ERROR_NAME_HAS_NO_OWNER: &str = "org.freedesktop.DBus.Error.NameHasNoOwner";
const ERROR_MATCH_INVALID: &str = "org.freedesktop.DBus.Error.MatchRuleInvalid";
const ERROR_MATCH_NOT_FOUND: &str = "org.freedesktop.DBus.Error.MatchRuleNotFound";
const ERROR_LIMITS: &str = "org.freedesktop.DBus.Error.LimitsExceeded";
const ERROR_PID_UNKNOWN: &str = "org.freedesktop.DBus.Error.UnixProcessIdUnknown";
/// The user id answered for every connection: the bus runs for one user, and the shim reports a fixed one.
const USER_ID: u32 = 1000;
const MAX_RULES: usize = 512;
const MACHINE_ID: &str = "0123456789abcdef0123456789abcdef";
const INTROSPECTION: &str = "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" \
\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n<node>\n <interface name=\"org.freedesktop.DBus\">\n \
</interface>\n</node>\n";

enum Answer {
    Return(Body),
    Failure(&'static str, String),
}

impl Bus {
    /// Carries out a call addressed to the bus (or to nobody) and sends the reply.
    pub(crate) fn call_bus(&mut self, id: u64, message: &Message) {
        if message.kind != METHOD_CALL {
            return;
        }
        let answer = self.answer(id, message);
        if !message.needs_reply() {
            self.flush_effects(id);
            return;
        }
        let Some(unique) = self.unique_of(id).map(str::to_string) else {
            return;
        };
        let serial = self.next_serial();
        let reply = match answer {
            Answer::Return(body) => message::method_return(serial, message, &unique, body),
            Answer::Failure(name, text) => message::error(serial, message, &unique, name, &text),
        };
        self.send_new(id, reply);
        self.flush_effects(id);
    }

    fn answer(&mut self, id: u64, message: &Message) -> Answer {
        let member = message.fields.member.as_deref().unwrap_or_default();
        match (message.fields.interface.as_deref(), member) {
            (Some("org.freedesktop.DBus.Peer"), "Ping") => Answer::Return(Body::new()),
            (Some("org.freedesktop.DBus.Peer"), "GetMachineId") => {
                Answer::Return(Body::new().string(MACHINE_ID))
            }
            (Some("org.freedesktop.DBus.Introspectable"), "Introspect") => {
                Answer::Return(Body::new().string(INTROSPECTION))
            }
            (Some("org.freedesktop.DBus") | None, _) => self.answer_bus(id, message, member),
            _ => unknown(member),
        }
    }

    fn answer_bus(&mut self, id: u64, message: &Message, member: &str) -> Answer {
        let name = message.string_argument(0).unwrap_or_default();
        match member {
            "Hello" => self.hello(id),
            "RequestName" => self.request_name(id, &name, message.u32_argument(1).unwrap_or(0)),
            "ReleaseName" => self.release_name(id, &name),
            "NameHasOwner" => Answer::Return(Body::new().boolean(self.has_owner(&name))),
            "GetNameOwner" if self.has_owner(&name) => {
                Answer::Return(Body::new().string(&self.owner_name(&name)))
            }
            "GetNameOwner" => Answer::Failure(
                ERROR_NAME_HAS_NO_OWNER,
                format!("Could not get owner of name '{name}': no such name"),
            ),
            "ListNames" => Answer::Return(Body::new().strings(&self.list_names())),
            "ListActivatableNames" => Answer::Return(Body::new().strings(&[])),
            "ListQueuedOwners" => Answer::Return(Body::new().strings(&self.queued_owners(&name))),
            "StartServiceByName" => Answer::Failure(
                ERROR_SERVICE_UNKNOWN,
                format!("The name {name} was not provided by any service files"),
            ),
            "AddMatch" => self.add_match(id, &name),
            "RemoveMatch" => self.remove_match(id, &name),
            "GetId" => Answer::Return(Body::new().string(&self.guid)),
            "GetConnectionUnixUser" if self.has_owner(&name) => {
                Answer::Return(Body::new().u32(USER_ID))
            }
            "GetConnectionUnixProcessID" if self.has_owner(&name) => Answer::Failure(
                ERROR_PID_UNKNOWN,
                "The process of a connection is not known".to_string(),
            ),
            "GetConnectionUnixUser" | "GetConnectionUnixProcessID" => Answer::Failure(
                ERROR_NAME_HAS_NO_OWNER,
                format!("Could not get owner of name '{name}': no such name"),
            ),
            "UpdateActivationEnvironment" | "ReloadConfig" => Answer::Return(Body::new()),
            _ => unknown(member),
        }
    }

    fn hello(&mut self, id: u64) -> Answer {
        if self.unique_of(id).is_some() {
            return Answer::Failure(
                ERROR_INVALID_ARGS,
                "Already handled an Hello message".to_string(),
            );
        }
        let Some(unique) = self.assign_unique(id) else {
            return Answer::Failure(ERROR_INVALID_ARGS, "The connection is gone".to_string());
        };
        self.effects.push(Effect::Hello(unique.clone()));
        Answer::Return(Body::new().string(&unique))
    }

    fn request_name(&mut self, id: u64, name: &str, flags: u32) -> Answer {
        if !is_well_known(name) || name.starts_with(BUS_NAME) {
            return Answer::Failure(
                ERROR_INVALID_ARGS,
                format!("Requested bus name \"{name}\" is not valid or reserved"),
            );
        }
        let (result, changes) = self.names.request(name, id, flags);
        self.effects
            .extend(changes.into_iter().map(Effect::Changed));
        Answer::Return(Body::new().u32(match result {
            RequestResult::PrimaryOwner => 1,
            RequestResult::InQueue => 2,
            RequestResult::Exists => 3,
            RequestResult::AlreadyOwner => 4,
        }))
    }

    fn release_name(&mut self, id: u64, name: &str) -> Answer {
        if !is_well_known(name) {
            return Answer::Failure(
                ERROR_INVALID_ARGS,
                format!("Given bus name \"{name}\" is not valid"),
            );
        }
        let (result, changes) = self.names.release(name, id);
        self.effects
            .extend(changes.into_iter().map(Effect::Changed));
        Answer::Return(Body::new().u32(match result {
            ReleaseResult::Released => 1,
            ReleaseResult::NonExistent => 2,
            ReleaseResult::NotOwner => 3,
        }))
    }

    fn has_owner(&self, name: &str) -> bool {
        name == BUS_NAME || self.resolve(name).is_some()
    }

    fn owner_name(&self, name: &str) -> String {
        if name == BUS_NAME {
            return BUS_NAME.to_string();
        }
        self.resolve(name)
            .and_then(|owner| self.unique_of(owner))
            .unwrap_or_default()
            .to_string()
    }

    fn list_names(&self) -> Vec<String> {
        let mut names = vec![BUS_NAME.to_string()];
        let mut unique: Vec<String> = self
            .connections
            .values()
            .filter_map(|c| c.unique.clone())
            .collect();
        unique.sort();
        names.extend(unique);
        names.extend(self.names.all_names());
        names
    }

    fn queued_owners(&self, name: &str) -> Vec<String> {
        self.names
            .queue_of(name)
            .into_iter()
            .filter_map(|owner| self.unique_of(owner).map(str::to_string))
            .collect()
    }

    fn add_match(&mut self, id: u64, text: &str) -> Answer {
        let rule = match MatchRule::parse(text) {
            Ok(rule) => rule,
            Err(reason) => {
                return Answer::Failure(
                    ERROR_MATCH_INVALID,
                    format!("Invalid match rule: {reason}"),
                )
            }
        };
        match self.connections.get_mut(&id) {
            Some(connection) if connection.rules.len() >= MAX_RULES => Answer::Failure(
                ERROR_LIMITS,
                "The connection has too many match rules".to_string(),
            ),
            Some(connection) => {
                connection.rules.push(rule);
                Answer::Return(Body::new())
            }
            None => Answer::Return(Body::new()),
        }
    }

    fn remove_match(&mut self, id: u64, text: &str) -> Answer {
        let Ok(rule) = MatchRule::parse(text) else {
            return Answer::Failure(ERROR_MATCH_INVALID, "Invalid match rule".to_string());
        };
        let removed = self.connections.get_mut(&id).and_then(|connection| {
            let position = connection
                .rules
                .iter()
                .position(|existing| *existing == rule)?;
            connection.rules.remove(position);
            Some(())
        });
        match removed {
            Some(()) => Answer::Return(Body::new()),
            None => Answer::Failure(
                ERROR_MATCH_NOT_FOUND,
                "The match rule was not added by this connection".to_string(),
            ),
        }
    }
}

fn unknown(member: &str) -> Answer {
    Answer::Failure(
        ERROR_UNKNOWN_METHOD,
        format!("The bus has no method {member}"),
    )
}
