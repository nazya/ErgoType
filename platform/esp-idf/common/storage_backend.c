#include "storage/backend.h"

#include <stdlib.h>
#include <string.h>

#include "esp_partition.h"

#define ESP_STORAGE_PARTITION_LABEL "storage"
#define ESP_STORAGE_ERASE_SIZE 4096u

static const esp_partition_t *storage_partition;

bool storage_backend_init(void)
{
    if (!storage_partition) {
        storage_partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_DATA_FAT,
            ESP_STORAGE_PARTITION_LABEL);
    }
    return storage_partition != NULL;
}

bool storage_backend_read(uint32_t offset, void *buffer, size_t size)
{
    return esp_partition_read(storage_partition, offset, buffer, size) == ESP_OK;
}

bool storage_backend_write(uint32_t offset, const void *buffer, size_t size)
{
    uint8_t *sector_data = malloc(ESP_STORAGE_ERASE_SIZE);
    if (!sector_data)
        return false;

    const uint8_t *source = buffer;
    bool ok = true;
    while (size) {
        uint32_t sector_offset = offset % ESP_STORAGE_ERASE_SIZE;
        uint32_t sector_base = offset - sector_offset;
        size_t chunk = ESP_STORAGE_ERASE_SIZE - sector_offset;
        if (chunk > size)
            chunk = size;

        ok = esp_partition_read(
            storage_partition,
            sector_base,
            sector_data,
            ESP_STORAGE_ERASE_SIZE) == ESP_OK;
        if (!ok)
            break;

        if (memcmp(sector_data + sector_offset, source, chunk) != 0) {
            memcpy(sector_data + sector_offset, source, chunk);
            ok = esp_partition_erase_range(
                storage_partition,
                sector_base,
                ESP_STORAGE_ERASE_SIZE) == ESP_OK;
            if (ok) {
                ok = esp_partition_write(
                    storage_partition,
                    sector_base,
                    sector_data,
                    ESP_STORAGE_ERASE_SIZE) == ESP_OK;
            }
            if (!ok)
                break;
        }

        offset += (uint32_t)chunk;
        source += chunk;
        size -= chunk;
    }

    free(sector_data);
    return ok;
}

bool storage_backend_format(const void *image, size_t image_size, size_t volume_size)
{
    return esp_partition_erase_range(storage_partition, 0, volume_size) == ESP_OK &&
           esp_partition_write(storage_partition, 0, image, image_size) == ESP_OK;
}
