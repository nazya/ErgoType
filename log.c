/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#include "log.h"

#include "stdio_tusb_cdc.h"
#include "ui/ui.h"

#include "pico/printf.h"

#include "FreeRTOS.h"
#include "semphr.h"

int log_level = 0;
int use_colours = 1;

// Serialize each rendered write so a throttled replay cannot interleave with
// another producer.
extern SemaphoreHandle_t log_mutex;

typedef struct {
	stdio_tusb_cdc_write_char_fn write_character;
	void *context;
} log_writer_t;

typedef struct {
	int level;
	log_kind_t kind;
	const char *file;
	unsigned int line;
	const char *format;
	va_list *format_args;
	bool use_colours;
} log_entry_t;

static void write_string(log_writer_t *writer, const char *string)
{
	while (*string)
		writer->write_character(*string++, writer->context);
}

static void write_decimal(log_writer_t *writer, unsigned int value)
{
	char digits[10];
	size_t length = 0;

	do {
		digits[length++] = (char)('0' + value % 10u);
		value /= 10u;
	} while (value);

	while (length)
		writer->write_character(digits[--length], writer->context);
}

static void write_log_prefix(log_writer_t *writer,
			     const log_entry_t *entry)
{
	const char *prefix;
	const char *suffix;

	switch (entry->kind) {
	case LOG_KIND_MSG:
		return;
	case LOG_KIND_DBG:
		if (!entry->use_colours)
			prefix = "INFO: ";
		else if (entry->level == 1)
			prefix = "\033[32mINFO:\033[0m \033[38;2;107;108;115m";
		else if (entry->level == 2)
			prefix = "\033[36mINFO:\033[0m \033[38;2;107;108;115m";
		else
			prefix = "\033[35mINFO:\033[0m \033[38;2;107;108;115m";
		break;
	case LOG_KIND_WARN:
		prefix = entry->use_colours ?
			 "\t\033[1;37;43mWARNING:\033[0m \033[38;2;107;108;115m" : "\tWARNING: ";
		break;
	case LOG_KIND_ERR:
		prefix = entry->use_colours ?
			 "\t\033[1;37;41mERROR:\033[0m \033[38;2;107;108;115m" : "\tERROR: ";
		break;
	}

	if (!entry->use_colours)
		suffix = ": ";
	else
		suffix = ":\033[0m ";
	write_string(writer, prefix);
	write_string(writer, entry->file);
	writer->write_character(':', writer->context);
	write_decimal(writer, entry->line);
	write_string(writer, suffix);
}

static void write_crlf_character(char character, void *context)
{
	log_writer_t *writer = context;

	if (character == '\n')
		writer->write_character('\r', writer->context);
	writer->write_character(character, writer->context);
}

static void render_log_line(stdio_tusb_cdc_write_char_fn write_character,
			    void *write_context, void *render_context)
{
	log_entry_t *entry = render_context;
	log_writer_t writer = {
		.write_character = write_character,
		.context = write_context,
	};

	write_log_prefix(&writer, entry);

	/* Throttling may render the line twice, so restart its arguments. */
	va_list args;
	va_copy(args, *entry->format_args);
	vfctprintf(write_crlf_character, &writer, entry->format, args);
	va_end(args);
}

/* _msg() is the logger's printf-style entry point. */
void _msg(int level, log_kind_t kind, const char *file, unsigned int line,
	  const char *format, ...)
{
	if (level > log_level)
		return;

	switch (kind) {
	case LOG_KIND_WARN:
		ui_notify_warn();
		break;
	case LOG_KIND_ERR:
		ui_notify_err();
		break;
	case LOG_KIND_MSG:
	case LOG_KIND_DBG:
		break;
	}

	va_list format_args;
	va_start(format_args, format);
	log_entry_t entry = {
		.level = level,
		.kind = kind,
		.file = file,
		.line = line,
		.format = format,
		.format_args = &format_args,
		.use_colours = use_colours,
	};

	xSemaphoreTake(log_mutex, portMAX_DELAY);
	stdio_tusb_cdc_write_rendered(render_log_line, &entry);
	xSemaphoreGive(log_mutex);
	va_end(format_args);
}
