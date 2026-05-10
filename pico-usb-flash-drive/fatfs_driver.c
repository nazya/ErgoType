#include "ff.h"
#include "diskio.h"

#include "flash.h"
#include "log.h"

#define FAT_MAGIC  (0x55AA)

static DSTATUS Stat = RES_OK; // STA_NOINIT;


DSTATUS disk_status(BYTE drv) {
    (void)drv;
    return Stat;
}

DSTATUS disk_initialize(BYTE drv) {
    (void)drv;
    uint8_t block[FAT_BLOCK_SIZE];
    flash_fat_read(0, block);

    uint16_t magic = block[FAT_BLOCK_SIZE - 2] << 8 | block[FAT_BLOCK_SIZE - 1];
    if (magic == FAT_MAGIC) {
        Stat = RES_OK;
        return Stat;
    }

    dbg("fatfs: disk image not formatted, initializing FAT12");
    flash_fat_initialize();

    Stat = RES_OK;
    return Stat;
}

DRESULT disk_read(BYTE drv, BYTE *buff, LBA_t sector, UINT count) {
    (void)drv;
    if (sector >= FAT_BLOCK_NUM || (sector + count) > FAT_BLOCK_NUM) {
        return RES_PARERR;
    }
    for (UINT i = 0; i < count; ++i) {
        flash_fat_read((int)(sector + i), (uint8_t *)(buff + (i * FAT_BLOCK_SIZE)));
    }
    return RES_OK;
}

DRESULT disk_write(BYTE drv, const BYTE *buff, LBA_t sector, UINT count) {
    (void)drv;
    if (sector >= FAT_BLOCK_NUM || (sector + count) > FAT_BLOCK_NUM) {
        return RES_PARERR;
    }
    for (UINT i = 0; i < count; ++i) {
        if (!flash_fat_write((int)(sector + i), (uint8_t *)(buff + (i * FAT_BLOCK_SIZE)))) {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE drv, BYTE ctrl, void *buff) {
    (void)drv;
    switch (ctrl) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = FAT_BLOCK_NUM;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = FAT_BLOCK_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = (DWORD)(FLASH_SECTOR_SIZE / FAT_BLOCK_SIZE);
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

DWORD get_fattime (void) {
    return 0;
}
