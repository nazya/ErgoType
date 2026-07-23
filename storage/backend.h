#ifndef ERGOTYPE_STORAGE_BACKEND_H
#define ERGOTYPE_STORAGE_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool storage_backend_init(void);
bool storage_backend_read(uint32_t offset, void *buffer, size_t size);
bool storage_backend_write(uint32_t offset, const void *buffer, size_t size);
bool storage_backend_format(const void *image, size_t image_size, size_t volume_size);
bool storage_diskio_register(void);

#endif
