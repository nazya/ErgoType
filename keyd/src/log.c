/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#include <time.h>
#include "log.h"

extern char errstr[2048];

int log_level = 0;
// int suppress_colours = 0;

// void die(const char *fmt, ...) {
// 	fprintf(stderr, "%s", colorize("r{FATAL ERROR:} "));

// 	va_list ap;
// 	va_start(ap, fmt);
// 	vfprintf(stderr, colorize(fmt), ap);
// 	va_end(ap);

// 	exit(-1);
// }

void _keyd_log(int level, const char *fmt, ...)
{
	if (level > log_level)
		return;

	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}