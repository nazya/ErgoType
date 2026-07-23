#include "platform/storage.h"

#include <string.h>

static void platform_fill_info(platform_storage_file_info_t *info, const FILINFO *fno)
{
    info->size = (uint32_t)fno->fsize;
    info->attributes = fno->fattrib;
    strncpy(info->name, fno->fname, PLATFORM_STORAGE_NAME_MAX);
    info->name[PLATFORM_STORAGE_NAME_MAX] = '\0';
}

platform_fs_result_t platform_fs_mount(platform_storage_fs_t *fs, bool mount_now)
{
    return (platform_fs_result_t)f_mount(fs, "/", mount_now ? 1 : 0);
}

platform_fs_result_t platform_fs_unmount(void)
{
    return (platform_fs_result_t)f_unmount("/");
}

platform_fs_result_t platform_fs_get_space(uint32_t *total_bytes, uint32_t *free_bytes)
{
    FATFS *fs;
    DWORD free_clusters;
    FRESULT res = f_getfree("/", &free_clusters, &fs);
    if (res == FR_OK) {
        DWORD total_clusters = fs->n_fatent - 2;
        *total_bytes = (uint32_t)(total_clusters * fs->csize * platform_storage_block_size());
        *free_bytes = (uint32_t)(free_clusters * fs->csize * platform_storage_block_size());
    }
    return (platform_fs_result_t)res;
}

platform_fs_result_t platform_file_open(platform_storage_file_t *file, const char *path, uint8_t mode)
{
    return (platform_fs_result_t)f_open(file, path, mode);
}

platform_fs_result_t platform_file_close(platform_storage_file_t *file)
{
    return (platform_fs_result_t)f_close(file);
}

platform_fs_result_t platform_file_read(platform_storage_file_t *file,
                                        void *buffer,
                                        uint32_t bytes_to_read,
                                        uint32_t *bytes_read)
{
    UINT nread = 0;
    FRESULT res = f_read(file, buffer, (UINT)bytes_to_read, &nread);
    *bytes_read = nread;
    return (platform_fs_result_t)res;
}

platform_fs_result_t platform_file_write(platform_storage_file_t *file,
                                         const void *buffer,
                                         uint32_t bytes_to_write,
                                         uint32_t *bytes_written)
{
    UINT nwritten = 0;
    FRESULT res = f_write(file, buffer, (UINT)bytes_to_write, &nwritten);
    *bytes_written = nwritten;
    return (platform_fs_result_t)res;
}

platform_fs_result_t platform_file_seek(platform_storage_file_t *file, uint32_t offset)
{
    return (platform_fs_result_t)f_lseek(file, offset);
}

char *platform_file_gets(char *buffer, int len, platform_storage_file_t *file)
{
    return f_gets(buffer, len, file);
}

platform_fs_result_t platform_file_stat(const char *path, platform_storage_file_info_t *info)
{
    FILINFO fno;
    FRESULT res = f_stat(path, &fno);
    if (res == FR_OK)
        platform_fill_info(info, &fno);
    return (platform_fs_result_t)res;
}

platform_fs_result_t platform_file_delete(const char *path)
{
    return (platform_fs_result_t)f_unlink(path);
}

platform_fs_result_t platform_dir_open(platform_storage_dir_t *dir, const char *path)
{
    return (platform_fs_result_t)f_opendir(dir, path);
}

platform_fs_result_t platform_dir_read(platform_storage_dir_t *dir,
                                       platform_storage_file_info_t *info,
                                       bool *has_entry)
{
    FILINFO fno;
    FRESULT res = f_readdir(dir, &fno);
    if (res == FR_OK) {
        *has_entry = fno.fname[0] != '\0';
        if (*has_entry)
            platform_fill_info(info, &fno);
    }
    return (platform_fs_result_t)res;
}

platform_fs_result_t platform_dir_close(platform_storage_dir_t *dir)
{
    return (platform_fs_result_t)f_closedir(dir);
}
