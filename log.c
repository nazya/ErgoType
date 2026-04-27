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

	static char buf[1984];
	// static char buf[4096];
	size_t n = 0;
	int inside_escape = 0;

	for (i = 0; s[i] != 0 && n < sizeof(buf); i++) {
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

static void log_write_bytes(const char *buf, size_t len)
{
	if (!buf || !len)
		return;

	// CDC: buffer only (no tud_* calls here).
	// Convert LF to CRLF for serial terminals that don't treat '\n' as newline.
	// Preserve existing CRLF if caller already provided '\r\n' (including across chunks).
	static bool last_was_cr;
	size_t start = 0;

	for (size_t i = 0; i < len; ++i) {
		char c = buf[i];
		if (c == '\n') {
			if (i > start)
				stdio_tusb_cdc_write(buf + start, (int)(i - start));

			if (last_was_cr) {
				stdio_tusb_cdc_write("\n", 1);
			} else {
				stdio_tusb_cdc_write("\r\n", 2);
			}

			start = i + 1;
			last_was_cr = false;
			continue;
		}

		last_was_cr = (c == '\r');
	}

	if (start < len)
		stdio_tusb_cdc_write(buf + start, (int)(len - start));
}

typedef struct {
	char buf[128];
	size_t len;
} log_sink_t;

static void sink_flush(log_sink_t *sink)
{
	if (!sink || !sink->len)
		return;

	log_write_bytes(sink->buf, sink->len);
	sink->len = 0;
}

static void sink_out(char character, void *arg)
{
	log_sink_t *sink = (log_sink_t *)arg;
	if (!sink)
		return;

	sink->buf[sink->len++] = character;
	if (sink->len == sizeof(sink->buf)) {
		sink_flush(sink);
	}
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
	sink_flush(&sink);
	xSemaphoreGive(log_mutex);
	va_end(ap);
}

void die(const char *fmt, ...)
{
	va_list ap;

	xSemaphoreTake(log_mutex, portMAX_DELAY);

	const char *prefix = colorize("r{FATAL ERROR:} ");
	log_write_bytes(prefix, strlen(prefix));

	va_start(ap, fmt);
	log_sink_t sink = {0};
	vfctprintf(sink_out, &sink, colorize(fmt), ap);
	va_end(ap);

	sink_out('\n', &sink);
	sink_flush(&sink);

	xSemaphoreGive(log_mutex);
	// exit(-1);
}
