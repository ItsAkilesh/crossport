/*
 * File: tests/corpus/dl_plugin/plugin.h
 * Purpose: What the host program exports to its plugins
 * Author: Akilesh
 * Date: 2026-11-05
 */

#ifndef DL_PLUGIN_H
#define DL_PLUGIN_H

int host_value(void);
void host_log(const char *message);

#endif
