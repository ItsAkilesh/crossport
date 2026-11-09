// File: bus/src/names.rs
// Purpose: The well-known name registry: who owns a name, who waits for it, and what changes when owners come and go
// Author: Akilesh
// Date: 2026-11-09

use std::collections::{HashMap, VecDeque};

pub type ConnectionId = u64;

pub const FLAG_ALLOW_REPLACEMENT: u32 = 1;
pub const FLAG_REPLACE_EXISTING: u32 = 2;
pub const FLAG_DO_NOT_QUEUE: u32 = 4;

const MAX_NAME_LENGTH: usize = 255;

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum RequestResult {
    PrimaryOwner = 1,
    InQueue = 2,
    Exists = 3,
    AlreadyOwner = 4,
}

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum ReleaseResult {
    Released = 1,
    NonExistent = 2,
    NotOwner = 3,
}

/// A name changed hands (`None` is "nobody").
#[derive(Debug, PartialEq, Eq, Clone)]
pub struct Change {
    pub name: String,
    pub old: Option<ConnectionId>,
    pub new: Option<ConnectionId>,
}

#[derive(Debug, Clone, Copy)]
struct Holder {
    connection: ConnectionId,
    flags: u32,
}

#[derive(Debug)]
struct Entry {
    owner: Holder,
    queue: VecDeque<Holder>,
}

#[derive(Debug, Default)]
pub struct Registry {
    names: HashMap<String, Entry>,
}

/// A well-known name: dot-separated non-empty elements of letters, digits, `_` and `-` that do not start with a
/// digit, and does not start with `:` (those are unique names the bus hands out itself).
pub fn is_well_known(name: &str) -> bool {
    name.len() <= MAX_NAME_LENGTH
        && name.contains('.')
        && name.split('.').all(|element| {
            !element.is_empty()
                && !element.starts_with(|c: char| c.is_ascii_digit())
                && element
                    .chars()
                    .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-')
        })
}

impl Registry {
    pub fn request(
        &mut self,
        name: &str,
        connection: ConnectionId,
        flags: u32,
    ) -> (RequestResult, Vec<Change>) {
        let holder = Holder { connection, flags };
        let Some(entry) = self.names.get_mut(name) else {
            self.names.insert(
                name.to_string(),
                Entry {
                    owner: holder,
                    queue: VecDeque::new(),
                },
            );
            return (
                RequestResult::PrimaryOwner,
                vec![change(name, None, Some(connection))],
            );
        };
        if entry.owner.connection == connection {
            entry.owner.flags = flags;
            return (RequestResult::AlreadyOwner, Vec::new());
        }
        let can_replace =
            entry.owner.flags & FLAG_ALLOW_REPLACEMENT != 0 && flags & FLAG_REPLACE_EXISTING != 0;
        if can_replace {
            let old = entry.owner;
            entry.owner = holder;
            entry.queue.retain(|waiter| waiter.connection != connection);
            if old.flags & FLAG_DO_NOT_QUEUE == 0 {
                entry.queue.push_front(old);
            }
            return (
                RequestResult::PrimaryOwner,
                vec![change(name, Some(old.connection), Some(connection))],
            );
        }
        if flags & FLAG_DO_NOT_QUEUE != 0 {
            return (RequestResult::Exists, Vec::new());
        }
        if !entry
            .queue
            .iter()
            .any(|waiter| waiter.connection == connection)
        {
            entry.queue.push_back(holder);
        }
        (RequestResult::InQueue, Vec::new())
    }

    pub fn release(
        &mut self,
        name: &str,
        connection: ConnectionId,
    ) -> (ReleaseResult, Vec<Change>) {
        let Some(entry) = self.names.get_mut(name) else {
            return (ReleaseResult::NonExistent, Vec::new());
        };
        if entry.owner.connection != connection {
            let before = entry.queue.len();
            entry.queue.retain(|waiter| waiter.connection != connection);
            let was_queued = entry.queue.len() != before;
            return (
                if was_queued {
                    ReleaseResult::Released
                } else {
                    ReleaseResult::NotOwner
                },
                Vec::new(),
            );
        }
        (ReleaseResult::Released, vec![self.pass_on(name)])
    }

    /// The owner of `name` is gone: the first in line takes it, or it becomes free.
    fn pass_on(&mut self, name: &str) -> Change {
        let Some(entry) = self.names.get_mut(name) else {
            return change(name, None, None);
        };
        let old = entry.owner.connection;
        if let Some(next) = entry.queue.pop_front() {
            entry.owner = next;
            return change(name, Some(old), Some(next.connection));
        }
        self.names.remove(name);
        change(name, Some(old), None)
    }

    /// Everything a closed connection held or waited for.
    pub fn drop_connection(&mut self, connection: ConnectionId) -> Vec<Change> {
        let mut owned: Vec<String> = self
            .names
            .iter()
            .filter(|(_, entry)| entry.owner.connection == connection)
            .map(|(name, _)| name.clone())
            .collect();
        owned.sort();
        for entry in self.names.values_mut() {
            entry.queue.retain(|waiter| waiter.connection != connection);
        }
        owned.iter().map(|name| self.pass_on(name)).collect()
    }

    pub fn owner_of(&self, name: &str) -> Option<ConnectionId> {
        self.names.get(name).map(|entry| entry.owner.connection)
    }

    pub fn names_of(&self, connection: ConnectionId) -> Vec<String> {
        let mut names: Vec<String> = self
            .names
            .iter()
            .filter(|(_, entry)| entry.owner.connection == connection)
            .map(|(name, _)| name.clone())
            .collect();
        names.sort();
        names
    }

    pub fn all_names(&self) -> Vec<String> {
        let mut names: Vec<String> = self.names.keys().cloned().collect();
        names.sort();
        names
    }

    /// The owner first, then those waiting, in order.
    pub fn queue_of(&self, name: &str) -> Vec<ConnectionId> {
        self.names
            .get(name)
            .map(|entry| {
                std::iter::once(entry.owner.connection)
                    .chain(entry.queue.iter().map(|waiter| waiter.connection))
                    .collect()
            })
            .unwrap_or_default()
    }
}

fn change(name: &str, old: Option<ConnectionId>, new: Option<ConnectionId>) -> Change {
    Change {
        name: name.to_string(),
        old,
        new,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_first_request_wins_and_a_second_is_queued_or_refused() {
        let mut registry = Registry::default();
        assert_eq!(registry.request("a.B", 1, 0).0, RequestResult::PrimaryOwner);
        assert_eq!(registry.request("a.B", 1, 0).0, RequestResult::AlreadyOwner);
        assert_eq!(registry.request("a.B", 2, 0).0, RequestResult::InQueue);
        assert_eq!(
            registry.request("a.B", 3, FLAG_DO_NOT_QUEUE).0,
            RequestResult::Exists
        );
        assert_eq!(registry.queue_of("a.B"), [1, 2]);
    }

    #[test]
    fn releasing_passes_the_name_to_the_next_in_line() {
        let mut registry = Registry::default();
        registry.request("a.B", 1, 0);
        registry.request("a.B", 2, 0);
        let (result, changes) = registry.release("a.B", 1);
        assert_eq!(result, ReleaseResult::Released);
        assert_eq!(changes, [change("a.B", Some(1), Some(2))]);
        let (_, changes) = registry.release("a.B", 2);
        assert_eq!(changes, [change("a.B", Some(2), None)]);
        assert_eq!(registry.release("a.B", 2).0, ReleaseResult::NonExistent);
    }

    #[test]
    fn replacement_needs_the_owner_to_allow_it_and_the_requester_to_ask() {
        let mut registry = Registry::default();
        registry.request("a.B", 1, FLAG_ALLOW_REPLACEMENT);
        assert_eq!(registry.request("a.B", 2, 0).0, RequestResult::InQueue);
        let (result, changes) = registry.request("a.B", 3, FLAG_REPLACE_EXISTING);
        assert_eq!(result, RequestResult::PrimaryOwner);
        assert_eq!(changes, [change("a.B", Some(1), Some(3))]);
        assert_eq!(
            registry.queue_of("a.B"),
            [3, 1, 2],
            "the old owner waits in front"
        );
        let mut strict = Registry::default();
        strict.request("a.B", 1, 0);
        assert_eq!(
            strict.request("a.B", 2, FLAG_REPLACE_EXISTING).0,
            RequestResult::InQueue
        );
    }

    #[test]
    fn an_owner_that_would_not_queue_is_dropped_when_replaced() {
        let mut registry = Registry::default();
        registry.request("a.B", 1, FLAG_ALLOW_REPLACEMENT | FLAG_DO_NOT_QUEUE);
        registry.request("a.B", 2, FLAG_REPLACE_EXISTING);
        assert_eq!(registry.queue_of("a.B"), [2]);
    }

    #[test]
    fn a_closed_connection_gives_up_its_names_and_its_place_in_queues() {
        let mut registry = Registry::default();
        registry.request("a.B", 1, 0);
        registry.request("a.B", 2, 0);
        registry.request("c.D", 2, 0);
        registry.request("c.D", 1, 0);
        let changes = registry.drop_connection(1);
        assert_eq!(changes, [change("a.B", Some(1), Some(2))]);
        assert_eq!(registry.queue_of("c.D"), [2]);
        let changes = registry.drop_connection(2);
        assert_eq!(registry.all_names(), Vec::<String>::new());
        assert_eq!(changes.len(), 2);
    }

    #[test]
    fn a_waiter_can_leave_the_queue() {
        let mut registry = Registry::default();
        registry.request("a.B", 1, 0);
        registry.request("a.B", 2, 0);
        assert_eq!(registry.release("a.B", 2).0, ReleaseResult::Released);
        assert_eq!(registry.queue_of("a.B"), [1]);
        assert_eq!(registry.release("a.B", 9).0, ReleaseResult::NotOwner);
    }

    #[test]
    fn well_known_names_follow_the_specification() {
        for good in ["org.freedesktop.DBus", "a.b", "com.example.App_1", "x-y.z"] {
            assert!(is_well_known(good), "{good}");
        }
        for bad in ["", "nodots", ":1.5", "a..b", "a.1b", "a.b!", ".a.b", "a.b."] {
            assert!(!is_well_known(bad), "{bad}");
        }
        assert!(!is_well_known(&format!("a.{}", "x".repeat(300))));
    }
}
