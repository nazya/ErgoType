/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#ifndef KEYD_LOG_H
#define KEYD_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

// Logging uses a mutex initialized by main() before scheduler starts.

#define msg(fmt, ...) _msg(0, fmt "\n", ##__VA_ARGS__)

#define dbg(fmt, ...) _msg(1, "g{INFO:} b{%s:%d:} " fmt "\n", \
    strstr(__FILE__, "ErgoType") ? strstr(__FILE__, "ErgoType") : __FILE__, __LINE__, ##__VA_ARGS__)
#define dbg2(fmt, ...) _msg(2, "c{INFO:} b{%s:%d:} " fmt "\n", \
    strstr(__FILE__, "ErgoType") ? strstr(__FILE__, "ErgoType") : __FILE__, __LINE__, ##__VA_ARGS__)
#define dbg3(fmt, ...) _msg(3, "m{INFO:} b{%s:%d:} " fmt "\n", \
    strstr(__FILE__, "ErgoType") ? strstr(__FILE__, "ErgoType") : __FILE__, __LINE__, ##__VA_ARGS__)

#define err(fmt, ...) _msg(0, "\tr{ERROR:} b{%s:%d:} " fmt "\n", \
    strstr(__FILE__, "ErgoType") ? strstr(__FILE__, "ErgoType") : __FILE__, __LINE__, ##__VA_ARGS__)
#define warn(fmt, ...) _msg(0, "\ty{WARNING:} b{%s:%d:} " fmt "\n", \
    strstr(__FILE__, "ErgoType") ? strstr(__FILE__, "ErgoType") : __FILE__, __LINE__, ##__VA_ARGS__)

void _msg(int level, const char *fmt, ...);
// void die(const char *fmt, ...);

// Initialized by main() before scheduler starts.

extern int log_level;
extern int suppress_colours;

#endif
