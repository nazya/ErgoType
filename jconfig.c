 /*
 * ErgoType - Keyboard Solutions
 *
 * © 2024 Nazarii Tupitsa (see also: LICENSE-ErgoType).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "ff.h" // FatFS header
#include "core_json.h"
#include "flash.h"
#include "jconfig.h"
#include "keys.h"

#define GPIO_PULL_UP(pin)       \
    do {                        \
        gpio_init(pin);         \
        gpio_pull_up(pin);      \
    } while (0)

#define GPIO_PULL_DOWN(pin)     \
    do {                        \
        gpio_init(pin);         \
        gpio_pull_down(pin);    \
    } while (0)

void dbg3config(const config_t *config)
{
    #define FIELD(name, type, default_value) dbg3("config.%s=%d", #name, (int)config->name);
    CONFIG_FIELDS
    #undef FIELD

    dbg3("config.matrix.nr_rows=%u", config->matrix.nr_rows);
    dbg3("config.matrix.nr_cols=%u", config->matrix.nr_cols);

    for (uint8_t i = 0; i < config->matrix.nr_rows; ++i)
        dbg3("config.matrix.gpio_rows[%u]=%u", i, config->matrix.gpio_rows[i]);

    for (uint8_t i = 0; i < config->matrix.nr_cols; ++i)
        dbg3("config.matrix.gpio_cols[%u]=%u", i, config->matrix.gpio_cols[i]);

    for (uint8_t row = 0; row < config->matrix.nr_rows; ++row)
        for (uint8_t col = 0; col < config->matrix.nr_cols; ++col)
            dbg3("config.matrix.keymap[%u][%u]=%u", row, col, config->matrix.keymap[row][col]);

    dbg3("config.nr_encoders=%u", config->nr_encoders);
    for (uint8_t i = 0; i < config->nr_encoders; ++i) {
        const encoder_t *enc = &config->encoders[i];
        #define FIELD(name, type, default_value) dbg3("config.encoders[%u]." #name "=%u", i, (unsigned)enc->name);
        ENCODER_FIELDS
        #undef FIELD
    }

    dbg3("config.spi0={sck=%d,mosi=%d,miso=%d}", (int)config->spi[0].sck, (int)config->spi[0].mosi, (int)config->spi[0].miso);
    dbg3("config.spi1={sck=%d,mosi=%d,miso=%d}", (int)config->spi[1].sck, (int)config->spi[1].mosi, (int)config->spi[1].miso);

    dbg3("config.nr_pmw3360=%u", config->nr_pmw3360);
    for (uint8_t i = 0; i < config->nr_pmw3360; ++i) {
        const pmw3360_cfg_t *d = &config->pmw3360[i];
        dbg3("config.pmw3360[%u]={id=%u,role=%u,bus=%d,cs=%d,irq=%d,baud=%lu,mode=%u,cpi=%u}",
             i, d->id, d->role, (int)d->bus, (int)d->cs, (int)d->irq, (unsigned long)d->baud, d->mode, d->cpi);
    }
}


// Function to load cfguration from JSON file
static int load(char **buffer, const char *filename) {
    FATFS filesystem;
    FIL file;
    FRESULT res;
    UINT bytesRead;
    size_t file_size;

    // Mount the filesystem
    res = f_mount(&filesystem, "/", 1);
    if (res != FR_OK) {
        flash_fat_initialize();
        res = f_mount(&filesystem, "/", 1);
        if (res != FR_OK) {
            err("f_mount failed (FRESULT: %d)", res);
            return -1;
        }
    }

    // Open the configuration file
    res = f_open(&file, filename, FA_READ);
    if (res != FR_OK) {
        err("Failed to open %s, using default configuration (FRESULT: %d)", filename, res);
        f_unmount("/");
        return -1;
    }

    // Get the file size
    FILINFO fno;
    res = f_stat(filename, &fno);
    if (res != FR_OK) {
        err("Failed to get file size for %s (FRESULT: %d)", filename, res);
        f_close(&file);
        f_unmount("/");
        return -1;
    }

    file_size = fno.fsize;
    if (file_size == 0) {
        err("Invalid file size for %s: %lu bytes", filename, file_size);
        f_close(&file);
        f_unmount("/");
        return -1;
    }

    // Allocate memory for the file content
    *buffer = (char *)pvPortMalloc(file_size + 1);  // +1 for null-terminator
    if (!*buffer) {
        err("Memory allocation failed for config buffer");
        f_close(&file);
        f_unmount("/");
        return -1;
    }

    // Read the file content into the buffer
    res = f_read(&file, *buffer, file_size, &bytesRead);
    if (res != FR_OK || bytesRead != file_size) {
        err("Failed to read %s (FRESULT: %d)", filename, res);
        vPortFree(*buffer);  // Free allocated memory
        *buffer = NULL;
        f_close(&file);
        f_unmount("/");
        return -1;
    }
    (*buffer)[file_size] = '\0';  // Null-terminate the string

    f_close(&file);  // Close the file
    f_unmount("/");  // Unmount the filesystem

    return (int)file_size;  // Return the size of the file
}


static void init_default_cfg(config_t *config) {
    #define FIELD(name, type, default_value) config->name = default_value;
    CONFIG_FIELDS
    #undef FIELD
    config->nr_encoders = 0;
    memset(config->encoders, 0, sizeof(config->encoders));

    config->spi[0].sck = -1;
    config->spi[0].mosi = -1;
    config->spi[0].miso = -1;
    config->spi[1].sck = -1;
    config->spi[1].mosi = -1;
    config->spi[1].miso = -1;

    config->nr_pmw3360 = 0;
    memset(config->pmw3360, 0, sizeof(config->pmw3360));
}

// Set a configuration value based on the field name
static int set_cfg_value(config_t *config, const char *field_name, int8_t value) {
    #define FIELD(name, type, default_value) \
        if (strcmp(field_name, #name) == 0) { \
            config->name = value; \
            return 0; \
        }
    CONFIG_FIELDS
    #undef FIELD

    err("Unknown field: %s", field_name);
    return -1;  // Field not found
}

static bool json_parse_long(const char *json, size_t json_len, const char *query, long *out)
{
    JSONStatus_t result;
    const char *value = NULL;
    size_t value_length = 0;
    JSONTypes_t value_type = JSONInvalid;

    result = JSON_SearchConst(json, json_len, query, strlen(query), &value, &value_length, &value_type);
    if (result != JSONSuccess)
        return false;
    if (value_type != JSONNumber && value_type != JSONString)
        return false;

    char save = value[value_length];
    ((char *)value)[value_length] = '\0';
    char *end = NULL;
    long v = strtol(value, &end, 0);
    ((char *)value)[value_length] = save;

    if (!end || end == value)
        return false;

    *out = v;
    return true;
}

static bool json_parse_string(const char *json, size_t json_len, const char *query, const char **out, size_t *out_len)
{
    JSONStatus_t result;
    const char *value = NULL;
    size_t value_length = 0;
    JSONTypes_t value_type = JSONInvalid;

    result = JSON_SearchConst(json, json_len, query, strlen(query), &value, &value_length, &value_type);
    if (result != JSONSuccess || value_type != JSONString)
        return false;

    *out = value;
    *out_len = value_length;
    return true;
}

static int8_t json_parse_gpio_pin(const char *json, size_t json_len, const char *query, int8_t default_value)
{
    long v = 0;
    if (!json_parse_long(json, json_len, query, &v))
        return default_value;
    if (!IS_GPIO_PIN(v)) {
        err("%s: invalid GPIO %ld", query, v);
        return default_value;
    }
    return (int8_t)v;
}

static void parse_spi_buses(const char *json, size_t json_len, config_t *config)
{
    config->spi[0].sck = json_parse_gpio_pin(json, json_len, "spi0.sck", config->spi[0].sck);
    config->spi[0].mosi = json_parse_gpio_pin(json, json_len, "spi0.mosi", config->spi[0].mosi);
    config->spi[0].miso = json_parse_gpio_pin(json, json_len, "spi0.miso", config->spi[0].miso);

    config->spi[1].sck = json_parse_gpio_pin(json, json_len, "spi1.sck", config->spi[1].sck);
    config->spi[1].mosi = json_parse_gpio_pin(json, json_len, "spi1.mosi", config->spi[1].mosi);
    config->spi[1].miso = json_parse_gpio_pin(json, json_len, "spi1.miso", config->spi[1].miso);
}

static bool spi_bus_pins_valid(const config_t *config, int8_t bus)
{
    switch (bus) {
    case 0:
        return IS_GPIO_PIN(config->spi[0].sck) && IS_GPIO_PIN(config->spi[0].mosi) && IS_GPIO_PIN(config->spi[0].miso);
    case 1:
        return IS_GPIO_PIN(config->spi[1].sck) && IS_GPIO_PIN(config->spi[1].mosi) && IS_GPIO_PIN(config->spi[1].miso);
    default:
        return false;
    }
}

static int8_t parse_spi_bus_name(const char *s, size_t len, int8_t default_bus)
{
    if (len == 4 && memcmp(s, "spi0", 4) == 0)
        return 0;
    if (len == 4 && memcmp(s, "spi1", 4) == 0)
        return 1;
    return default_bus;
}

static uint8_t parse_pmw3360_role_name(const char *s, size_t len, uint8_t default_role)
{
    if (len == 8 && memcmp(s, "mousemove", 8) == 0)
        return PMW3360_ROLE_MOUSEMOVE;
    if (len == 5 && memcmp(s, "mouse", 5) == 0)
        return PMW3360_ROLE_MOUSEMOVE;
    if (len == 6 && memcmp(s, "scroll", 6) == 0)
        return PMW3360_ROLE_SCROLL;
    return default_role;
}

static int parse_pmw3360_drivers(const char *json, size_t json_len, config_t *config)
{
    JSONStatus_t result;
    const char *start = NULL;
    size_t length = 0;
    JSONTypes_t type = JSONInvalid;

    config->nr_pmw3360 = 0;

    result = JSON_SearchConst(json, json_len, "drivers.pmw3360", strlen("drivers.pmw3360"), &start, &length, &type);
    if (result != JSONSuccess) {
        return 0;
    }

    size_t it_start = 0, it_next = 0;
    JSONPair_t pair = {0};
    uint8_t idx = 0;

    while (idx < MAX_PMW3360) {
        if (type == JSONArray) {
            if (JSON_Iterate(start, length, &it_start, &it_next, &pair) != JSONSuccess)
                break;
            if (pair.jsonType != JSONObject) {
                err("drivers.pmw3360[%u]: expected object", idx);
                continue;
            }
        } else if (type == JSONObject) {
            if (idx > 0)
                break;
            pair.value = start;
            pair.valueLength = length;
            pair.jsonType = JSONObject;
        } else {
            err("drivers.pmw3360: expected array/object");
            config->nr_pmw3360 = 0;
            return -1;
        }

        pmw3360_cfg_t dev = {0};
        dev.id = idx;
        dev.role = PMW3360_ROLE_MOUSEMOVE;
        dev.bus = 0;
        dev.cs = -1;
        dev.irq = -1;
        dev.baud = 500000;
        dev.mode = 3;
        dev.cpi = 800;

        long v = 0;
        if (json_parse_long(pair.value, pair.valueLength, "id", &v))
            dev.id = (uint8_t)v;

        const char *bus_s = NULL;
        size_t bus_len = 0;
        if (json_parse_string(pair.value, pair.valueLength, "bus", &bus_s, &bus_len))
            dev.bus = parse_spi_bus_name(bus_s, bus_len, dev.bus);

        const char *role_s = NULL;
        size_t role_len = 0;
        if (json_parse_string(pair.value, pair.valueLength, "role", &role_s, &role_len)) {
            dev.role = parse_pmw3360_role_name(role_s, role_len, dev.role);
        } else if (json_parse_long(pair.value, pair.valueLength, "role", &v) && (v == 0 || v == 1)) {
            dev.role = (uint8_t)v;
        }

        dev.cs = json_parse_gpio_pin(pair.value, pair.valueLength, "cs", dev.cs);
        dev.irq = json_parse_gpio_pin(pair.value, pair.valueLength, "irq", dev.irq);

        if (json_parse_long(pair.value, pair.valueLength, "baud", &v) && v > 0)
            dev.baud = (uint32_t)v;
        if (json_parse_long(pair.value, pair.valueLength, "mode", &v) && v >= 0 && v <= 3)
            dev.mode = (uint8_t)v;
        if (json_parse_long(pair.value, pair.valueLength, "cpi", &v) && v > 0)
            dev.cpi = (uint16_t)v;

        if (!IS_GPIO_PIN(dev.cs) || !IS_GPIO_PIN(dev.irq)) {
            err("drivers.pmw3360[%u]: need valid cs+irq pins", idx);
            idx++;
            continue;
        }
        if (!spi_bus_pins_valid(config, dev.bus)) {
            err("drivers.pmw3360[%u]: %s pins missing/invalid", idx, dev.bus == 1 ? "spi1" : "spi0");
            idx++;
            continue;
        }

        config->pmw3360[config->nr_pmw3360++] = dev;
        idx++;
    }

    return 0;
}

static int parse_int_array(const char *json, size_t json_len, const char *key, uint8_t *array, size_t max_size) {
    JSONStatus_t result;
    const char *array_start;
    size_t array_length;
    size_t element_count = 0;

    // Search for the JSON array with the given key
    result = JSON_SearchConst(json, json_len, key, strlen(key), &array_start, &array_length, NULL);
    if (result != JSONSuccess) {
        err("Failed to find key: %s", key);
        return -1;  // Indicate failure
    }

    // Variables for iteration
    size_t start = 0, next = 0;
    JSONPair_t pair = {0};

    // Iterate through the elements in the array
    while (element_count < max_size) {
        result = JSON_Iterate(array_start, array_length, &start, &next, &pair);
        if (result != JSONSuccess) {
            break;  // No more elements to parse
        }

        // Only process valid numeric elements
        if (pair.jsonType == JSONNumber) {
            // Null-terminate the value for conversion to integer
            char save = pair.value[pair.valueLength];
            ((char *)pair.value)[pair.valueLength] = '\0';

            // Convert the value to an integer and store it in the array
            array[element_count++] = (uint8_t)atoi(pair.value);

            // Restore the original character
            ((char *)pair.value)[pair.valueLength] = save;
        } else {
            err("Non-integer value found, skipping.");
        }
    }

    return (int)element_count;  // Return the number of elements parsed
}

static uint8_t lookup_keycode(const char *name)
{
    size_t i;
    for (i = 0; i < 256; i++) {
        const struct keycode_table_ent *ent = &keycode_table[i];
        if (ent->name && !strcmp(ent->name, name)) {
            return i;
        }
    }
    return KEYD_NOOP;
}

static int parse_keymap(const char *json, size_t json_len, matrix_t *matrix) {
    JSONStatus_t result;
    const char *array_start;
    size_t array_length;

    // Search for the "keymap" array in the JSON
    result = JSON_SearchConst(json, json_len, "keymap", strlen("keymap"), &array_start, &array_length, NULL);
    if (result != JSONSuccess) {
        err("Failed to find 'keymap' in JSON");
        return -1;
    }

    // Variables for iterating through the JSON array
    size_t start = 0, next = 0;
    JSONPair_t pair = {0};

    // Iterate over the rows of the 2D keymap array
    uint8_t row = 0;
    while (row < matrix->nr_rows && JSON_Iterate(array_start, array_length, &start, &next, &pair) == JSONSuccess) {
        if (pair.jsonType != JSONArray) {
            err("Expected a JSON array for row %zu", row);
            continue;
        }

        // Parse each element in the current row
        const char *row_array_start = pair.value;
        size_t row_array_length = pair.valueLength;
        size_t row_start = 0, row_next = 0;
        uint8_t col = 0;

        while (col < matrix->nr_cols && JSON_Iterate(row_array_start, row_array_length, &row_start, &row_next, &pair) == JSONSuccess) {
            uint8_t keycode = 0;

            if (pair.jsonType == JSONString) {
                // Convert key name to keycode using the lookup function
                char save = pair.value[pair.valueLength];
                ((char *)pair.value)[pair.valueLength] = '\0';  // Null-terminate the string
                keycode = lookup_keycode(pair.value);
                ((char *)pair.value)[pair.valueLength] = save;  // Restore original character
            } else if (pair.jsonType == JSONNumber) {
                // Convert JSON number to keycode directly
                char save = pair.value[pair.valueLength];
                ((char *)pair.value)[pair.valueLength] = '\0';  // Null-terminate for atoi()
                keycode = (uint8_t)atoi(pair.value);  // Convert string to integer
                ((char *)pair.value)[pair.valueLength] = save;  // Restore original character
            } else {
                err("Unexpected type for keymap element at [%zu][%zu].", row, col);
                continue;
            }

            // Store the keycode in the 2D keymap array
            matrix->keymap[row][col] = keycode;
            col++;
        }

        row++;
    }

    return 0;  // Success
}

static int lookup_pin_index(config_t *config, uint8_t pin)
{
    for (uint8_t col = 0; col < config->matrix.nr_cols; ++col) {
        if (config->matrix.gpio_cols[col] == pin) {
            return col;
        }
    }
    for (uint8_t row = 0; row < config->matrix.nr_rows; ++row) {
        if (config->matrix.gpio_rows[row] == pin) {
            return row;
        }
    }
    return -1;
}

static int parse_encoders(const char *json, size_t json_len, config_t *config)
{
    JSONStatus_t result;
    const char *array_start = NULL;
    size_t array_length = 0;

    result = JSON_SearchConst(json, json_len, "encoders", strlen("encoders"), &array_start, &array_length, NULL);
    if (result != JSONSuccess) {
        config->nr_encoders = 0;
        return 0;
    }

    size_t start = 0, next = 0;
    JSONPair_t pair = {0};
    uint8_t idx = 0;

    while (idx < MAX_ENCODERS && JSON_Iterate(array_start, array_length, &start, &next, &pair) == JSONSuccess) {
        if (pair.jsonType != JSONObject) {
            err("encoders[%u]: expected object", idx);
            continue;
        }

        encoder_t enc = {0};

        // Defaults.
        #define FIELD(name, type, default_value) enc.name = (type)(default_value);
        ENCODER_FIELDS
        #undef FIELD

        // Iterate over fields using the X-Macro (same style as CONFIG_FIELDS).
        #define FIELD(name, type, default_value)                               \
            {                                                                  \
            const char *value = NULL;                                      \
            size_t value_length = 0;                                       \
            JSONTypes_t value_type = JSONInvalid;                          \
            result = JSON_SearchConst(pair.value, pair.valueLength,        \
                                        #name, strlen(#name),                \
                                        &value, &value_length, &value_type); \
                if (result == JSONSuccess) {                                   \
                    char save = value[value_length];                           \
                    ((char *)value)[value_length] = '\0';                      \
                    if (strcmp(#name, "div") == 0) {                           \
                        enc.div = (type)atoi(value);                           \
                    } else {                                                   \
                        enc.name = lookup_pin_index(config, (type)atoi(value));\
                    }                                                          \
                    ((char *)value)[value_length] = save;                      \
                }                                                              \
            }
        ENCODER_FIELDS
        #undef FIELD
	
        if (enc.a < 0 || enc.b < 0 || enc.c < 0 || enc.div <= 0) {
            continue;
        }
        config->encoders[idx++] = enc;
    }

    config->nr_encoders = idx;
    return 0;
}

static void pull_pins(const char *json_data) {
    JSONStatus_t result;
    const char *array_start;
    size_t array_length;
    bool found = true;

    // Search for the "pull_up_pins" array in the JSON data
    result = JSON_SearchConst(json_data, strlen(json_data),
                              "pull_up_pins", strlen("pull_up_pins"),
                              &array_start, &array_length, NULL);

    if (result != JSONSuccess) {
        // dbg("Failed to find 'pull_up_pins' array in JSON");
        found = false;
    }

    // Variables for iteration
    size_t start = 0, next = 0;
    JSONPair_t pair = {0};

    // Iterate over the elements in the "pull_up_pins" array
    while (found) {
        result = JSON_Iterate(array_start, array_length, &start, &next, &pair);
        if (result != JSONSuccess) {
            break;  // No more elements to process
        }

        // Process only numeric elements
        if (pair.jsonType == JSONNumber) {
            // Null-terminate the value for conversion to integer
            char save = pair.value[pair.valueLength];
            ((char *)pair.value)[pair.valueLength] = '\0';
            int pin = atoi(pair.value);
            ((char *)pair.value)[pair.valueLength] = save;
            if (IS_GPIO_PIN(pin)) {
                GPIO_PULL_UP(pin);
                // dbg3("GPIO %d: Pulled up", pin);
            } else {
                err("Not a valid pin in 'pull_up_pins'");
            }
        } else {
            err("Skipping non-integer value in 'pull_up_pins'");
        }
    }

    result = JSON_SearchConst(json_data, strlen(json_data),
                              "pull_down_pins", strlen("pull_down_pins"),
                              &array_start, &array_length, NULL);

    if (result != JSONSuccess) {
        // dbg("Failed to find 'pull_down_pins' array in JSON");
        return;
    }

    start = 0, next = 0;
    while (1) {
        result = JSON_Iterate(array_start, array_length, &start, &next, &pair);
        if (result != JSONSuccess) {
            break;  // No more elements to process
        }

        // Process only numeric elements
        if (pair.jsonType == JSONNumber) {
            // Null-terminate the value for conversion to integer
            char save = pair.value[pair.valueLength];
            ((char *)pair.value)[pair.valueLength] = '\0';
            int pin = atoi(pair.value);
            ((char *)pair.value)[pair.valueLength] = save;
            if (IS_GPIO_PIN(pin)) {
                GPIO_PULL_DOWN(pin);
                // dbg3("GPIO %d: Pulled down", pin);
            } else {
                err("Not a valid pin in 'pull_down_pins'");
            }
        } else {
            err("Skipping non-integer value in 'pull_down_pins'");
        }
    }
}

int parse(config_t *config, const char *filename) {
    init_default_cfg(config);
    // errstr[0] = '\0';

    char *json = NULL;
    int json_len = load(&json, filename);

    if (json_len < 0) {
        err("No 'config.json'");
        vPortFree(json);
        return -1;
    }

    JSONStatus_t result = JSON_Validate(json, json_len);
    if (result != JSONSuccess) {
        err("Invalid JSON format.");
        vPortFree(json);  // Free the buffer before returning
        return -1;
    }

    pull_pins(json);
    
    // Iterate over fields using the X-Macro
    #define FIELD(name, type, default_value)                       \
        {                                                          \
            char *value;                                           \
            size_t value_length;                                   \
            result = JSON_Search(json, json_len, #name,            \
                strlen(#name), &value, &value_length);             \
            if (result == JSONSuccess) {                           \
                char save = value[value_length];                   \
                value[value_length] = '\0';                        \
                set_cfg_value(config, #name, (int8_t)atoi(value)); \
                value[value_length] = save;                        \
            }                                                      \
        }
    CONFIG_FIELDS
    #undef FIELD

    log_level = config->log_level;

    parse_spi_buses(json, json_len, config);

    // Parse the integer array from the JSON
    int nr;
    nr = parse_int_array(json, json_len, "gpio_rows", config->matrix.gpio_rows, MAX_GPIOS);
    if (nr < 0) {
        err("Failed to parse gpio_rows.");
        vPortFree(json);
        return -1;
    }
    config->matrix.nr_rows = nr;

    // Parse the integer array from the JSON
    nr = parse_int_array(json, json_len, "gpio_cols", config->matrix.gpio_cols, MAX_GPIOS);
    if (nr < 0) {
        err("Failed to parse gpio_cols.");
        vPortFree(json);
        return -1;
    }
    config->matrix.nr_cols = nr;

    if (parse_keymap(json, json_len, &config->matrix)) {
        err("Failed to parse keymap.");
        vPortFree(json);
        return -1;
    }

    if (parse_encoders(json, json_len, config)) {
        err("Failed to parse encoders.");
        vPortFree(json);
        return -1;
    }

    if (parse_pmw3360_drivers(json, json_len, config)) {
        err("Failed to parse drivers.pmw3360.");
        vPortFree(json);
        return -1;
    }

    // Free the JSON buffer after parsing
    vPortFree(json);
    return 0;
}
