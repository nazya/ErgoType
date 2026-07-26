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

typedef enum {
	LOG_KIND_MSG = 0,
	LOG_KIND_WARN = 1,
	LOG_KIND_ERR = 2,
} log_kind_t;

#define msg(fmt, ...) _msg(0, LOG_KIND_MSG, fmt "\n", ##__VA_ARGS__)

#define dbg(fmt, ...) _msg(1, LOG_KIND_MSG, "g{INFO:} b{%s:%d:} " fmt "\n", \
    strstr(__FILE__, "ErgoType") ? strstr(__FILE__, "ErgoType") : __FILE__, __LINE__, ##__VA_ARGS__)
#define dbg2(fmt, ...) _msg(2, LOG_KIND_MSG, "c{INFO:} b{%s:%d:} " fmt "\n", \
    strstr(__FILE__, "ErgoType") ? strstr(__FILE__, "ErgoType") : __FILE__, __LINE__, ##__VA_ARGS__)
#define dbg3(fmt, ...) _msg(3, LOG_KIND_MSG, "m{INFO:} b{%s:%d:} " fmt "\n", \
    strstr(__FILE__, "ErgoType") ? strstr(__FILE__, "ErgoType") : __FILE__, __LINE__, ##__VA_ARGS__)

#define err(fmt, ...) _msg(0, LOG_KIND_ERR, "\tr{ERROR:} b{%s:%d:} " fmt "\n", \
	strstr(__FILE__, "ErgoType") ? strstr(__FILE__, "ErgoType") : __FILE__, __LINE__, ##__VA_ARGS__)
#define warn(fmt, ...) _msg(0, LOG_KIND_WARN, "\ty{WARNING:} b{%s:%d:} " fmt "\n", \
	strstr(__FILE__, "ErgoType") ? strstr(__FILE__, "ErgoType") : __FILE__, __LINE__, ##__VA_ARGS__)

void _msg(int level, log_kind_t kind, const char *fmt, ...);
// void die(const char *fmt, ...);

// Initialized by main() before scheduler starts.

extern int log_level;
extern int suppress_colours;

#endif
