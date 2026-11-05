/*
 * File: tests/corpus/dl_plugin/main.c
 * Purpose: Host program that opens plugins with dlopen and calls into them, as toolkits do
 * Author: Akilesh
 * Date: 2026-11-05
 *
 * Differential test: the same program runs on Linux and, translated with its plugins, on Windows, and prints the
 * same output. It checks that a missing library fails with a message, that a plugin's own library is loaded and
 * constructed first, that plugin constructors run when opened, that plugins reach the host's exported functions,
 * that two plugins exporting one name each use their own, that opening a plugin twice gives one handle, and that
 * a plugin's thread-local variable works.
 */

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "plugin.h"

typedef const char *(*NameFunction)(void);
typedef int (*AddFunction)(int, int);
typedef int (*IntFunction)(void);

int host_value(void)
{
    return 41;
}

void host_log(const char *message)
{
    printf("host_log: %s\n", message);
}

static void *open_plugin(const char *library)
{
    void *handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    printf("open %s: %s\n", library, handle != NULL ? "ok" : "failed");
    return handle;
}

static int call_int(void *handle, const char *symbol)
{
    IntFunction function = (IntFunction)dlsym(handle, symbol);
    return function != NULL ? function() : -1;
}

static void check_missing_library(void)
{
    void *handle = dlopen("libnosuchplugin.so", RTLD_NOW);
    const char *error = dlerror();
    printf("missing: handle=%s error=%d\n", handle != NULL ? "set" : "null",
           error != NULL && strstr(error, "cannot open shared object file") != NULL);
}

static void use_plugin_a(void *plugin_a)
{
    NameFunction name = (NameFunction)dlsym(plugin_a, "plugin_name");
    AddFunction add = (AddFunction)dlsym(plugin_a, "plugin_add");
    printf("a: name=%s\n", name());
    printf("a: add=%d\n", add(2, 3));
    printf("a: dep=%d\n", call_int(plugin_a, "plugin_dep_value"));
    printf("a: host=%d\n", call_int(plugin_a, "plugin_host_call"));
    printf("a: unknown symbol=%s\n", dlsym(plugin_a, "no_such_symbol") == NULL ? "null" : "found");
}

static void use_thread_local(void *plugin_a)
{
    int first = call_int(plugin_a, "plugin_tls_next");
    int second = call_int(plugin_a, "plugin_tls_next");
    int third = call_int(plugin_a, "plugin_tls_next");
    printf("a: tls=%d,%d,%d\n", first, second, third);
}

int main(void)
{
    check_missing_library();
    void *plugin_a = open_plugin("libplugin_a.so");
    use_plugin_a(plugin_a);
    void *plugin_b = open_plugin("libplugin_b.so");
    NameFunction name_b = (NameFunction)dlsym(plugin_b, "plugin_name");
    printf("b: name=%s\n", name_b());
    int instance_a = call_int(plugin_a, "plugin_instance_via_self");
    int instance_b = call_int(plugin_b, "plugin_instance_via_self");
    printf("instances: a=%d b=%d\n", instance_a, instance_b);
    void *again = dlopen("libplugin_a.so", RTLD_NOW);
    printf("reopen: same handle=%d\n", again == plugin_a);
    use_thread_local(plugin_a);
    dlclose(plugin_a);
    dlclose(plugin_b);
    puts("done");
    return 0;
}
