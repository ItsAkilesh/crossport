#!/usr/bin/env bash
# File: tools/ci/check_bus.sh
# Purpose: Run crossport-bus against real D-Bus clients (libdbus's dbus-send and dbus-monitor, GLib's gdbus)
# Author: Akilesh
# Date: 2026-11-09
#
# Usage: tools/ci/check_bus.sh
# Needs: dbus-send and dbus-monitor (package dbus), gdbus (package libglib2.0-bin), a Rust toolchain.
# The test clients are the ones real programs use, so this checks the nonce-tcp transport, the handshake and the
# bus methods against implementations written by other people.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
work="$(mktemp -d)"
monitor_pid=""
daemon_pid=""

cleanup() {
  [[ -n "$monitor_pid" ]] && kill "$monitor_pid" 2> /dev/null || true
  [[ -n "$daemon_pid" ]] && kill "$daemon_pid" 2> /dev/null || true
  rm -rf "$work"
}
trap cleanup EXIT

cargo build --release --locked --manifest-path "$root/bus/Cargo.toml"
"$root/bus/target/release/crossport-bus" --nonce-file "$work/nonce" --idle-exit 120 > "$work/address" 2> "$work/log" &
daemon_pid=$!

for _ in $(seq 1 50); do
  [[ -s "$work/address" ]] && break
  sleep 0.2
done
address="$(head -n 1 "$work/address")"
[[ "$address" == nonce-tcp:* ]] || { echo "the daemon printed no address: $address" >&2; cat "$work/log" >&2; exit 1; }
echo "bus at $address"

fail() {
  echo "check_bus: $1" >&2
  cat "$work/log" >&2 || true
  exit 1
}

# libdbus: a call to the bus itself, and a name that is taken and released again.
names="$(dbus-send --address="$address" --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus.ListNames)"
grep -q 'string "org.freedesktop.DBus"' <<< "$names" || fail "ListNames lacks the bus: $names"
grep -q 'string ":1\.' <<< "$names" || fail "ListNames lacks the caller's unique name: $names"

dbus-monitor --address "$address" "type='signal',member='NameOwnerChanged'" > "$work/monitor" 2>&1 &
monitor_pid=$!
sleep 1
reply="$(dbus-send --address="$address" --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus.RequestName string:org.crossport.Check uint32:0)"
grep -q 'uint32 1' <<< "$reply" || fail "RequestName did not make the caller the owner: $reply"
sleep 1
grep -q 'org.crossport.Check' "$work/monitor" || fail "the monitor never saw the name arrive: $(cat "$work/monitor")"

# A call to a name nobody owns must come back as an error, not hang.
if dbus-send --address="$address" --print-reply --dest=org.crossport.Nobody /x org.crossport.Nobody.Call 2> "$work/error" > /dev/null; then
  fail "a call to a missing service succeeded"
fi
grep -q 'ServiceUnknown' "$work/error" || fail "a missing service did not answer ServiceUnknown: $(cat "$work/error")"

# GLib's client (what GTK uses): its own handshake and a typed reply.
gdbus_names="$(gdbus call --address "$address" --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.ListNames)"
grep -q "org.freedesktop.DBus" <<< "$gdbus_names" || fail "gdbus ListNames: $gdbus_names"
gdbus call --address "$address" --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.GetId | grep -q "'" || fail "gdbus GetId"

echo "check_bus: ok"
