#include "storage/backend.h"

#include "ff.h"
#include "diskio.h"
#include "hardware/flash.h"
#include "platform/storage.h"

static bool storage_sector_range_valid(LBA_t sector, UINT count)
{
    return sector < PLATFORM_STORAGE_BLOCK_COUNT &&
           count <= PLATFORM_STORAGE_BLOCK_COUNT - sector;
}

DSTATUS disk_status(BYTE drv)
{
    (void)drv;
    return storage_backend_init() ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE drv)
{
    (void)drv;
    return storage_backend_init() ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE drv, BYTE *buffer, LBA_t sector, UINT count)
{
    (void)drv;
    if (!storage_sector_range_valid(sector, count))
        return RES_PARERR;

    uint32_t offset = (uint32_t)sector * PLATFORM_STORAGE_BLOCK_SIZE;
    size_t size = (size_t)count * PLATFORM_STORAGE_BLOCK_SIZE;
    return platform_storage_read(offset, buffer, size) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE drv, const BYTE *buffer, LBA_t sector, UINT count)
{
    (void)drv;
    if (!storage_sector_range_valid(sector, count))
        return RES_PARERR;

    uint32_t offset = (uint32_t)sector * PLATFORM_STORAGE_BLOCK_SIZE;
    size_t size = (size_t)count * PLATFORM_STORAGE_BLOCK_SIZE;
    return platform_storage_write(offset, buffer, size) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE drv, BYTE ctrl, void *buffer)
{
    (void)drv;
    switch (ctrl) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buffer = PLATFORM_STORAGE_BLOCK_COUNT;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buffer = PLATFORM_STORAGE_BLOCK_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buffer = FLASH_SECTOR_SIZE / PLATFORM_STORAGE_BLOCK_SIZE;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

DWORD get_fattime(void)
{
    return 0;
}

bool storage_diskio_register(void)
{
    return true;
}
