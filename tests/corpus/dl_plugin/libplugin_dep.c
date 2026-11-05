/*
 * File: tests/corpus/dl_plugin/libplugin_dep.c
 * Purpose: A library only the plugins need: it is loaded, and its constructor run, before the plugin that uses it
 * Author: Akilesh
 * Date: 2026-11-05
 */

#include <stdio.h>

__attribute__((constructor)) static void announce(void)
{
    puts("dep: loaded");
}

int dep_value(void)
{
    return 7;
}
