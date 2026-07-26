#ifndef ERGOTYPE_PLATFORM_STORAGE_H
#define ERGOTYPE_PLATFORM_STORAGE_H

#include <stddef.h>
#include <stdint.h>

int platform_storage_mount(void);
int platform_storage_format(void);
int platform_storage_size(const char *path, size_t *size);
int platform_storage_info(size_t *total, size_t *free);
int platform_storage_read(const char *path, uint8_t *buffer, size_t buffer_size, size_t *size_read);
int platform_storage_write(const char *path, const uint8_t *buffer, size_t size);
int platform_storage_delete(const char *path);
int platform_storage_list(char *buffer, size_t buffer_size, size_t *size_written);
int platform_storage_upload_begin(void);
int platform_storage_upload_write(const uint8_t *buffer, size_t size, size_t offset);
int platform_storage_upload_commit(const char *path);
int platform_storage_upload_abort(void);

#endif
