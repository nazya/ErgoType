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

#define ERRSTR_LENGTH 2048
extern char errstr[ERRSTR_LENGTH];

#define keyd_log(fmt, ...) _keyd_log(0, fmt, ##__VA_ARGS__);

#define dbg(fmt, ...) _keyd_log(1, "r{INFO:} b{%s:%d:} " fmt "\n", \
    strstr(__FILE__, "ErgoType") ? strstr(__FILE__, "ErgoType") : __FILE__, __LINE__, ##__VA_ARGS__)
#define dbg2(fmt, ...) _keyd_log(2, "r{INFO:} b{%s:%d:} " fmt "\n", \
    strstr(__FILE__, "ErgoType") ? strstr(__FILE__, "ErgoType") : __FILE__, __LINE__, ##__VA_ARGS__)
#define dbg3(fmt, ...) _keyd_log(3, "r{INFO:} b{%s:%d:} " fmt "\n", \
    strstr(__FILE__, "ErgoType") ? strstr(__FILE__, "ErgoType") : __FILE__, __LINE__, ##__VA_ARGS__)


#define err(fmt, ...) snprintf(errstr, sizeof(errstr), fmt, ##__VA_ARGS__);
#define warn(fmt, ...) keyd_log("\ty{WARNING:} "fmt"\n", ##__VA_ARGS__)

void _keyd_log(int level, const char *fmt, ...);

extern int log_level;


#endif