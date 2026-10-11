#ifndef ERGOTYPE_PLATFORM_CDC_H
#define ERGOTYPE_PLATFORM_CDC_H

#include <stddef.h>

#define PLATFORM_CDC_BUFFER_SIZE 2048u

typedef void (*platform_cdc_write_char_fn)(char character, void *context);
typedef void (*platform_cdc_render_fn)(
    platform_cdc_write_char_fn write_character,
    void *write_context,
    void *render_context);

void platform_cdc_init(void);
void platform_cdc_write_rendered(platform_cdc_render_fn render, void *context);
void platform_cdc_poll(void);

#endif
