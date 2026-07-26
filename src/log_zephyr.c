#include "log.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/sys/printk.h>

#include "platform/ble_log.h"
#include "ui/ui.h"

int log_level = 0;
int suppress_colours = 1;

static void strip_markup(const char *input, char *output, size_t output_size)
{
    size_t out = 0;
    bool inside_markup = false;

    for (size_t i = 0; input[i] != '\0' && out + 1u < output_size; ++i) {
        if (input[i + 1u] == '{' && strchr("rgybmcw", input[i])) {
            inside_markup = true;
            ++i;
            continue;
        }
        if (input[i] == '}' && inside_markup) {
            inside_markup = false;
            continue;
        }
        output[out++] = input[i];
    }

    output[out] = '\0';
}

void _msg(int level, log_kind_t kind, const char *fmt, ...)
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
        break;
    }

    char clean_fmt[160];
    char line[160];
    strip_markup(fmt, clean_fmt, sizeof(clean_fmt));

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), clean_fmt, ap);
    va_end(ap);

    if (n <= 0)
        return;

    size_t length = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1u;
    printk("%s", line);
    platform_ble_log_write(line, length);
}
