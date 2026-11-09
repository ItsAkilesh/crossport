/*
 * File: tests/corpus/dbus_session/main.c
 * Purpose: libdbus client that connects to the session bus, takes a name, lists names and receives its own signal
 * Author: Akilesh
 * Date: 2026-11-09
 *
 * Windows-only check of the session bus the shim starts (tools/ci/check_output.py reads the output). On Linux there
 * is usually no session bus in CI, and the program prints "bus: none" and succeeds.
 */

#include <stdio.h>
#include <string.h>

#include <dbus/dbus.h>

#define OWNED_NAME "org.crossport.Test"
#define CALL_TIMEOUT_MILLISECONDS 5000
#define SIGNAL_ROUNDS 30
#define ROUND_MILLISECONDS 100

static int list_contains(DBusConnection *connection, const char *wanted)
{
    DBusError error;
    dbus_error_init(&error);
    DBusMessage *call = dbus_message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames");
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(connection, call, CALL_TIMEOUT_MILLISECONDS, &error);
    dbus_message_unref(call);
    int is_listed = 0;
    if (reply != NULL) {
        char **names = NULL;
        int count = 0;
        if (dbus_message_get_args(reply, &error, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &names, &count, DBUS_TYPE_INVALID)) {
            for (int i = 0; i < count; i++) {
                is_listed |= strcmp(names[i], wanted) == 0;
            }
            dbus_free_string_array(names);
        }
        dbus_message_unref(reply);
    }
    return is_listed;
}

static int receive_own_signal(DBusConnection *connection)
{
    DBusError error;
    dbus_error_init(&error);
    dbus_bus_add_match(connection, "type='signal',interface='org.crossport.Test',member='Hello'", &error);
    DBusMessage *signal = dbus_message_new_signal("/org/crossport", "org.crossport.Test", "Hello");
    dbus_connection_send(connection, signal, NULL);
    dbus_message_unref(signal);
    dbus_connection_flush(connection);
    for (int round = 0; round < SIGNAL_ROUNDS; round++) {
        dbus_connection_read_write(connection, ROUND_MILLISECONDS);
        DBusMessage *message = dbus_connection_pop_message(connection);
        if (message != NULL) {
            int is_ours = dbus_message_is_signal(message, "org.crossport.Test", "Hello");
            dbus_message_unref(message);
            if (is_ours) {
                return 1;
            }
        }
    }
    return 0;
}

int main(void)
{
    DBusError error;
    dbus_error_init(&error);
    DBusConnection *connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (connection == NULL) {
        puts("bus: none");
        return 0;
    }
    const char *unique = dbus_bus_get_unique_name(connection);
    printf("bus: connected unique_name_ok=%d\n", unique != NULL && strncmp(unique, ":1.", 3) == 0);
    printf("request: %d\n", dbus_bus_request_name(connection, OWNED_NAME, 0, &error));
    printf("has_owner: %d\n", dbus_bus_name_has_owner(connection, OWNED_NAME, &error));
    printf("names: listed=%d\n", list_contains(connection, OWNED_NAME));
    printf("signal: received=%d\n", receive_own_signal(connection));
    /* A shared connection (dbus_bus_get) is only released, never closed. */
    dbus_connection_unref(connection);
    puts("done");
    return 0;
}
