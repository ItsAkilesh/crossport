/*
 * File: tests/corpus/two_libs/libgreet.h
 * Purpose: Public interface of the second bundled library, which depends on libmath
 * Author: Akilesh
 * Date: 2026-10-28
 */

#ifndef CROSSPORT_LIBGREET_H
#define CROSSPORT_LIBGREET_H

void greet_run(void);
int greet_total(void);

#endif
