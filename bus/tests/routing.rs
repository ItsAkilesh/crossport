// File: bus/tests/routing.rs
// Purpose: Tests of the bus's routing through its public interface, with in-memory connections
// Author: Akilesh
// Date: 2026-11-09

use std::sync::{Arc, Mutex};

use crossport_bus::bus::{Bus, Outbox};
use crossport_bus::message::{
    encode, Body, Fields, Message, ERROR, METHOD_CALL, METHOD_RETURN, SIGNAL,
};

type Inbox = Arc<Mutex<Vec<Arc<Message>>>>;

struct Recorder(Inbox);

impl Outbox for Recorder {
    fn deliver(&self, message: Arc<Message>) -> bool {
        self.0.lock().expect("lock").push(message);
        true
    }
}

struct Refuser;

impl Outbox for Refuser {
    fn deliver(&self, _message: Arc<Message>) -> bool {
        false
    }
}

fn call(destination: &str, interface: &str, member: &str, serial: u32, body: Body) -> Message {
    let (signature, bytes) = body.finish();
    let fields = Fields {
        path: Some("/org/freedesktop/DBus".into()),
        interface: Some(interface.into()),
        member: Some(member.into()),
        destination: Some(destination.into()),
        signature,
        ..Fields::default()
    };
    Message::parse(encode(METHOD_CALL, 0, serial, &fields, &bytes)).expect("parses")
}

fn bus_call(member: &str, serial: u32, body: Body) -> Message {
    call(
        "org.freedesktop.DBus",
        "org.freedesktop.DBus",
        member,
        serial,
        body,
    )
}

fn reply(to: &str, reply_serial: u32, serial: u32, body: Body) -> Message {
    let (signature, bytes) = body.finish();
    let fields = Fields {
        reply_serial: Some(reply_serial),
        destination: Some(to.into()),
        signature,
        ..Fields::default()
    };
    Message::parse(encode(METHOD_RETURN, 1, serial, &fields, &bytes)).expect("parses")
}

fn signal(member: &str, serial: u32, body: Body) -> Message {
    let (signature, bytes) = body.finish();
    let fields = Fields {
        path: Some("/o".into()),
        interface: Some("x.Y".into()),
        member: Some(member.into()),
        signature,
        ..Fields::default()
    };
    Message::parse(encode(SIGNAL, 1, serial, &fields, &bytes)).expect("parses")
}

/// A connection that has said Hello; returns its id, its unique name and what it receives.
fn client(bus: &mut Bus) -> (u64, String, Inbox) {
    let inbox: Inbox = Inbox::default();
    let id = bus.connect(Box::new(Recorder(Arc::clone(&inbox))));
    assert!(bus.handle(id, bus_call("Hello", 1, Body::new())));
    let name = {
        let received = inbox.lock().expect("lock");
        let hello = received
            .iter()
            .find(|m| m.kind == METHOD_RETURN)
            .expect("a reply to Hello");
        hello.string_argument(0).expect("the unique name")
    };
    inbox.lock().expect("lock").clear();
    (id, name, inbox)
}

fn take(inbox: &Inbox) -> Vec<Arc<Message>> {
    std::mem::take(&mut *inbox.lock().expect("lock"))
}

#[test]
fn hello_gives_a_unique_name_and_tells_the_client_it_owns_it() {
    let mut bus = Bus::new("g".repeat(32));
    let inbox: Inbox = Inbox::default();
    let id = bus.connect(Box::new(Recorder(Arc::clone(&inbox))));
    assert!(bus.handle(id, bus_call("Hello", 1, Body::new())));
    let received = take(&inbox);
    let kinds: Vec<(u8, Option<String>)> = received
        .iter()
        .map(|m| (m.kind, m.fields.member.clone()))
        .collect();
    assert_eq!(kinds.len(), 2, "{kinds:?}");
    assert_eq!(
        received[0].kind, METHOD_RETURN,
        "the reply comes before the notice"
    );
    assert_eq!(received[0].string_argument(0).as_deref(), Some(":1.1"));
    assert_eq!(received[1].fields.member.as_deref(), Some("NameAcquired"));
}

#[test]
fn a_connection_that_does_not_say_hello_first_is_closed() {
    let mut bus = Bus::new("g".repeat(32));
    let id = bus.connect(Box::new(Recorder(Inbox::default())));
    assert!(!bus.handle(id, bus_call("ListNames", 1, Body::new())));
}

#[test]
fn a_call_reaches_the_owner_of_a_well_known_name_and_the_reply_comes_back() {
    let mut bus = Bus::new("g".repeat(32));
    let (service, service_name, service_inbox) = client(&mut bus);
    let (caller, caller_name, caller_inbox) = client(&mut bus);
    bus.handle(
        service,
        bus_call(
            "RequestName",
            2,
            Body::new().string("org.example.Service").u32(0),
        ),
    );
    take(&service_inbox);
    bus.handle(
        caller,
        call(
            "org.example.Service",
            "x.Y",
            "Ask",
            9,
            Body::new().string("question"),
        ),
    );
    let delivered = take(&service_inbox);
    assert_eq!(delivered.len(), 1);
    assert_eq!(
        delivered[0].fields.sender.as_deref(),
        Some(caller_name.as_str()),
        "the bus sets the sender"
    );
    assert_eq!(delivered[0].string_argument(0).as_deref(), Some("question"));
    bus.handle(
        service,
        reply(&caller_name, 9, 4, Body::new().string("answer")),
    );
    let answered = take(&caller_inbox);
    assert_eq!(answered[0].kind, METHOD_RETURN);
    assert_eq!(answered[0].string_argument(0).as_deref(), Some("answer"));
    assert!(service_name.starts_with(":1."));
}

#[test]
fn a_call_to_a_name_nobody_owns_gets_a_service_unknown_error() {
    let mut bus = Bus::new("g".repeat(32));
    let (caller, _, inbox) = client(&mut bus);
    bus.handle(
        caller,
        call("org.example.Nobody", "x.Y", "Ask", 5, Body::new()),
    );
    let received = take(&inbox);
    assert_eq!(received[0].kind, ERROR);
    assert_eq!(
        received[0].fields.error_name.as_deref(),
        Some("org.freedesktop.DBus.Error.ServiceUnknown")
    );
    assert_eq!(received[0].fields.reply_serial, Some(5));
}

#[test]
fn a_reply_nobody_asked_for_is_dropped() {
    let mut bus = Bus::new("g".repeat(32));
    let (forger, _, _) = client(&mut bus);
    let (victim, victim_name, victim_inbox) = client(&mut bus);
    bus.handle(
        forger,
        reply(&victim_name, 1, 3, Body::new().string("forged")),
    );
    assert!(take(&victim_inbox).is_empty());
    assert_eq!(bus.connections_count(), 2);
    assert!(victim > 0);
}

#[test]
fn a_service_that_goes_away_answers_for_calls_it_never_replied_to() {
    let mut bus = Bus::new("g".repeat(32));
    let (service, _, service_inbox) = client(&mut bus);
    let (caller, _, caller_inbox) = client(&mut bus);
    bus.handle(
        service,
        bus_call(
            "RequestName",
            2,
            Body::new().string("org.example.Service").u32(0),
        ),
    );
    take(&service_inbox);
    bus.handle(
        caller,
        call("org.example.Service", "x.Y", "Slow", 11, Body::new()),
    );
    take(&caller_inbox);
    bus.disconnect(service);
    let received = take(&caller_inbox);
    let failure = received.iter().find(|m| m.kind == ERROR).expect("an error");
    assert_eq!(
        failure.fields.error_name.as_deref(),
        Some("org.freedesktop.DBus.Error.NoReply")
    );
    assert_eq!(failure.fields.reply_serial, Some(11));
}

#[test]
fn a_broadcast_reaches_only_connections_with_a_matching_rule() {
    let mut bus = Bus::new("g".repeat(32));
    let (emitter, _, _) = client(&mut bus);
    let (listener, _, listener_inbox) = client(&mut bus);
    let (other, _, other_inbox) = client(&mut bus);
    bus.handle(
        listener,
        bus_call(
            "AddMatch",
            2,
            Body::new().string("type='signal',interface='x.Y',member='Ping'"),
        ),
    );
    bus.handle(
        other,
        bus_call("AddMatch", 2, Body::new().string("member='Different'")),
    );
    take(&listener_inbox);
    take(&other_inbox);
    bus.handle(emitter, signal("Ping", 7, Body::new().string("hello")));
    assert_eq!(take(&listener_inbox).len(), 1);
    assert!(take(&other_inbox).is_empty());
}

#[test]
fn name_owner_changed_is_sent_to_subscribers_when_a_name_is_taken_and_released() {
    let mut bus = Bus::new("g".repeat(32));
    let (owner, owner_name, owner_inbox) = client(&mut bus);
    let (watcher, _, watcher_inbox) = client(&mut bus);
    bus.handle(
        watcher,
        bus_call(
            "AddMatch",
            2,
            Body::new().string("type='signal',member='NameOwnerChanged',arg0='org.example.App'"),
        ),
    );
    take(&watcher_inbox);
    bus.handle(
        owner,
        bus_call(
            "RequestName",
            2,
            Body::new().string("org.example.App").u32(0),
        ),
    );
    let first = take(&watcher_inbox);
    assert_eq!(first.len(), 1);
    assert_eq!(
        first[0].string_argument(0).as_deref(),
        Some("org.example.App")
    );
    assert_eq!(
        first[0].string_argument(2).as_deref(),
        Some(owner_name.as_str())
    );
    let notices: Vec<String> = take(&owner_inbox)
        .iter()
        .filter_map(|m| m.fields.member.clone())
        .collect();
    assert!(notices.contains(&"NameAcquired".to_string()));
    bus.disconnect(owner);
    let last = take(&watcher_inbox);
    assert_eq!(
        last[0].string_argument(2).as_deref(),
        Some(""),
        "the name is free again"
    );
}

#[test]
fn queued_requests_take_over_in_order_and_get_name_owner_follows() {
    let mut bus = Bus::new("g".repeat(32));
    let (first, _, _) = client(&mut bus);
    let (second, second_name, second_inbox) = client(&mut bus);
    bus.handle(
        first,
        bus_call(
            "RequestName",
            2,
            Body::new().string("org.example.App").u32(0),
        ),
    );
    bus.handle(
        second,
        bus_call(
            "RequestName",
            2,
            Body::new().string("org.example.App").u32(0),
        ),
    );
    let queued = take(&second_inbox);
    assert_eq!(queued[0].u32_argument(0), Some(2), "in the queue");
    bus.disconnect(first);
    take(&second_inbox);
    bus.handle(
        second,
        bus_call("GetNameOwner", 3, Body::new().string("org.example.App")),
    );
    assert_eq!(
        take(&second_inbox)[0].string_argument(0).as_deref(),
        Some(second_name.as_str())
    );
}

#[test]
fn list_names_and_name_has_owner_answer() {
    let mut bus = Bus::new("g".repeat(32));
    let (id, name, inbox) = client(&mut bus);
    bus.handle(
        id,
        bus_call(
            "RequestName",
            2,
            Body::new().string("org.example.App").u32(0),
        ),
    );
    take(&inbox);
    bus.handle(
        id,
        bus_call("NameHasOwner", 3, Body::new().string("org.example.App")),
    );
    bus.handle(
        id,
        bus_call("NameHasOwner", 4, Body::new().string("org.example.Other")),
    );
    bus.handle(id, bus_call("ListNames", 5, Body::new()));
    let received = take(&inbox);
    assert_eq!(received[0].u32_argument(0), Some(1));
    assert_eq!(received[1].u32_argument(0), Some(0));
    assert!(received[2]
        .bytes()
        .windows(name.len())
        .any(|window| window == name.as_bytes()));
}

#[test]
fn invalid_names_matches_and_methods_get_errors() {
    let mut bus = Bus::new("g".repeat(32));
    let (id, _, inbox) = client(&mut bus);
    bus.handle(
        id,
        bus_call("RequestName", 2, Body::new().string("not-a-name").u32(0)),
    );
    bus.handle(
        id,
        bus_call(
            "RequestName",
            3,
            Body::new().string("org.freedesktop.DBus.Fake").u32(0),
        ),
    );
    bus.handle(
        id,
        bus_call("AddMatch", 4, Body::new().string("type='bogus'")),
    );
    bus.handle(
        id,
        bus_call("RemoveMatch", 5, Body::new().string("member='never-added'")),
    );
    bus.handle(id, bus_call("NoSuchMethod", 6, Body::new()));
    let received = take(&inbox);
    let names: Vec<&str> = received
        .iter()
        .filter_map(|m| m.fields.error_name.as_deref())
        .collect();
    assert_eq!(
        names,
        [
            "org.freedesktop.DBus.Error.InvalidArgs",
            "org.freedesktop.DBus.Error.InvalidArgs",
            "org.freedesktop.DBus.Error.MatchRuleInvalid",
            "org.freedesktop.DBus.Error.MatchRuleNotFound",
            "org.freedesktop.DBus.Error.UnknownMethod",
        ]
    );
}

#[test]
fn a_connection_that_refuses_messages_is_closed() {
    let mut bus = Bus::new("g".repeat(32));
    let (_, _, _) = client(&mut bus);
    let slow = bus.connect(Box::new(Refuser));
    assert_eq!(bus.connections_count(), 2);
    assert!(
        !bus.handle(slow, bus_call("Hello", 1, Body::new())),
        "it could not even take the reply to Hello"
    );
    assert_eq!(
        bus.connections_count(),
        1,
        "the refusing connection was dropped"
    );
}
