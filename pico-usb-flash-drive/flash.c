#include "flash.h"
// #include "FreeRTOS.h"
// #include "task.h"

#define DEFAULT_CONTENTS       "counter=0\n"


uint8_t disk_image[4][FAT_BLOCK_SIZE] =
{
  {
      0xEB, 0x3C, 0x90,
      0x4D, 0x53, 0x44, 0x4F, 0x53, 0x35, 0x2E, 0x30,
      0x00, 0x02,  // BPB_BytePerSec
      0x01,        // BPB_SecPerClus
      0x01, 0x00,  // BPB_RsvdSecCnt
      0x01,        // BPB_NumFATs
      0x10, 0x00,  // BPB_RootEntCnt
      FAT_BLOCK_NUM & 0xFF, FAT_BLOCK_NUM >> 8,  // BPB_TotSec16
      0xF8,        // BPB_Media
      0x01, 0x00,  // BPB_FATSz16
      0x01, 0x00,  // BPB_SecPerTrk
      0x01, 0x00,  // BPB_NumHeads
      0x00, 0x00, 0x00, 0x00, // BPB_HiddSec
      0x00, 0x00, 0x00, 0x00, // BPB_TotSec32
      0x80, 0x00, 0x29, 0x34, 0x12, 0x00, 0x00, 'P' , 'I' , 'C' , 'O' , '_' ,
      'F' , 'S' , ' ' , ' ' , ' ' , ' ' , 0x46, 0x41, 0x54, 0x31, 0x32, 0x20, 0x20, 0x20, 0x00, 0x00,

      // Zero up to 2 last bytes of FAT magic code
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0xAA
  },

  //------------- Block1: FAT12 Table -------------//
  {
      0xF8, 0xFF, 0xFF, 0xFF, 0x0F // // first 2 entries must be F8FF, third entry is cluster end of readme file
  },
  //------------- Block2: Root Directory -------------//
  {
      // first entry is volume label
      'P' , 'I' , 'C' , 'O' , '_' , 'F' , 'S' , ' ' , ' ' , ' ' , ' ' , 0x08, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4F, 0x6D, 0x65, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      // second entry is readme file
      'R' , 'E' , 'A' , 'D' , 'M' , 'E' , ' ' , ' ' , 'T' , 'X' , 'T' , 0x20, 0x00, 0xC6, 0x52, 0x6D,
      0x65, 0x43, 0x65, 0x43, 0x00, 0x00, 0x88, 0x6D, 0x65, 0x43, 0x02, 0x00,
      sizeof(DEFAULT_CONTENTS) - 1, 0x00, 0x00, 0x00 // readme's files size
  },

  //------------- Block3: Readme Content -------------//
  { DEFAULT_CONTENTS },
};


void flash_fat_initialize(void) {
    printf("ffInit started\n");

    // taskENTER_CRITICAL();
    // uint32_t ints = save_and_disable_interrupts();

#if defined(ERASE_ALL_FLASH)
    flash_range_erase(FLASH_FAT_OFFSET, FLASH_SECTOR_SIZE * 16);
#else
    flash_range_erase(FLASH_FAT_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_FAT_OFFSET, (uint8_t *)disk_image, sizeof(disk_image));
#endif
    // taskEXIT_CRITICAL();

    // restore_interrupts(ints);
    printf("ffInit finished\n");

}

bool flash_fat_read(int block, uint8_t *buffer) {
    const uint8_t *data = (uint8_t *)(XIP_BASE + FLASH_FAT_OFFSET + FAT_BLOCK_SIZE * block);
    memcpy(buffer, data, FAT_BLOCK_SIZE);
    return true;
}

typedef struct {
    int block;
    uint8_t *buffer;
} flash_arg_t;

void _flash_fat_write(flash_arg_t* args);

int once = 0;

bool flash_fat_write(int block, uint8_t *buffer) {
    flash_arg_t args = { .block = block, .buffer = buffer };
 
    // int res = 0;
    int res = flash_safe_execute(( void* )_flash_fat_write, &args, 1000);
//    if (res == PICO_ERROR_NOT_PERMITTED) { while (1) {} }
    return res;
}

void _flash_fat_write(flash_arg_t* args) {
    int block = args->block;
    uint8_t* buffer = args->buffer;

    /*
     * NOTE: Flash memory must be erased and updated in blocks of 4096 bytes
     *       from the head, and updating at the halfway boundary will (probably)
     *       lead to undefined results.
     */
    uint8_t data[FLASH_SECTOR_SIZE];  // 4096 byte

    // Obtain the location of the FAT sector(512 byte) in the flash memory sector(4096 byte).
    int flash_sector = floor((block * FAT_BLOCK_SIZE) / FLASH_SECTOR_SIZE);
    int flash_sector_fat_offset = (block * FAT_BLOCK_SIZE) % FLASH_SECTOR_SIZE;
    // Retrieve the data in the flash memory sector and update only the data for the FAT sector.
    memcpy(data, (uint8_t *)(XIP_BASE + FLASH_FAT_OFFSET + FLASH_SECTOR_SIZE * flash_sector), sizeof(data));
    memcpy(data + flash_sector_fat_offset, buffer, FAT_BLOCK_SIZE);

    // Clear and update flash sectors.
//    stdio_set_driver_enabled(&stdio_usb, false);
//    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_FAT_OFFSET + flash_sector * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_FAT_OFFSET + flash_sector * FLASH_SECTOR_SIZE, data, FLASH_SECTOR_SIZE);
//    restore_interrupts(ints);
//    stdio_set_driver_enabled(&stdio_usb, true);
}
