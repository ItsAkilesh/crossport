// File: bus/src/matching.rs
// Purpose: Match rules: parse the text AddMatch gets and decide whether a message is wanted by a connection
// Author: Akilesh
// Date: 2026-11-09

use crate::message::{Message, ERROR, METHOD_CALL, METHOD_RETURN, SIGNAL};

/// The most arguments a rule can test (the specification numbers them 0 to 63).
const MAX_ARGUMENTS: usize = 64;

#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct MatchRule {
    kind: Option<u8>,
    sender: Option<String>,
    interface: Option<String>,
    member: Option<String>,
    path: Option<String>,
    path_namespace: Option<String>,
    destination: Option<String>,
    arguments: Vec<(usize, String)>,
    argument_paths: Vec<(usize, String)>,
}

/// Splits `key='value',key2=value2` into pairs. Inside single quotes a comma is part of the value; outside them,
/// a backslash-quote is a literal quote.
fn pairs(text: &str) -> Result<Vec<(String, String)>, &'static str> {
    let mut result = Vec::new();
    let mut key = String::new();
    let mut value = String::new();
    let mut is_value = false;
    let mut is_quoted = false;
    let mut characters = text.chars().peekable();
    while let Some(character) = characters.next() {
        match character {
            '\'' => is_quoted = !is_quoted,
            '\\' if !is_quoted && characters.peek() == Some(&'\'') => {
                characters.next();
                value.push('\'');
            }
            '=' if !is_value && !is_quoted => is_value = true,
            ',' if !is_quoted => {
                if !is_value {
                    return Err("a key without a value");
                }
                result.push((std::mem::take(&mut key), std::mem::take(&mut value)));
                is_value = false;
            }
            _ if is_value => value.push(character),
            _ => key.push(character),
        }
    }
    if is_quoted {
        return Err("an unclosed quote");
    }
    if is_value {
        result.push((key, value));
    } else if !key.trim().is_empty() {
        return Err("a key without a value");
    }
    Ok(result)
}

fn argument_index(key: &str, prefix: &str) -> Option<usize> {
    key.strip_prefix(prefix)?
        .parse()
        .ok()
        .filter(|&index| index < MAX_ARGUMENTS)
}

impl MatchRule {
    pub fn parse(text: &str) -> Result<Self, &'static str> {
        let mut rule = Self::default();
        for (key, value) in pairs(text)? {
            let key = key.trim();
            match key {
                "type" => {
                    rule.kind = Some(match value.as_str() {
                        "signal" => SIGNAL,
                        "method_call" => METHOD_CALL,
                        "method_return" => METHOD_RETURN,
                        "error" => ERROR,
                        _ => return Err("an unknown message type"),
                    });
                }
                "sender" => rule.sender = Some(value),
                "interface" => rule.interface = Some(value),
                "member" => rule.member = Some(value),
                "path" => rule.path = Some(value),
                "path_namespace" => rule.path_namespace = Some(value),
                "destination" => rule.destination = Some(value),
                "eavesdrop" => {}
                _ => {
                    if let Some(index) =
                        argument_index(key.strip_suffix("path").unwrap_or(key), "arg")
                    {
                        if key.ends_with("path") {
                            rule.argument_paths.push((index, value));
                        } else {
                            rule.arguments.push((index, value));
                        }
                    } else if argument_index(key.strip_suffix("namespace").unwrap_or(key), "arg")
                        .is_none()
                    {
                        return Err("an unknown key");
                    }
                }
            }
        }
        Ok(rule)
    }

    /// Whether the rule wants `message`. `sender_names` are the names the sender answers to: its unique name and
    /// every well-known name it owns.
    pub fn matches(&self, message: &Message, sender_names: &[String]) -> bool {
        let fields = &message.fields;
        let equal =
            |wanted: &Option<String>, actual: &Option<String>| wanted.is_none() || wanted == actual;
        self.kind.is_none_or(|kind| kind == message.kind)
            && self
                .sender
                .as_ref()
                .is_none_or(|sender| sender_names.contains(sender))
            && equal(&self.interface, &fields.interface)
            && equal(&self.member, &fields.member)
            && equal(&self.path, &fields.path)
            && equal(&self.destination, &fields.destination)
            && self
                .path_namespace
                .as_ref()
                .is_none_or(|namespace| in_namespace(fields.path.as_deref(), namespace))
            && self
                .arguments
                .iter()
                .all(|(index, value)| message.string_argument(*index).as_deref() == Some(value))
            && self.argument_paths.iter().all(|(index, value)| {
                message
                    .string_argument(*index)
                    .is_some_and(|argument| paths_overlap(&argument, value))
            })
    }
}

fn in_namespace(path: Option<&str>, namespace: &str) -> bool {
    path.is_some_and(|path| {
        path == namespace
            || path
                .strip_prefix(namespace)
                .is_some_and(|rest| rest.starts_with('/'))
    })
}

/// `arg0path`: equal, or one ends in a slash and is a prefix of the other.
fn paths_overlap(argument: &str, wanted: &str) -> bool {
    argument == wanted
        || (wanted.ends_with('/') && argument.starts_with(wanted))
        || (argument.ends_with('/') && wanted.starts_with(argument))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::message::{encode, Body, Fields};

    fn signal(path: &str, interface: &str, member: &str, body: Body) -> Message {
        let (signature, bytes) = body.finish();
        let fields = Fields {
            path: Some(path.into()),
            interface: Some(interface.into()),
            member: Some(member.into()),
            signature,
            ..Fields::default()
        };
        Message::parse(encode(SIGNAL, 0, 1, &fields, &bytes)).expect("parses")
    }

    fn names(items: &[&str]) -> Vec<String> {
        items.iter().map(|item| (*item).to_string()).collect()
    }

    #[test]
    fn a_rule_with_only_a_type_matches_every_message_of_it() {
        let rule = MatchRule::parse("type='signal'").expect("parses");
        let message = signal("/a", "x.Y", "Z", Body::new());
        assert!(rule.matches(&message, &[]));
        assert!(!MatchRule::parse("type='error'")
            .expect("parses")
            .matches(&message, &[]));
    }

    #[test]
    fn interface_member_and_path_must_all_agree() {
        let message = signal("/org/a", "x.Y", "Z", Body::new());
        for (rule, wanted) in [
            ("interface='x.Y',member='Z',path='/org/a'", true),
            ("interface='x.Y',member='Other'", false),
            ("path='/org/b'", false),
            ("path_namespace='/org'", true),
            ("path_namespace='/or'", false),
        ] {
            assert_eq!(
                MatchRule::parse(rule)
                    .expect("parses")
                    .matches(&message, &[]),
                wanted,
                "{rule}"
            );
        }
    }

    #[test]
    fn a_sender_matches_by_unique_or_well_known_name() {
        let message = signal("/", "x.Y", "Z", Body::new());
        let rule = MatchRule::parse("sender='org.example.App'").expect("parses");
        assert!(rule.matches(&message, &names([":1.4", "org.example.App"].as_slice())));
        assert!(!rule.matches(&message, &names([":1.4"].as_slice())));
    }

    #[test]
    fn string_arguments_are_tested_by_position() {
        let message = signal(
            "/",
            "x.Y",
            "Z",
            Body::new().string("first").string("second"),
        );
        assert!(MatchRule::parse("arg1='second'")
            .expect("parses")
            .matches(&message, &[]));
        assert!(!MatchRule::parse("arg0='second'")
            .expect("parses")
            .matches(&message, &[]));
        assert!(!MatchRule::parse("arg5='x'")
            .expect("parses")
            .matches(&message, &[]));
    }

    #[test]
    fn argument_paths_match_by_prefix_with_a_trailing_slash() {
        let message = signal("/", "x.Y", "Z", Body::new().string("/aa/bb/cc"));
        assert!(MatchRule::parse("arg0path='/aa/bb/'")
            .expect("parses")
            .matches(&message, &[]));
        assert!(!MatchRule::parse("arg0path='/aa/xx/'")
            .expect("parses")
            .matches(&message, &[]));
    }

    #[test]
    fn quotes_commas_and_escapes_in_values_are_read() {
        let rule = MatchRule::parse(r"member='a,b',interface=x.Y\'z").expect("parses");
        let message = signal("/", "x.Y'z", "a,b", Body::new());
        assert!(rule.matches(&message, &[]));
    }

    #[test]
    fn bad_rules_are_refused() {
        for text in [
            "type='bogus'",
            "nokey",
            "member='unclosed",
            "wat='x'",
            "arg99='x'",
        ] {
            assert!(MatchRule::parse(text).is_err(), "{text}");
        }
    }

    #[test]
    fn an_empty_rule_matches_everything() {
        assert!(MatchRule::parse("")
            .expect("parses")
            .matches(&signal("/", "a.B", "C", Body::new()), &[]));
    }
}
