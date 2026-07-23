#ifndef ERGOTYPE_PLATFORM_STORAGE_H
#define ERGOTYPE_PLATFORM_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ff.h"

#define PLATFORM_FS_OK FR_OK
#define PLATFORM_FS_DISK_ERR FR_DISK_ERR
#define PLATFORM_FS_DENIED FR_DENIED
#define PLATFORM_FS_NOT_ENOUGH_CORE FR_NOT_ENOUGH_CORE
#define PLATFORM_FS_NO_FILE FR_NO_FILE
#define PLATFORM_FS_NO_PATH FR_NO_PATH

#define PLATFORM_FS_READ FA_READ
#define PLATFORM_FS_WRITE FA_WRITE
#define PLATFORM_FS_CREATE_ALWAYS FA_CREATE_ALWAYS
#define PLATFORM_FS_MOUNT_NOW true

#define PLATFORM_STORAGE_BLOCK_COUNT 128u
#define PLATFORM_STORAGE_BLOCK_SIZE 512u
#define PLATFORM_STORAGE_SIZE (PLATFORM_STORAGE_BLOCK_COUNT * PLATFORM_STORAGE_BLOCK_SIZE)
#define PLATFORM_STORAGE_NAME_MAX 255u

typedef int platform_fs_result_t;
typedef FATFS platform_storage_fs_t;
typedef FIL platform_storage_file_t;
#ifdef ESP_PLATFORM
typedef FF_DIR platform_storage_dir_t;
#else
typedef DIR platform_storage_dir_t;
#endif

typedef struct {
    uint32_t size;
    uint8_t attributes;
    char name[PLATFORM_STORAGE_NAME_MAX + 1u];
} platform_storage_file_info_t;

uint32_t platform_storage_block_count(void);
uint16_t platform_storage_block_size(void);
bool platform_storage_mount(void);
void platform_storage_format(void);
bool platform_storage_read(uint32_t offset, void *buffer, size_t size);
bool platform_storage_write(uint32_t offset, const void *buffer, size_t size);

platform_fs_result_t platform_fs_mount(platform_storage_fs_t *fs, bool mount_now);
platform_fs_result_t platform_fs_unmount(void);
platform_fs_result_t platform_fs_get_space(uint32_t *total_bytes, uint32_t *free_bytes);
platform_fs_result_t platform_file_open(platform_storage_file_t *file, const char *path, uint8_t mode);
platform_fs_result_t platform_file_close(platform_storage_file_t *file);
platform_fs_result_t platform_file_read(platform_storage_file_t *file,
                                        void *buffer,
                                        uint32_t bytes_to_read,
                                        uint32_t *bytes_read);
platform_fs_result_t platform_file_write(platform_storage_file_t *file,
                                         const void *buffer,
                                         uint32_t bytes_to_write,
                                         uint32_t *bytes_written);
platform_fs_result_t platform_file_seek(platform_storage_file_t *file, uint32_t offset);
char *platform_file_gets(char *buffer, int len, platform_storage_file_t *file);
platform_fs_result_t platform_file_stat(const char *path, platform_storage_file_info_t *info);
platform_fs_result_t platform_file_delete(const char *path);
platform_fs_result_t platform_dir_open(platform_storage_dir_t *dir, const char *path);
platform_fs_result_t platform_dir_read(platform_storage_dir_t *dir,
                                       platform_storage_file_info_t *info,
                                       bool *has_entry);
platform_fs_result_t platform_dir_close(platform_storage_dir_t *dir);

#endif
