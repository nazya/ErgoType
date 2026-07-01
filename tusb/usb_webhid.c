#include "usb_webhid.h"

#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "ff.h"
#include "flash.h"
#include "usb_descriptors.h"

extern SemaphoreHandle_t fatfs_mutex;

enum command {
    CMD_GET_STATUS = 1,
    CMD_FILE_OPEN_READ,
    CMD_FILE_READ_CHUNK,
    CMD_FILE_WRITE_BEGIN,
    CMD_FILE_WRITE_CHUNK,
    CMD_FILE_WRITE_COMMIT,
    CMD_FILE_WRITE_ABORT,
    CMD_FILE_LIST_OPEN,
    CMD_FILE_LIST_READ_CHUNK,
    CMD_FILE_DELETE,
    CMD_FILE_READ_END,
};

enum status {
    STATUS_OK = 0,
    STATUS_UNKNOWN_COMMAND = 1,
    STATUS_BAD_REQUEST = 2,
    STATUS_FS_ERROR = 3,
    STATUS_BUSY = 4,
};

#define PATH_MAX_LEN 60
#define FILE_READ_DATA_OFFSET 13
#define FILE_READ_DATA_SIZE (WEBHID_REPORT_SIZE - FILE_READ_DATA_OFFSET)
#define FILE_LIST_DATA_OFFSET 13
#define FILE_LIST_DATA_SIZE (WEBHID_REPORT_SIZE - FILE_LIST_DATA_OFFSET)
#define FILE_WRITE_DATA_OFFSET 7
#define FILE_WRITE_DATA_SIZE (WEBHID_REPORT_SIZE - FILE_WRITE_DATA_OFFSET)
#define FILE_WRITE_BUFFER_SIZE 4096
#define FILE_LIST_BUFFER_SIZE 4096
#define PATH_OFFSET 2
#define WRITE_PATH_OFFSET 6
#define FATFS_MOUNT_NOW 1

static uint8_t response[WEBHID_REPORT_SIZE];
static char path[PATH_MAX_LEN + 1];
static FATFS* read_fs;
static FIL* read_file;
static uint32_t read_size;
static uint32_t read_offset;
static bool read_active;
static uint8_t* list_buffer;
static uint32_t list_size;
static uint8_t* write_buffer;
static uint32_t write_size;
static uint32_t write_capacity;
static uint32_t write_flushed;
static uint32_t write_buffer_size;
static bool write_active;

static uint32_t get_u32(uint8_t const* buffer)
{
    return (uint32_t)buffer[0] |
        ((uint32_t)buffer[1] << 8) |
        ((uint32_t)buffer[2] << 16) |
        ((uint32_t)buffer[3] << 24);
}

static void put_u32(uint8_t* buffer, uint32_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 24);
}

static void response_begin(uint8_t command)
{
    memset(response, 0, sizeof(response));
    response[0] = WEBHID_PROTOCOL_VERSION;
    response[1] = command;
}

static void response_status(uint8_t status, FRESULT fs_result)
{
    response[2] = status;
    response[3] = (uint8_t)fs_result;
}

static bool copy_path_at(char* dst, uint8_t const* buffer, uint16_t bufsize, uint16_t offset)
{
    if (bufsize <= offset)
        return false;

    uint8_t len = buffer[offset];

    if (len == 0 || len > PATH_MAX_LEN || len > bufsize - offset - 1)
        return false;

    for (uint8_t i = 0; i < len; i++) {
        char c = (char)buffer[offset + 1 + i];

        if (c == '/' || c == '\\' || c == ':')
            return false;

        dst[i] = c;
    }

    dst[len] = '\0';

    return true;
}

static void read_reset(void)
{
    if (read_active) {
        f_close(read_file);
        f_unmount("/");
        xSemaphoreGive(fatfs_mutex);
    }

    vPortFree(read_file);
    vPortFree(read_fs);
    read_file = NULL;
    read_fs = NULL;
    read_size = 0;
    read_offset = 0;
    read_active = false;
    path[0] = '\0';
}

static void list_reset(void)
{
    vPortFree(list_buffer);
    list_buffer = NULL;
    list_size = 0;
}

static void write_reset(void)
{
    vPortFree(write_buffer);
    write_buffer = NULL;
    write_size = 0;
    write_capacity = 0;
    write_flushed = 0;
    write_buffer_size = 0;
    write_active = false;
    path[0] = '\0';
}

static void file_open_read(uint8_t const* buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_OPEN_READ);

    read_reset();

    if (xSemaphoreTake(fatfs_mutex, 0) != pdTRUE) {
        response_status(STATUS_BUSY, FR_OK);
        return;
    }

    if (!copy_path_at(path, buffer, bufsize, PATH_OFFSET)) {
        xSemaphoreGive(fatfs_mutex);
        response_status(STATUS_BAD_REQUEST, FR_OK);
        return;
    }

    read_fs = pvPortMalloc(sizeof(*read_fs));
    read_file = pvPortMalloc(sizeof(*read_file));
    if (read_fs == NULL || read_file == NULL) {
        xSemaphoreGive(fatfs_mutex);
        read_reset();
        response_status(STATUS_FS_ERROR, FR_NOT_ENOUGH_CORE);
        return;
    }

    FILINFO fno;
    FRESULT res = f_mount(read_fs, "/", FATFS_MOUNT_NOW);
    if (res == FR_OK)
        res = f_stat(path, &fno);
    if (res == FR_OK)
        res = f_open(read_file, path, FA_READ);

    if (res != FR_OK) {
        response_status(STATUS_FS_ERROR, res);
        f_unmount("/");
        xSemaphoreGive(fatfs_mutex);
        read_reset();
        return;
    }

    read_size = fno.fsize;
    read_offset = 0;
    read_active = true;
    put_u32(&response[4], read_size);
    response_status(STATUS_OK, FR_OK);
}

static void file_read_chunk(uint8_t const* buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_READ_CHUNK);

    if (bufsize < 6 || !read_active) {
        response_status(STATUS_BAD_REQUEST, FR_OK);
        return;
    }

    uint32_t offset = get_u32(&buffer[2]);
    if (offset > read_size || offset != read_offset) {
        response_status(STATUS_BAD_REQUEST, FR_OK);
        return;
    }

    put_u32(&response[4], read_size);
    put_u32(&response[8], offset);

    uint32_t bytes_left = read_size - offset;
    UINT bytes_to_read = bytes_left > FILE_READ_DATA_SIZE ? FILE_READ_DATA_SIZE : bytes_left;
    UINT bytes_read = 0;

    FRESULT res = f_read(read_file, &response[FILE_READ_DATA_OFFSET], bytes_to_read, &bytes_read);
    if (res != FR_OK || bytes_read != bytes_to_read) {
        read_reset();
        response_status(STATUS_FS_ERROR, res);
        return;
    }

    response[12] = (uint8_t)bytes_to_read;
    response_status(STATUS_OK, FR_OK);
    read_offset += bytes_to_read;

    if (offset + bytes_to_read == read_size)
        read_reset();
}

static void file_read_end(void)
{
    response_begin(CMD_FILE_READ_END);
    read_reset();
    response_status(STATUS_OK, FR_OK);
}

static bool list_append_char(char c)
{
    if (list_size >= FILE_LIST_BUFFER_SIZE)
        return false;

    list_buffer[list_size++] = (uint8_t)c;
    return true;
}

static bool list_append_str(char const* s)
{
    while (*s) {
        if (!list_append_char(*s++))
            return false;
    }

    return true;
}

static bool list_append_u32(uint32_t value)
{
    if (value == 0)
        return list_append_char('0');

    char digits[10];
    int pos = 0;

    while (value && pos < (int)sizeof(digits)) {
        digits[pos++] = (char)('0' + value % 10);
        value /= 10;
    }

    while (pos) {
        if (!list_append_char(digits[--pos]))
            return false;
    }

    return true;
}

static void file_list_open(void)
{
    response_begin(CMD_FILE_LIST_OPEN);

    if (xSemaphoreTake(fatfs_mutex, 0) != pdTRUE) {
        response_status(STATUS_BUSY, FR_OK);
        return;
    }

    list_reset();
    list_buffer = pvPortMalloc(FILE_LIST_BUFFER_SIZE);
    if (list_buffer == NULL) {
        xSemaphoreGive(fatfs_mutex);
        response_status(STATUS_FS_ERROR, FR_NOT_ENOUGH_CORE);
        return;
    }

    FATFS filesystem;
    DIR dir;
    bool dir_opened = false;
    FRESULT res = f_mount(&filesystem, "/", FATFS_MOUNT_NOW);
    if (res == FR_OK) {
        res = f_opendir(&dir, "/");
        dir_opened = res == FR_OK;
    }

    FILINFO fno;
    while (res == FR_OK) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == '\0')
            break;

        if (!list_append_u32(fno.fsize) ||
            !list_append_char('\t') ||
            !list_append_u32(fno.fattrib) ||
            !list_append_char('\t') ||
            !list_append_str(fno.fname) ||
            !list_append_char('\n')) {
            res = FR_NOT_ENOUGH_CORE;
            break;
        }
    }

    if (dir_opened && res != FR_OK)
        f_closedir(&dir);
    if (dir_opened && res == FR_OK)
        res = f_closedir(&dir);

    f_unmount("/");
    xSemaphoreGive(fatfs_mutex);

    if (res != FR_OK) {
        list_reset();
        response_status(STATUS_FS_ERROR, res);
        return;
    }

    put_u32(&response[4], list_size);
    response_status(STATUS_OK, FR_OK);
}

static void file_list_read_chunk(uint8_t const* buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_LIST_READ_CHUNK);

    if (bufsize < 6 || list_buffer == NULL) {
        response_status(STATUS_BAD_REQUEST, FR_OK);
        return;
    }

    uint32_t offset = get_u32(&buffer[2]);
    if (offset > list_size) {
        response_status(STATUS_BAD_REQUEST, FR_OK);
        return;
    }

    put_u32(&response[4], list_size);
    put_u32(&response[8], offset);

    uint32_t bytes_left = list_size - offset;
    UINT bytes_to_read = bytes_left > FILE_LIST_DATA_SIZE ? FILE_LIST_DATA_SIZE : bytes_left;

    memcpy(&response[FILE_LIST_DATA_OFFSET], &list_buffer[offset], bytes_to_read);
    response[12] = (uint8_t)bytes_to_read;
    response_status(STATUS_OK, FR_OK);

    if (offset + bytes_to_read == list_size)
        list_reset();
}

static void file_write_begin(uint8_t const* buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_WRITE_BEGIN);

    if (bufsize < 7) {
        response_status(STATUS_BAD_REQUEST, FR_OK);
        return;
    }

    if (xSemaphoreTake(fatfs_mutex, 0) != pdTRUE) {
        response_status(STATUS_BUSY, FR_OK);
        return;
    }

    write_reset();
    if (!copy_path_at(path, buffer, bufsize, WRITE_PATH_OFFSET)) {
        xSemaphoreGive(fatfs_mutex);
        response_status(STATUS_BAD_REQUEST, FR_OK);
        return;
    }

    uint32_t size = get_u32(&buffer[2]);
    write_buffer = pvPortMalloc(FILE_WRITE_BUFFER_SIZE);
    if (write_buffer == NULL) {
        write_reset();
        xSemaphoreGive(fatfs_mutex);
        response_status(STATUS_FS_ERROR, FR_NOT_ENOUGH_CORE);
        return;
    }

    FATFS filesystem;
    uint32_t free_bytes = 0;
    uint32_t old_size = 0;
    FRESULT res = f_mount(&filesystem, "/", FATFS_MOUNT_NOW);
    if (res == FR_OK) {
        FATFS* fs;
        DWORD free_clusters;
        res = f_getfree("/", &free_clusters, &fs);
        if (res == FR_OK)
            free_bytes = free_clusters * fs->csize * FAT_BLOCK_SIZE;
    }
    if (res == FR_OK) {
        FILINFO fno;
        res = f_stat(path, &fno);
        if (res == FR_OK) {
            old_size = fno.fsize;
        } else if (res == FR_NO_FILE || res == FR_NO_PATH) {
            res = FR_OK;
        }
    }
    if (res == FR_OK && size > free_bytes + old_size)
        res = FR_DENIED;
    if (res == FR_OK) {
        FIL file;
        res = f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS);
        if (res == FR_OK)
            res = f_close(&file);
    }

    if (res != FR_OK) {
        f_unmount("/");
        write_reset();
        xSemaphoreGive(fatfs_mutex);
        response_status(STATUS_FS_ERROR, res);
        return;
    }

    write_capacity = size;
    write_active = true;
    f_unmount("/");
    xSemaphoreGive(fatfs_mutex);
    response_status(STATUS_OK, FR_OK);
}

static FRESULT file_write_flush(void)
{
    if (!write_buffer_size)
        return FR_OK;

    FATFS filesystem;
    FRESULT res = f_mount(&filesystem, "/", FATFS_MOUNT_NOW);
    if (res == FR_OK) {
        FIL file;
        UINT bytes_written = 0;
        bool file_opened = false;
        bool file_closed = false;

        res = f_open(&file, path, FA_WRITE);
        file_opened = res == FR_OK;
        if (res == FR_OK)
            res = f_lseek(&file, write_flushed);
        if (res == FR_OK)
            res = f_write(&file, write_buffer, write_buffer_size, &bytes_written);
        if (res == FR_OK && bytes_written != write_buffer_size)
            res = FR_DISK_ERR;
        if (res == FR_OK) {
            res = f_close(&file);
            file_closed = true;
        }

        if (res != FR_OK && file_opened && !file_closed)
            f_close(&file);
    }

    f_unmount("/");

    if (res == FR_OK) {
        write_flushed += write_buffer_size;
        write_buffer_size = 0;
    }
    return res;
}

static void file_write_chunk(uint8_t const* buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_WRITE_CHUNK);

    if (!write_active || bufsize < FILE_WRITE_DATA_OFFSET) {
        response_status(STATUS_BAD_REQUEST, FR_OK);
        return;
    }

    uint32_t offset = get_u32(&buffer[2]);
    uint8_t bytes_to_write = buffer[6];
    uint32_t end = offset + bytes_to_write;

    if (bytes_to_write > FILE_WRITE_DATA_SIZE ||
        bytes_to_write > bufsize - FILE_WRITE_DATA_OFFSET ||
        end < offset ||
        end > write_capacity ||
        offset != write_size) {
        response_status(STATUS_BAD_REQUEST, FR_OK);
        return;
    }

    if (xSemaphoreTake(fatfs_mutex, 0) != pdTRUE) {
        response_status(STATUS_BUSY, FR_OK);
        return;
    }

    uint8_t bytes_copied = 0;
    while (bytes_copied < bytes_to_write) {
        uint32_t buffer_left = FILE_WRITE_BUFFER_SIZE - write_buffer_size;
        uint32_t chunk_left = bytes_to_write - bytes_copied;
        uint32_t copy_size = chunk_left < buffer_left ? chunk_left : buffer_left;

        memcpy(&write_buffer[write_buffer_size],
               &buffer[FILE_WRITE_DATA_OFFSET + bytes_copied],
               copy_size);
        write_buffer_size += copy_size;
        bytes_copied += copy_size;

        if (write_buffer_size == FILE_WRITE_BUFFER_SIZE) {
            FRESULT res = file_write_flush();
            if (res != FR_OK) {
                write_reset();
                xSemaphoreGive(fatfs_mutex);
                response_status(STATUS_FS_ERROR, res);
                return;
            }
        }
    }

    write_size = end;
    xSemaphoreGive(fatfs_mutex);

    put_u32(&response[4], offset);
    response[8] = bytes_to_write;
    response_status(STATUS_OK, FR_OK);
}

static void file_write_commit(void)
{
    response_begin(CMD_FILE_WRITE_COMMIT);

    if (!write_active || write_size != write_capacity) {
        response_status(STATUS_BAD_REQUEST, FR_OK);
        return;
    }

    if (xSemaphoreTake(fatfs_mutex, 0) != pdTRUE) {
        response_status(STATUS_BUSY, FR_OK);
        return;
    }

    FRESULT res = file_write_flush();

    if (res != FR_OK) {
        write_reset();
        xSemaphoreGive(fatfs_mutex);
        response_status(STATUS_FS_ERROR, res);
        return;
    }

    write_reset();
    xSemaphoreGive(fatfs_mutex);
    response_status(STATUS_OK, FR_OK);
}

static void file_write_abort(void)
{
    response_begin(CMD_FILE_WRITE_ABORT);

    if (!write_active) {
        response_status(STATUS_OK, FR_OK);
        return;
    }

    write_reset();
    response_status(STATUS_OK, FR_OK);
}

static void file_delete(uint8_t const* buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_DELETE);

    if (xSemaphoreTake(fatfs_mutex, 0) != pdTRUE) {
        response_status(STATUS_BUSY, FR_OK);
        return;
    }

    if (!copy_path_at(path, buffer, bufsize, PATH_OFFSET)) {
        xSemaphoreGive(fatfs_mutex);
        response_status(STATUS_BAD_REQUEST, FR_OK);
        return;
    }

    FATFS filesystem;
    FRESULT res = f_mount(&filesystem, "/", FATFS_MOUNT_NOW);
    if (res == FR_OK)
        res = f_unlink(path);

    f_unmount("/");
    xSemaphoreGive(fatfs_mutex);
    response_status(res == FR_OK ? STATUS_OK : STATUS_FS_ERROR, res);
}

static void get_status(void)
{
    response_status(STATUS_OK, FR_OK);
    response[3] = mode;
    response[4] = hid_output_profile;

    if (xSemaphoreTake(fatfs_mutex, 0) != pdTRUE) {
        response_status(STATUS_BUSY, FR_OK);
        return;
    }

    FATFS filesystem;
    FATFS* fs;
    DWORD free_clusters;
    uint32_t free_bytes = 0;
    uint32_t total_bytes = 0;
    FRESULT res = f_mount(&filesystem, "/", FATFS_MOUNT_NOW);
    if (res == FR_OK)
        res = f_getfree("/", &free_clusters, &fs);
    if (res == FR_OK) {
        DWORD total_clusters = fs->n_fatent - 2;
        total_bytes = total_clusters * fs->csize * FAT_BLOCK_SIZE;
        free_bytes = free_clusters * fs->csize * FAT_BLOCK_SIZE;
    }

    put_u32(&response[5], total_bytes);
    put_u32(&response[9], free_bytes);
    put_u32(&response[13], total_bytes - free_bytes);
    response[17] = (uint8_t)res;
    f_unmount("/");
    xSemaphoreGive(fatfs_mutex);
}

bool webhid_is_instance(uint8_t instance)
{
    return mode == HID &&
        hid_output_profile == HID_OUTPUT_PROFILE_NKRO_KB_MOUSE &&
        instance == HID_WEBHID_INSTANCE;
}

uint16_t webhid_get_report(uint8_t report_id, hid_report_type_t report_type,
                           uint8_t* buffer, uint16_t reqlen)
{
    if (report_id != REPORT_ID_WEBHID_CONFIG ||
        report_type != HID_REPORT_TYPE_FEATURE ||
        reqlen < WEBHID_REPORT_SIZE)
        return 0;

    memcpy(buffer, response, WEBHID_REPORT_SIZE);

    return WEBHID_REPORT_SIZE;
}

void webhid_set_report(uint8_t report_id, hid_report_type_t report_type,
                       uint8_t const* buffer, uint16_t bufsize)
{
    if (report_id != REPORT_ID_WEBHID_CONFIG ||
        report_type != HID_REPORT_TYPE_FEATURE ||
        bufsize < 2)
        return;

    response_begin(buffer[1]);

    if (buffer[0] != WEBHID_PROTOCOL_VERSION) {
        response_status(STATUS_BAD_REQUEST, FR_OK);
        return;
    }

    switch (buffer[1]) {
    case CMD_GET_STATUS:
        get_status();
        break;
    case CMD_FILE_OPEN_READ:
        file_open_read(buffer, bufsize);
        break;
    case CMD_FILE_READ_CHUNK:
        file_read_chunk(buffer, bufsize);
        break;
    case CMD_FILE_WRITE_BEGIN:
        file_write_begin(buffer, bufsize);
        break;
    case CMD_FILE_WRITE_CHUNK:
        file_write_chunk(buffer, bufsize);
        break;
    case CMD_FILE_WRITE_COMMIT:
        file_write_commit();
        break;
    case CMD_FILE_WRITE_ABORT:
        file_write_abort();
        break;
    case CMD_FILE_LIST_OPEN:
        file_list_open();
        break;
    case CMD_FILE_LIST_READ_CHUNK:
        file_list_read_chunk(buffer, bufsize);
        break;
    case CMD_FILE_DELETE:
        file_delete(buffer, bufsize);
        break;
    case CMD_FILE_READ_END:
        file_read_end();
        break;
    default:
        response_status(STATUS_UNKNOWN_COMMAND, FR_OK);
        break;
    }
}
