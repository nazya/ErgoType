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
	LOG_KIND_DBG = 1,
	LOG_KIND_WARN = 2,
	LOG_KIND_ERR = 3,
} log_kind_t;

extern int log_level;
extern int use_colours;

#define ANSI_DARK_GREY (use_colours ? "\033[38;2;107;108;115m" : "")
#define ANSI_BRIGHT_WHITE (use_colours ? "\033[0;97m" : "")
#define ANSI_BOLD (use_colours ? "\033[1m" : "")
#define ANSI_BLACK_ON_BRIGHT_WHITE (use_colours ? "\033[30;107m" : "")
#define ANSI_RESET (use_colours ? "\033[0m" : "")

#define msg(fmt, ...) \
	_msg(0, LOG_KIND_MSG, NULL, 0, fmt "\n", ##__VA_ARGS__)

#define dbg0(fmt, ...) \
	_msg(0, LOG_KIND_DBG, __FILE__, __LINE__, fmt "\n", ##__VA_ARGS__)
#define dbg(fmt, ...) \
	_msg(1, LOG_KIND_DBG, __FILE__, __LINE__, fmt "\n", ##__VA_ARGS__)
#define dbg2(fmt, ...) \
	_msg(2, LOG_KIND_DBG, __FILE__, __LINE__, fmt "\n", ##__VA_ARGS__)
#define dbg3(fmt, ...) \
	_msg(3, LOG_KIND_DBG, __FILE__, __LINE__, fmt "\n", ##__VA_ARGS__)

#define err(fmt, ...) \
	_msg(0, LOG_KIND_ERR, __FILE__, __LINE__, fmt "\n", ##__VA_ARGS__)
#define warn(fmt, ...) \
	_msg(0, LOG_KIND_WARN, __FILE__, __LINE__, fmt "\n", ##__VA_ARGS__)

void _msg(int level, log_kind_t kind, const char *file,
	  unsigned int line, const char *format, ...);

#endif
