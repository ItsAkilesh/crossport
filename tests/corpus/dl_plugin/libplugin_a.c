/*
 * File: tests/corpus/dl_plugin/libplugin_a.c
 * Purpose: Plugin "alpha": exports functions, uses the host and a library of its own, and keeps a thread-local counter
 * Author: Akilesh
 * Date: 2026-11-05
 */

#include <stdio.h>

#include "plugin.h"

int dep_value(void);

static __thread int g_counter;

__attribute__((constructor)) static void announce(void)
{
    puts("plugin_a: loaded");
}

const char *plugin_name(void)
{
    return "alpha";
}

int plugin_add(int a, int b)
{
    return a + b;
}

int plugin_dep_value(void)
{
    return dep_value();
}

int plugin_instance(void)
{
    return 1;
}

/* Calls through the symbol table, so the plugin's own definition must win over the other plugin's. */
int plugin_instance_via_self(void)
{
    return plugin_instance();
}

int plugin_host_call(void)
{
    host_log("from alpha");
    return host_value() + 1;
}

int plugin_tls_next(void)
{
    return ++g_counter;
}
