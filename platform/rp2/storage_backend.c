#include "storage/backend.h"

#include <string.h>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/error.h"
#include "pico/flash.h"

#define RP2_STORAGE_OFFSET 0x1f0000u
#define RP2_STORAGE_TIMEOUT_MS 1000u

typedef struct {
    const uint8_t *image;
    size_t image_size;
    size_t volume_size;
} storage_format_args_t;

typedef struct {
    uint32_t offset;
    const uint8_t *buffer;
    size_t size;
} storage_write_args_t;

static const uint8_t *storage_xip_address(uint32_t offset)
{
    return (const uint8_t *)(XIP_BASE + RP2_STORAGE_OFFSET + offset);
}

static void storage_format_execute(void *param)
{
    storage_format_args_t *args = param;
    flash_range_erase(RP2_STORAGE_OFFSET, args->volume_size);
    flash_range_program(RP2_STORAGE_OFFSET, args->image, args->image_size);
}

static void storage_write_execute(void *param)
{
    storage_write_args_t *args = param;
    uint32_t offset = args->offset;
    const uint8_t *buffer = args->buffer;
    size_t size = args->size;
    uint8_t sector_data[FLASH_SECTOR_SIZE];

    while (size) {
        uint32_t sector_offset = offset % FLASH_SECTOR_SIZE;
        uint32_t sector_base = offset - sector_offset;
        size_t chunk = FLASH_SECTOR_SIZE - sector_offset;
        if (chunk > size)
            chunk = size;

        const uint8_t *flash_data = storage_xip_address(sector_base);
        memcpy(sector_data, flash_data, sizeof(sector_data));
        if (memcmp(sector_data + sector_offset, buffer, chunk) != 0) {
            memcpy(sector_data + sector_offset, buffer, chunk);
            flash_range_erase(RP2_STORAGE_OFFSET + sector_base, FLASH_SECTOR_SIZE);
            flash_range_program(RP2_STORAGE_OFFSET + sector_base, sector_data, FLASH_SECTOR_SIZE);
        }

        offset += (uint32_t)chunk;
        buffer += chunk;
        size -= chunk;
    }
}

bool storage_backend_init(void)
{
    return true;
}

bool storage_backend_read(uint32_t offset, void *buffer, size_t size)
{
    memcpy(buffer, storage_xip_address(offset), size);
    return true;
}

bool storage_backend_write(uint32_t offset, const void *buffer, size_t size)
{
    storage_write_args_t args = {
        .offset = offset,
        .buffer = buffer,
        .size = size,
    };
    return flash_safe_execute(storage_write_execute, &args, RP2_STORAGE_TIMEOUT_MS) == PICO_OK;
}

bool storage_backend_format(const void *image, size_t image_size, size_t volume_size)
{
    uint8_t image_copy[image_size];
    memcpy(image_copy, image, image_size);

    storage_format_args_t args = {
        .image = image_copy,
        .image_size = image_size,
        .volume_size = volume_size,
    };
    return flash_safe_execute(storage_format_execute, &args, RP2_STORAGE_TIMEOUT_MS) == PICO_OK;
}
