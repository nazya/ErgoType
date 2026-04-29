/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#include "log.h"

#include "stdio_tusb_cdc.h"

#include "pico/printf.h"

#include "FreeRTOS.h"
#include "semphr.h"

int log_level = 0;
int suppress_colours = 0;

// FreeRTOSConfig.h has configUSE_NEWLIB_REENTRANT=0, so newlib stdio/printf
// is not task-safe. Also, colorize() uses a shared static buffer.
// Serialize the whole log call to avoid truncated/garbled output.
extern SemaphoreHandle_t log_mutex;

static const char *colorize(const char *s)
{
	int i;

	static char buf[BUFSIZE];
	// static char buf[4096];
	size_t n = 0;
	int inside_escape = 0;

	// for (i = 0; s[i] != 0 && n < sizeof(buf); i++) {
	for (i = 0; s[i] != 0 && (sizeof(buf) - n) > 1; i++) {
		if (s[i + 1] == '{') {
			int escape_num = 0;

			switch (s[i]) {
			case 'r': escape_num = 1; break;
			case 'g': escape_num = 2; break;
			case 'y': escape_num = 3; break;
			case 'b': escape_num = 4; break;
			case 'm': escape_num = 5; break;
			case 'c': escape_num = 6; break;
			case 'w': escape_num = 7; break;
			default: break;
			}

			if (escape_num) {
				if (!suppress_colours && (sizeof(buf) - n > 5)) {
					buf[n++] = '\033';
					buf[n++] = '[';
					buf[n++] = '3';
					buf[n++] = '0' + escape_num;
					buf[n++] = 'm';
				}

				inside_escape = 1;
				i++;
				continue;
			}
		}

		if (s[i] == '}' && inside_escape) {
			if (!suppress_colours && (sizeof(buf) - n > 4)) {
				memcpy(buf + n, "\033[0m", 4);
				n += 4;
			}

			inside_escape = 0;
			continue;
		}

		buf[n++] = s[i];
	}

	buf[n] = 0;
	return buf;
}

typedef struct {
	char buf[BUFSIZE];
	size_t len;
} log_sink_t;

static void sink_flush(log_sink_t *sink)
{
	if (sink->len)
		stdio_tusb_cdc_write(sink->buf, sink->len);
	sink->len = 0;
}

static void sink_out(char character, void *arg)
{
	log_sink_t *sink = (log_sink_t *)arg;
	if (character == '\n' && (sizeof(sink->buf) - sink->len) > 1) {
		sink->buf[sink->len++] = '\r';
		sink->buf[sink->len++] = '\n';
		sink_flush(sink);
		return;
	}

	if ((sizeof(sink->buf) - sink->len) == 2) {
		// Very long line: drop incoming bytes
		sink->buf[sink->len++] = '\r';
		sink->buf[sink->len++] = '\n';
		sink_flush(sink);
		return;
	}
	
	sink->buf[sink->len++] = character;
}

void _msg(int level, const char *fmt, ...)
{
	if (level > log_level)
		return;

	va_list ap;
	va_start(ap, fmt);
	xSemaphoreTake(log_mutex, portMAX_DELAY);
	log_sink_t sink = {0};
	vfctprintf(sink_out, &sink, colorize(fmt), ap);
	xSemaphoreGive(log_mutex);
	va_end(ap);
}

// void die(const char *fmt, ...)
// {
// 	va_list ap;

// 	xSemaphoreTake(log_mutex, portMAX_DELAY);

// 	const char *prefix = colorize("r{FATAL ERROR:} ");
// 	stdio_tusb_cdc_write(prefix, strlen(prefix));

// 	va_start(ap, fmt);
// 	log_sink.len = 0;
// 	vfctprintf(sink_out, &log_sink, colorize(fmt), ap);
// 	va_end(ap);
// 	sink_out('\n', &log_sink);
// 	xSemaphoreGive(log_mutex);
// 	// exit(-1);
// }
