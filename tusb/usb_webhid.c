#include "usb_webhid.h"

#include <string.h>

#include "platform/os.h"
#include "platform/storage.h"
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
    CMD_SESSION_RESET,
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
static platform_storage_fs_t* read_fs;
static platform_storage_file_t* read_file;
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

static void response_status(uint8_t status, platform_fs_result_t fs_result)
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
        platform_file_close(read_file);
        platform_fs_unmount();
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

void webhid_reset(void)
{
    /*
     * TinyUSB invokes this from the sole device owner after the previous
     * callback has unwound. A read transaction deliberately owns fatfs_mutex
     * across feature reports; bus reset/unplug is its exact abort edge.
     * LIST/WRITE retain only heap buffers, which are released here as well.
     */
    read_reset();
    list_reset();
    write_reset();
    memset(response, 0, sizeof(response));
}

static void file_open_read(uint8_t const* buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_OPEN_READ);

    read_reset();

    if (xSemaphoreTake(fatfs_mutex, 0) != pdTRUE) {
        response_status(STATUS_BUSY, PLATFORM_FS_OK);
        return;
    }

    if (!copy_path_at(path, buffer, bufsize, PATH_OFFSET)) {
        xSemaphoreGive(fatfs_mutex);
        response_status(STATUS_BAD_REQUEST, PLATFORM_FS_OK);
        return;
    }

    read_fs = pvPortMalloc(sizeof(*read_fs));
    read_file = pvPortMalloc(sizeof(*read_file));
    if (read_fs == NULL || read_file == NULL) {
        xSemaphoreGive(fatfs_mutex);
        read_reset();
        response_status(STATUS_FS_ERROR, PLATFORM_FS_NOT_ENOUGH_CORE);
        return;
    }

    platform_storage_file_info_t fno;
    platform_fs_result_t res = platform_fs_mount(read_fs, PLATFORM_FS_MOUNT_NOW);
    if (res == PLATFORM_FS_OK)
        res = platform_file_stat(path, &fno);
    if (res == PLATFORM_FS_OK)
        res = platform_file_open(read_file, path, PLATFORM_FS_READ);

    if (res != PLATFORM_FS_OK) {
        response_status(STATUS_FS_ERROR, res);
        platform_fs_unmount();
        xSemaphoreGive(fatfs_mutex);
        read_reset();
        return;
    }

    read_size = fno.size;
    read_offset = 0;
    read_active = true;
    put_u32(&response[4], read_size);
    response_status(STATUS_OK, PLATFORM_FS_OK);
}

static void file_read_chunk(uint8_t const* buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_READ_CHUNK);

    if (bufsize < 6 || !read_active) {
        response_status(STATUS_BAD_REQUEST, PLATFORM_FS_OK);
        return;
    }

    uint32_t offset = get_u32(&buffer[2]);
    if (offset > read_size || offset != read_offset) {
        response_status(STATUS_BAD_REQUEST, PLATFORM_FS_OK);
        return;
    }

    put_u32(&response[4], read_size);
    put_u32(&response[8], offset);

    uint32_t bytes_left = read_size - offset;
    uint32_t bytes_to_read = bytes_left > FILE_READ_DATA_SIZE ? FILE_READ_DATA_SIZE : bytes_left;
    uint32_t bytes_read = 0;

    platform_fs_result_t res = platform_file_read(read_file, &response[FILE_READ_DATA_OFFSET], bytes_to_read, &bytes_read);
    if (res != PLATFORM_FS_OK || bytes_read != bytes_to_read) {
        read_reset();
        response_status(STATUS_FS_ERROR, res);
        return;
    }

    response[12] = (uint8_t)bytes_to_read;
    response_status(STATUS_OK, PLATFORM_FS_OK);
    read_offset += bytes_to_read;

    if (offset + bytes_to_read == read_size)
        read_reset();
}

static void file_read_end(void)
{
    response_begin(CMD_FILE_READ_END);
    read_reset();
    response_status(STATUS_OK, PLATFORM_FS_OK);
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
        response_status(STATUS_BUSY, PLATFORM_FS_OK);
        return;
    }

    list_reset();
    list_buffer = pvPortMalloc(FILE_LIST_BUFFER_SIZE);
    if (list_buffer == NULL) {
        xSemaphoreGive(fatfs_mutex);
        response_status(STATUS_FS_ERROR, PLATFORM_FS_NOT_ENOUGH_CORE);
        return;
    }

    platform_storage_fs_t filesystem;
    platform_storage_dir_t dir;
    bool dir_opened = false;
    platform_fs_result_t res = platform_fs_mount(&filesystem, PLATFORM_FS_MOUNT_NOW);
    if (res == PLATFORM_FS_OK) {
        res = platform_dir_open(&dir, "/");
        dir_opened = res == PLATFORM_FS_OK;
    }

    platform_storage_file_info_t fno;
    bool has_entry = false;
    while (res == PLATFORM_FS_OK) {
        res = platform_dir_read(&dir, &fno, &has_entry);
        if (res != PLATFORM_FS_OK || !has_entry)
            break;

        if (!list_append_u32(fno.size) ||
            !list_append_char('\t') ||
            !list_append_u32(fno.attributes) ||
            !list_append_char('\t') ||
            !list_append_str(fno.name) ||
            !list_append_char('\n')) {
            res = PLATFORM_FS_NOT_ENOUGH_CORE;
            break;
        }
    }

    if (dir_opened && res != PLATFORM_FS_OK)
        platform_dir_close(&dir);
    if (dir_opened && res == PLATFORM_FS_OK)
        res = platform_dir_close(&dir);

    platform_fs_unmount();
    xSemaphoreGive(fatfs_mutex);

    if (res != PLATFORM_FS_OK) {
        list_reset();
        response_status(STATUS_FS_ERROR, res);
        return;
    }

    put_u32(&response[4], list_size);
    response_status(STATUS_OK, PLATFORM_FS_OK);
}

static void file_list_read_chunk(uint8_t const* buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_LIST_READ_CHUNK);

    if (bufsize < 6 || list_buffer == NULL) {
        response_status(STATUS_BAD_REQUEST, PLATFORM_FS_OK);
        return;
    }

    uint32_t offset = get_u32(&buffer[2]);
    if (offset > list_size) {
        response_status(STATUS_BAD_REQUEST, PLATFORM_FS_OK);
        return;
    }

    put_u32(&response[4], list_size);
    put_u32(&response[8], offset);

    uint32_t bytes_left = list_size - offset;
    uint32_t bytes_to_read = bytes_left > FILE_LIST_DATA_SIZE ? FILE_LIST_DATA_SIZE : bytes_left;

    memcpy(&response[FILE_LIST_DATA_OFFSET], &list_buffer[offset], bytes_to_read);
    response[12] = (uint8_t)bytes_to_read;
    response_status(STATUS_OK, PLATFORM_FS_OK);

    if (offset + bytes_to_read == list_size)
        list_reset();
}

static void file_write_begin(uint8_t const* buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_WRITE_BEGIN);

    if (bufsize < 7) {
        response_status(STATUS_BAD_REQUEST, PLATFORM_FS_OK);
        return;
    }

    if (xSemaphoreTake(fatfs_mutex, 0) != pdTRUE) {
        response_status(STATUS_BUSY, PLATFORM_FS_OK);
        return;
    }

    write_reset();
    if (!copy_path_at(path, buffer, bufsize, WRITE_PATH_OFFSET)) {
        xSemaphoreGive(fatfs_mutex);
        response_status(STATUS_BAD_REQUEST, PLATFORM_FS_OK);
        return;
    }

    uint32_t size = get_u32(&buffer[2]);
    write_buffer = pvPortMalloc(FILE_WRITE_BUFFER_SIZE);
    if (write_buffer == NULL) {
        write_reset();
        xSemaphoreGive(fatfs_mutex);
        response_status(STATUS_FS_ERROR, PLATFORM_FS_NOT_ENOUGH_CORE);
        return;
    }

    platform_storage_fs_t filesystem;
    uint32_t free_bytes = 0;
    uint32_t total_bytes = 0;
    uint32_t old_size = 0;
    platform_fs_result_t res = platform_fs_mount(&filesystem, PLATFORM_FS_MOUNT_NOW);
    if (res == PLATFORM_FS_OK)
        res = platform_fs_get_space(&total_bytes, &free_bytes);
    if (res == PLATFORM_FS_OK) {
        platform_storage_file_info_t fno;
        res = platform_file_stat(path, &fno);
        if (res == PLATFORM_FS_OK) {
            old_size = fno.size;
        } else if (res == PLATFORM_FS_NO_FILE || res == PLATFORM_FS_NO_PATH) {
            res = PLATFORM_FS_OK;
        }
    }
    if (res == PLATFORM_FS_OK && size > free_bytes + old_size)
        res = PLATFORM_FS_DENIED;
    if (res == PLATFORM_FS_OK) {
        platform_storage_file_t file;
        res = platform_file_open(&file, path, PLATFORM_FS_WRITE | PLATFORM_FS_CREATE_ALWAYS);
        if (res == PLATFORM_FS_OK)
            res = platform_file_close(&file);
    }

    if (res != PLATFORM_FS_OK) {
        platform_fs_unmount();
        write_reset();
        xSemaphoreGive(fatfs_mutex);
        response_status(STATUS_FS_ERROR, res);
        return;
    }

    write_capacity = size;
    write_active = true;
    platform_fs_unmount();
    xSemaphoreGive(fatfs_mutex);
    response_status(STATUS_OK, PLATFORM_FS_OK);
}

static platform_fs_result_t file_write_flush(void)
{
    if (!write_buffer_size)
        return PLATFORM_FS_OK;

    platform_storage_fs_t filesystem;
    platform_fs_result_t res = platform_fs_mount(&filesystem, PLATFORM_FS_MOUNT_NOW);
    if (res == PLATFORM_FS_OK) {
        platform_storage_file_t file;
        uint32_t bytes_written = 0;
        bool file_opened = false;
        bool file_closed = false;

        res = platform_file_open(&file, path, PLATFORM_FS_WRITE);
        file_opened = res == PLATFORM_FS_OK;
        if (res == PLATFORM_FS_OK)
            res = platform_file_seek(&file, write_flushed);
        if (res == PLATFORM_FS_OK)
            res = platform_file_write(&file, write_buffer, write_buffer_size, &bytes_written);
        if (res == PLATFORM_FS_OK && bytes_written != write_buffer_size)
            res = PLATFORM_FS_DISK_ERR;
        if (res == PLATFORM_FS_OK) {
            res = platform_file_close(&file);
            file_closed = true;
        }

        if (res != PLATFORM_FS_OK && file_opened && !file_closed)
            platform_file_close(&file);
    }

    platform_fs_unmount();

    if (res == PLATFORM_FS_OK) {
        write_flushed += write_buffer_size;
        write_buffer_size = 0;
    }
    return res;
}

static void file_write_chunk(uint8_t const* buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_WRITE_CHUNK);

    if (!write_active || bufsize < FILE_WRITE_DATA_OFFSET) {
        response_status(STATUS_BAD_REQUEST, PLATFORM_FS_OK);
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
        response_status(STATUS_BAD_REQUEST, PLATFORM_FS_OK);
        return;
    }

    if (xSemaphoreTake(fatfs_mutex, 0) != pdTRUE) {
        response_status(STATUS_BUSY, PLATFORM_FS_OK);
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
            platform_fs_result_t res = file_write_flush();
            if (res != PLATFORM_FS_OK) {
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
    response_status(STATUS_OK, PLATFORM_FS_OK);
}

static void file_write_commit(void)
{
    response_begin(CMD_FILE_WRITE_COMMIT);

    if (!write_active || write_size != write_capacity) {
        response_status(STATUS_BAD_REQUEST, PLATFORM_FS_OK);
        return;
    }

    if (xSemaphoreTake(fatfs_mutex, 0) != pdTRUE) {
        response_status(STATUS_BUSY, PLATFORM_FS_OK);
        return;
    }

    platform_fs_result_t res = file_write_flush();

    if (res != PLATFORM_FS_OK) {
        write_reset();
        xSemaphoreGive(fatfs_mutex);
        response_status(STATUS_FS_ERROR, res);
        return;
    }

    write_reset();
    xSemaphoreGive(fatfs_mutex);
    response_status(STATUS_OK, PLATFORM_FS_OK);
}

static void file_write_abort(void)
{
    response_begin(CMD_FILE_WRITE_ABORT);

    if (!write_active) {
        response_status(STATUS_OK, PLATFORM_FS_OK);
        return;
    }

    write_reset();
    response_status(STATUS_OK, PLATFORM_FS_OK);
}

static void file_delete(uint8_t const* buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_DELETE);

    if (xSemaphoreTake(fatfs_mutex, 0) != pdTRUE) {
        response_status(STATUS_BUSY, PLATFORM_FS_OK);
        return;
    }

    if (!copy_path_at(path, buffer, bufsize, PATH_OFFSET)) {
        xSemaphoreGive(fatfs_mutex);
        response_status(STATUS_BAD_REQUEST, PLATFORM_FS_OK);
        return;
    }

    platform_storage_fs_t filesystem;
    platform_fs_result_t res = platform_fs_mount(&filesystem, PLATFORM_FS_MOUNT_NOW);
    if (res == PLATFORM_FS_OK)
        res = platform_file_delete(path);

    platform_fs_unmount();
    xSemaphoreGive(fatfs_mutex);
    response_status(res == PLATFORM_FS_OK ? STATUS_OK : STATUS_FS_ERROR, res);
}

static void get_status(void)
{
    response_status(STATUS_OK, PLATFORM_FS_OK);
    response[3] = mode;
    response[4] = hid_output_profile;

    if (xSemaphoreTake(fatfs_mutex, 0) != pdTRUE) {
        response_status(STATUS_BUSY, PLATFORM_FS_OK);
        return;
    }

    platform_storage_fs_t filesystem;
    uint32_t free_bytes = 0;
    uint32_t total_bytes = 0;
    platform_fs_result_t res = platform_fs_mount(&filesystem, PLATFORM_FS_MOUNT_NOW);
    if (res == PLATFORM_FS_OK)
        res = platform_fs_get_space(&total_bytes, &free_bytes);

    put_u32(&response[5], total_bytes);
    put_u32(&response[9], free_bytes);
    put_u32(&response[13], total_bytes - free_bytes);
    response[17] = (uint8_t)res;
    platform_fs_unmount();
    xSemaphoreGive(fatfs_mutex);
}

bool webhid_is_instance(uint8_t instance)
{
    return mode == HID &&
        hid_output_profile == HID_OUTPUT_PROFILE_NKRO_KB_MOUSE &&
        instance == HID_NKRO_WEBHID_INSTANCE;
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
        response_status(STATUS_BAD_REQUEST, PLATFORM_FS_OK);
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
    case CMD_SESSION_RESET:
        webhid_reset();
        response_begin(CMD_SESSION_RESET);
        response_status(STATUS_OK, PLATFORM_FS_OK);
        break;
    default:
        response_status(STATUS_UNKNOWN_COMMAND, PLATFORM_FS_OK);
        break;
    }
}
