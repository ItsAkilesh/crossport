// File: bus/src/lib.rs
// Purpose: crossport-bus: a D-Bus session bus for translated programs, as a library (the routing) and a daemon (the transport)
// Author: Akilesh
// Date: 2026-11-09

pub mod auth;
pub mod bus;
pub mod matching;
pub mod message;
pub mod names;
pub mod server;
mod service;
pub mod wire;
