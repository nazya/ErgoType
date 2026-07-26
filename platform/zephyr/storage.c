#include "platform/storage.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

#define STORAGE_MOUNT_POINT "/lfs"
#define STORAGE_USER_DIR "/lfs/config"
#define STORAGE_UPLOAD_PATH "/lfs/.config-upload"
#define STORAGE_MAX_PATH 192

#if DT_NODE_EXISTS(DT_NODELABEL(storage_partition))
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);

static struct fs_mount_t storage_mount = {
    .type = FS_LITTLEFS,
    .fs_data = &storage,
    .storage_dev = (void *)PARTITION_ID(storage_partition),
    .mnt_point = STORAGE_MOUNT_POINT,
};
#endif

static bool storage_mounted;

static int make_storage_path(const char *path, char *out, size_t out_size)
{
    if (!path || path[0] == '\0')
        return -EINVAL;

    if (!strcmp(path, ".") || !strcmp(path, "..") ||
        strchr(path, '/') || strchr(path, '\\'))
        return -EINVAL;

    int n = snprintf(out, out_size, "%s/%s", STORAGE_USER_DIR, path);
    if (n < 0 || (size_t)n >= out_size)
        return -ENAMETOOLONG;

    return 0;
}

int platform_storage_mount(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(storage_partition))
    if (storage_mounted)
        return 0;

    int rc = fs_mount(&storage_mount);
    if (rc)
        return rc;

    rc = fs_mkdir(STORAGE_USER_DIR);
    if (rc && rc != -EEXIST) {
        (void)fs_unmount(&storage_mount);
        return rc;
    }

    storage_mounted = true;
    return 0;
#else
    return -ENODEV;
#endif
}

int platform_storage_format(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(storage_partition))
    if (storage_mounted) {
        int unmount_rc = fs_unmount(&storage_mount);
        if (unmount_rc)
            return unmount_rc;
        storage_mounted = false;
    }

    const struct flash_area *area;
    int rc = flash_area_open(PARTITION_ID(storage_partition), &area);
    if (rc)
        return rc;

    rc = flash_area_flatten(area, 0, area->fa_size);
    flash_area_close(area);
    if (rc)
        return rc;

    storage_mounted = false;
    return platform_storage_mount();
#else
    return -ENODEV;
#endif
}

int platform_storage_size(const char *path, size_t *size)
{
    if (!size)
        return -EINVAL;

    int rc = platform_storage_mount();
    if (rc)
        return rc;

    char full_path[STORAGE_MAX_PATH];
    rc = make_storage_path(path, full_path, sizeof(full_path));
    if (rc)
        return rc;

    struct fs_dirent entry;
    rc = fs_stat(full_path, &entry);
    if (rc)
        return rc;
    if (entry.type != FS_DIR_ENTRY_FILE)
        return -EISDIR;

    *size = entry.size;
    return 0;
}

int platform_storage_info(size_t *total, size_t *free)
{
    int rc = platform_storage_mount();
    if (rc)
        return rc;

    struct fs_statvfs stat;
    rc = fs_statvfs(STORAGE_MOUNT_POINT, &stat);
    if (rc)
        return rc;

    *total = (size_t)stat.f_blocks * stat.f_frsize;
    *free = (size_t)stat.f_bfree * stat.f_frsize;
    return 0;
}

int platform_storage_read(const char *path, uint8_t *buffer, size_t buffer_size, size_t *size_read)
{
    if (!buffer || !size_read)
        return -EINVAL;

    int rc = platform_storage_mount();
    if (rc)
        return rc;

    char full_path[STORAGE_MAX_PATH];
    rc = make_storage_path(path, full_path, sizeof(full_path));
    if (rc)
        return rc;

    struct fs_file_t file;
    fs_file_t_init(&file);

    rc = fs_open(&file, full_path, FS_O_READ);
    if (rc)
        return rc;

    ssize_t n = fs_read(&file, buffer, buffer_size);
    int close_rc = fs_close(&file);
    if (n < 0)
        return (int)n;
    if (close_rc)
        return close_rc;

    *size_read = (size_t)n;
    return 0;
}

int platform_storage_write(const char *path, const uint8_t *buffer, size_t size)
{
    if (!buffer && size)
        return -EINVAL;

    int rc = platform_storage_mount();
    if (rc)
        return rc;

    char full_path[STORAGE_MAX_PATH];
    rc = make_storage_path(path, full_path, sizeof(full_path));
    if (rc)
        return rc;

    (void)fs_unlink(full_path);

    struct fs_file_t file;
    fs_file_t_init(&file);

    rc = fs_open(&file, full_path, FS_O_CREATE | FS_O_WRITE);
    if (rc)
        return rc;

    ssize_t n = fs_write(&file, buffer, size);
    int close_rc = fs_close(&file);
    if (n < 0)
        return (int)n;
    if ((size_t)n != size)
        return -EIO;
    if (close_rc)
        return close_rc;

    return 0;
}

int platform_storage_upload_begin(void)
{
    int rc = platform_storage_mount();
    if (rc)
        return rc;

    (void)fs_unlink(STORAGE_UPLOAD_PATH);

    struct fs_file_t file;
    fs_file_t_init(&file);

    rc = fs_open(&file, STORAGE_UPLOAD_PATH, FS_O_CREATE | FS_O_WRITE);
    if (rc)
        return rc;

    return fs_close(&file);
}

int platform_storage_upload_write(const uint8_t *buffer, size_t size, size_t offset)
{
    if (!buffer && size)
        return -EINVAL;

    int rc = platform_storage_mount();
    if (rc)
        return rc;

    struct fs_file_t file;
    fs_file_t_init(&file);

    rc = fs_open(&file, STORAGE_UPLOAD_PATH, FS_O_WRITE);
    if (rc)
        return rc;

    rc = fs_seek(&file, offset, FS_SEEK_SET);
    if (rc) {
        fs_close(&file);
        return rc;
    }

    ssize_t n = fs_write(&file, buffer, size);
    int close_rc = fs_close(&file);
    if (n < 0)
        return (int)n;
    if ((size_t)n != size)
        return -EIO;
    if (close_rc)
        return close_rc;

    return 0;
}

int platform_storage_upload_commit(const char *path)
{
    int rc = platform_storage_mount();
    if (rc)
        return rc;

    char full_path[STORAGE_MAX_PATH];
    rc = make_storage_path(path, full_path, sizeof(full_path));
    if (rc)
        return rc;

    return fs_rename(STORAGE_UPLOAD_PATH, full_path);
}

int platform_storage_upload_abort(void)
{
    int rc = platform_storage_mount();
    if (rc)
        return rc;

    rc = fs_unlink(STORAGE_UPLOAD_PATH);
    return rc == -ENOENT ? 0 : rc;
}

int platform_storage_delete(const char *path)
{
    int rc = platform_storage_mount();
    if (rc)
        return rc;

    char full_path[STORAGE_MAX_PATH];
    rc = make_storage_path(path, full_path, sizeof(full_path));
    if (rc)
        return rc;

    return fs_unlink(full_path);
}

int platform_storage_list(char *buffer, size_t buffer_size, size_t *size_written)
{
    if (!buffer || !size_written)
        return -EINVAL;

    int rc = platform_storage_mount();
    if (rc)
        return rc;

    struct fs_dir_t dir;
    fs_dir_t_init(&dir);

    rc = fs_opendir(&dir, STORAGE_USER_DIR);
    if (rc)
        return rc;

    size_t written = 0;
    for (;;) {
        struct fs_dirent entry;
        rc = fs_readdir(&dir, &entry);
        if (rc || entry.name[0] == '\0')
            break;

        if (written >= buffer_size) {
            rc = -ENOMEM;
            break;
        }

        int n = snprintf(&buffer[written],
                         buffer_size - written,
                         "%zu\t%u\t%s\n",
                         (size_t)entry.size,
                         entry.type,
                         entry.name);
        if (n < 0 || (size_t)n >= buffer_size - written) {
            rc = -ENOMEM;
            break;
        }
        written += (size_t)n;
    }

    int close_rc = fs_closedir(&dir);
    if (!rc)
        rc = close_rc;
    if (rc)
        return rc;

    *size_written = written;
    return 0;
}
