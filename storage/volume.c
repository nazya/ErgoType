#include "platform/storage.h"

#include "log.h"
#include "storage/backend.h"
#include "storage/fat12_image.h"

static bool storage_range_valid(uint32_t offset, size_t size)
{
    return offset <= PLATFORM_STORAGE_SIZE &&
           size <= (size_t)PLATFORM_STORAGE_SIZE - offset;
}

uint32_t platform_storage_block_count(void)
{
    return PLATFORM_STORAGE_BLOCK_COUNT;
}

uint16_t platform_storage_block_size(void)
{
    return PLATFORM_STORAGE_BLOCK_SIZE;
}

bool platform_storage_mount(void)
{
    return storage_backend_init() && storage_diskio_register();
}

void platform_storage_format(void)
{
    if (!platform_storage_mount())
        return;

    warn("storage: init FAT12 image");
    if (!storage_backend_format(disk_image, sizeof(disk_image), PLATFORM_STORAGE_SIZE))
        err("storage: format failed");
}

bool platform_storage_read(uint32_t offset, void *buffer, size_t size)
{
    return storage_range_valid(offset, size) &&
           storage_backend_init() &&
           storage_backend_read(offset, buffer, size);
}

bool platform_storage_write(uint32_t offset, const void *buffer, size_t size)
{
    return storage_range_valid(offset, size) &&
           storage_backend_init() &&
           storage_backend_write(offset, buffer, size);
}
