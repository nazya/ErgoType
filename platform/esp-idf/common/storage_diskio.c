#include "storage/backend.h"

#include "ff.h"
#include "diskio_impl.h"
#include "platform/storage.h"

#define ESP_STORAGE_PDRV 0
#define ESP_STORAGE_ERASE_SIZE 4096u

static bool storage_sector_range_valid(DWORD sector, UINT count)
{
    return sector < PLATFORM_STORAGE_BLOCK_COUNT &&
           count <= PLATFORM_STORAGE_BLOCK_COUNT - sector;
}

static DSTATUS storage_disk_initialize(BYTE pdrv)
{
    (void)pdrv;
    return storage_backend_init() ? 0 : STA_NOINIT;
}

static DSTATUS storage_disk_status(BYTE pdrv)
{
    (void)pdrv;
    return storage_backend_init() ? 0 : STA_NOINIT;
}

static DRESULT storage_disk_read(BYTE pdrv, BYTE *buffer, DWORD sector, UINT count)
{
    (void)pdrv;
    if (!storage_sector_range_valid(sector, count))
        return RES_PARERR;

    uint32_t offset = sector * PLATFORM_STORAGE_BLOCK_SIZE;
    size_t size = (size_t)count * PLATFORM_STORAGE_BLOCK_SIZE;
    return platform_storage_read(offset, buffer, size) ? RES_OK : RES_ERROR;
}

static DRESULT storage_disk_write(BYTE pdrv, const BYTE *buffer, DWORD sector, UINT count)
{
    (void)pdrv;
    if (!storage_sector_range_valid(sector, count))
        return RES_PARERR;

    uint32_t offset = sector * PLATFORM_STORAGE_BLOCK_SIZE;
    size_t size = (size_t)count * PLATFORM_STORAGE_BLOCK_SIZE;
    return platform_storage_write(offset, buffer, size) ? RES_OK : RES_ERROR;
}

static DRESULT storage_disk_ioctl(BYTE pdrv, BYTE cmd, void *buffer)
{
    (void)pdrv;
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buffer = PLATFORM_STORAGE_BLOCK_COUNT;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buffer = PLATFORM_STORAGE_BLOCK_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buffer = ESP_STORAGE_ERASE_SIZE / PLATFORM_STORAGE_BLOCK_SIZE;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

static const ff_diskio_impl_t storage_diskio = {
    .init = storage_disk_initialize,
    .status = storage_disk_status,
    .read = storage_disk_read,
    .write = storage_disk_write,
    .ioctl = storage_disk_ioctl,
};

bool storage_diskio_register(void)
{
    static bool registered;
    if (!registered) {
        ff_diskio_register(ESP_STORAGE_PDRV, &storage_diskio);
        registered = true;
    }
    return true;
}
