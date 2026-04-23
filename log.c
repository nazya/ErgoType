/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#include "log.h"

char errstr[ERRSTR_LENGTH] = {0};
int log_level = 0;

void _keyd_log(int level, const char *fmt, ...)
{
	if (level > log_level)
		return;

	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}
