#include "platform/ble_config.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "platform/hid_reports.h"
#include "platform/storage.h"

LOG_MODULE_REGISTER(ergotype_ble_config, LOG_LEVEL_INF);

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
#define FILE_READ_DATA_SIZE (CONFIG_MESSAGE_SIZE - FILE_READ_DATA_OFFSET)
#define FILE_LIST_DATA_OFFSET 13
#define FILE_LIST_DATA_SIZE (CONFIG_MESSAGE_SIZE - FILE_LIST_DATA_OFFSET)
#define FILE_WRITE_DATA_OFFSET 7
#define FILE_WRITE_DATA_SIZE (CONFIG_MESSAGE_SIZE - FILE_WRITE_DATA_OFFSET)
#define FILE_WRITE_BUFFER_SIZE 4096
#define FILE_LIST_BUFFER_SIZE 4096
#define PATH_OFFSET 2
#define WRITE_PATH_OFFSET 6
#define ATTR_CONFIG_TX 4

#define BT_UUID_ERGOTYPE_CONFIG_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x4552474f, 0x5459, 0x5045, 0x4346, 0x470000000001)
#define BT_UUID_ERGOTYPE_CONFIG_RX_VAL \
    BT_UUID_128_ENCODE(0x4552474f, 0x5459, 0x5045, 0x4346, 0x470000000002)
#define BT_UUID_ERGOTYPE_CONFIG_TX_VAL \
    BT_UUID_128_ENCODE(0x4552474f, 0x5459, 0x5045, 0x4346, 0x470000000003)

extern const struct bt_gatt_service_static ergotype_config_svc;

static uint8_t response[CONFIG_MESSAGE_SIZE];
static char path[PATH_MAX_LEN + 1];
static uint8_t *read_buffer;
static uint32_t read_size;
static uint32_t read_offset;
static uint8_t *list_buffer;
static uint32_t list_size;
static uint8_t *write_buffer;
static uint32_t write_size;
static uint32_t write_capacity;
static uint32_t write_flushed;
static uint32_t write_buffer_size;
static bool write_active;
static bool notify_enabled;

static uint32_t get_u32(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] |
        ((uint32_t)buffer[1] << 8) |
        ((uint32_t)buffer[2] << 16) |
        ((uint32_t)buffer[3] << 24);
}

static void put_u32(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 24);
}

static void response_begin(uint8_t command)
{
    memset(response, 0, sizeof(response));
    response[0] = CONFIG_PROTOCOL_VERSION;
    response[1] = command;
}

static void response_status(uint8_t status, int storage_result)
{
    response[2] = status;
    response[3] = (uint8_t)-storage_result;
}

static bool copy_path_at(char *dst, const uint8_t *buffer, uint16_t bufsize, uint16_t offset)
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
    k_free(read_buffer);
    read_buffer = NULL;
    read_size = 0;
    read_offset = 0;
}

static void list_reset(void)
{
    k_free(list_buffer);
    list_buffer = NULL;
    list_size = 0;
}

static void write_reset(void)
{
    k_free(write_buffer);
    write_buffer = NULL;
    write_size = 0;
    write_capacity = 0;
    write_flushed = 0;
    write_buffer_size = 0;
    write_active = false;
}

static void write_abort(void)
{
    (void)platform_storage_upload_abort();
    write_reset();
}

static void get_status(void)
{
    response_begin(CMD_GET_STATUS);
    response_status(STATUS_OK, 0);
    response[3] = CONFIG_STATUS_MODE_HID;
    response[4] = CONFIG_STATUS_PROFILE_NKRO;

    size_t total_bytes = 0;
    size_t free_bytes = 0;
    int rc = platform_storage_info(&total_bytes, &free_bytes);
    put_u32(&response[5], (uint32_t)total_bytes);
    put_u32(&response[9], (uint32_t)free_bytes);
    put_u32(&response[13], (uint32_t)(total_bytes - free_bytes));
    response[17] = (uint8_t)-rc;
}

static void file_open_read(const uint8_t *buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_OPEN_READ);
    read_reset();

    if (!copy_path_at(path, buffer, bufsize, PATH_OFFSET)) {
        response_status(STATUS_BAD_REQUEST, 0);
        return;
    }

    size_t file_size = 0;
    int rc = platform_storage_size(path, &file_size);
    if (rc) {
        response_status(STATUS_FS_ERROR, rc);
        return;
    }

    read_buffer = k_malloc(file_size ? file_size : 1);
    if (!read_buffer) {
        response_status(STATUS_FS_ERROR, -ENOMEM);
        return;
    }

    size_t bytes_read = 0;
    rc = platform_storage_read(path, read_buffer, file_size, &bytes_read);
    if (rc || bytes_read != file_size) {
        read_reset();
        response_status(STATUS_FS_ERROR, rc ? rc : -EIO);
        return;
    }

    read_size = (uint32_t)file_size;
    put_u32(&response[4], read_size);
    response_status(STATUS_OK, 0);
}

static void file_read_chunk(const uint8_t *buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_READ_CHUNK);

    if (bufsize < 6 || !read_buffer) {
        response_status(STATUS_BAD_REQUEST, 0);
        return;
    }

    uint32_t offset = get_u32(&buffer[2]);
    if (offset > read_size || offset != read_offset) {
        response_status(STATUS_BAD_REQUEST, 0);
        return;
    }

    put_u32(&response[4], read_size);
    put_u32(&response[8], offset);

    uint32_t bytes_left = read_size - offset;
    uint8_t bytes_to_read = bytes_left > FILE_READ_DATA_SIZE ? FILE_READ_DATA_SIZE : (uint8_t)bytes_left;
    memcpy(&response[FILE_READ_DATA_OFFSET], &read_buffer[offset], bytes_to_read);
    response[12] = bytes_to_read;
    response_status(STATUS_OK, 0);
    read_offset += bytes_to_read;

    if (offset + bytes_to_read == read_size)
        read_reset();
}

static void file_read_end(void)
{
    response_begin(CMD_FILE_READ_END);
    read_reset();
    response_status(STATUS_OK, 0);
}

static void file_write_begin(const uint8_t *buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_WRITE_BEGIN);

    if (bufsize < 7) {
        response_status(STATUS_BAD_REQUEST, 0);
        return;
    }

    write_abort();
    if (!copy_path_at(path, buffer, bufsize, WRITE_PATH_OFFSET)) {
        response_status(STATUS_BAD_REQUEST, 0);
        return;
    }

    uint32_t size = get_u32(&buffer[2]);
    write_buffer = k_malloc(FILE_WRITE_BUFFER_SIZE);
    if (!write_buffer) {
        response_status(STATUS_FS_ERROR, -ENOMEM);
        return;
    }

    int rc = platform_storage_upload_begin();
    if (rc) {
        write_reset();
        response_status(STATUS_FS_ERROR, rc);
        return;
    }

    write_capacity = size;
    write_active = true;
    response_status(STATUS_OK, 0);
}

static int file_write_flush(void)
{
    if (!write_buffer_size)
        return 0;

    int rc = platform_storage_upload_write(write_buffer, write_buffer_size, write_flushed);
    if (!rc) {
        write_flushed += write_buffer_size;
        write_buffer_size = 0;
    }
    return rc;
}

static void file_write_chunk(const uint8_t *buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_WRITE_CHUNK);

    if (!write_active || bufsize < FILE_WRITE_DATA_OFFSET) {
        response_status(STATUS_BAD_REQUEST, 0);
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
        response_status(STATUS_BAD_REQUEST, 0);
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
            int rc = file_write_flush();
            if (rc) {
                write_abort();
                response_status(STATUS_FS_ERROR, rc);
                return;
            }
        }
    }

    write_size = end;
    put_u32(&response[4], offset);
    response[8] = bytes_to_write;
    response_status(STATUS_OK, 0);
}

static void file_write_commit(void)
{
    response_begin(CMD_FILE_WRITE_COMMIT);

    if (!write_active || write_size != write_capacity) {
        response_status(STATUS_BAD_REQUEST, 0);
        return;
    }

    int rc = file_write_flush();
    if (!rc)
        rc = platform_storage_upload_commit(path);
    if (rc)
        (void)platform_storage_upload_abort();
    write_reset();
    response_status(rc ? STATUS_FS_ERROR : STATUS_OK, rc);
}

static void file_write_abort(void)
{
    response_begin(CMD_FILE_WRITE_ABORT);
    if (!write_active) {
        response_status(STATUS_OK, 0);
        return;
    }
    write_abort();
    response_status(STATUS_OK, 0);
}

static void file_list_open(void)
{
    response_begin(CMD_FILE_LIST_OPEN);
    list_reset();

    list_buffer = k_malloc(FILE_LIST_BUFFER_SIZE);
    if (!list_buffer) {
        response_status(STATUS_FS_ERROR, -ENOMEM);
        return;
    }

    size_t bytes_written = 0;
    int rc = platform_storage_list((char *)list_buffer, FILE_LIST_BUFFER_SIZE, &bytes_written);
    if (rc) {
        list_reset();
        response_status(STATUS_FS_ERROR, rc);
        return;
    }

    list_size = (uint32_t)bytes_written;
    put_u32(&response[4], list_size);
    response_status(STATUS_OK, 0);
}

static void file_list_read_chunk(const uint8_t *buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_LIST_READ_CHUNK);

    if (bufsize < 6 || !list_buffer) {
        response_status(STATUS_BAD_REQUEST, 0);
        return;
    }

    uint32_t offset = get_u32(&buffer[2]);
    if (offset > list_size) {
        response_status(STATUS_BAD_REQUEST, 0);
        return;
    }

    put_u32(&response[4], list_size);
    put_u32(&response[8], offset);

    uint32_t bytes_left = list_size - offset;
    uint8_t bytes_to_read = bytes_left > FILE_LIST_DATA_SIZE ? FILE_LIST_DATA_SIZE : (uint8_t)bytes_left;
    memcpy(&response[FILE_LIST_DATA_OFFSET], &list_buffer[offset], bytes_to_read);
    response[12] = bytes_to_read;
    response_status(STATUS_OK, 0);

    if (offset + bytes_to_read == list_size)
        list_reset();
}

static void file_delete(const uint8_t *buffer, uint16_t bufsize)
{
    response_begin(CMD_FILE_DELETE);

    if (!copy_path_at(path, buffer, bufsize, PATH_OFFSET)) {
        response_status(STATUS_BAD_REQUEST, 0);
        return;
    }

    int rc = platform_storage_delete(path);
    response_status(rc ? STATUS_FS_ERROR : STATUS_OK, rc);
}

static void process_request(const uint8_t *buffer, uint16_t bufsize)
{
    if (bufsize < 2 || buffer[0] != CONFIG_PROTOCOL_VERSION) {
        response_begin(bufsize >= 2 ? buffer[1] : 0);
        response_status(STATUS_BAD_REQUEST, 0);
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
        read_reset();
        list_reset();
        write_abort();
        response_begin(CMD_SESSION_RESET);
        response_status(STATUS_OK, 0);
        break;
    default:
        response_begin(buffer[1]);
        response_status(STATUS_UNKNOWN_COMMAND, 0);
        break;
    }
}

static ssize_t write_request(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len, uint16_t offset,
                             uint8_t flags)
{
    (void)attr;
    (void)flags;

    if (offset != 0 || len > CONFIG_MESSAGE_SIZE)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);

    uint8_t request[CONFIG_MESSAGE_SIZE] = {0};
    memcpy(request, buf, len);
    process_request(request, len);

    if (notify_enabled) {
        int rc = bt_gatt_notify(conn, &ergotype_config_svc.attrs[ATTR_CONFIG_TX],
                                response, sizeof(response));
        if (rc) {
            LOG_WRN("63-byte config notification failed: %d", rc);
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }
    }

    return len;
}

static ssize_t read_response(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, response, sizeof(response));
}

static void tx_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    (void)attr;
    notify_enabled = value == BT_GATT_CCC_NOTIFY;
}

BT_GATT_SERVICE_DEFINE(ergotype_config_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(BT_UUID_ERGOTYPE_CONFIG_SERVICE_VAL)),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(BT_UUID_ERGOTYPE_CONFIG_RX_VAL),
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE_ENCRYPT,
                           NULL, write_request, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(BT_UUID_ERGOTYPE_CONFIG_TX_VAL),
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT,
                           read_response, NULL, NULL),
    BT_GATT_CCC(tx_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

int platform_ble_config_init(void)
{
    read_reset();
    list_reset();
    write_abort();
    response_begin(CMD_GET_STATUS);
    response_status(STATUS_OK, 0);
    return 0;
}

void platform_ble_config_disconnected(void)
{
    notify_enabled = false;
    read_reset();
    list_reset();
    write_abort();
}
