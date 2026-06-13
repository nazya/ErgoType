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

extern const char *keyd_overlay_conf;

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


#define IS_PIN_FIELD(name_lit) \
    (sizeof(name_lit) > 4 && strcmp((name_lit) + sizeof(name_lit) - 5, "_pin") == 0)

static uint32_t used_gpio_mask;

static bool claim_gpio(int pin)
{
    if (!IS_GPIO_PIN(pin)) {
        err("invalid GPIO %d", pin);
        return false;
    }

    uint32_t bit = 1u << (uint32_t)pin;
    if (used_gpio_mask & bit) {
        err("duplicate GPIO %d", pin);
        return false;
    }

    used_gpio_mask |= bit;
    return true;
}

static bool spi_pin_valid(uint8_t spi_idx, const char *name, int pin)
{
    // RP2040: SPI signals are only available on specific GPIOs.
    // Validate only bus pins (sck/mosi/miso). Device CS is handled as a plain GPIO.
    if (spi_idx == 0) {
        if (strcmp(name, "sck") == 0)
            return pin == 2 || pin == 6 || pin == 18 || pin == 22;
        if (strcmp(name, "mosi") == 0)
            return pin == 3 || pin == 7 || pin == 19 || pin == 23;
        if (strcmp(name, "miso") == 0)
            return pin == 0 || pin == 4 || pin == 16 || pin == 20;
        return false;
    }

    if (spi_idx == 1) {
        if (strcmp(name, "sck") == 0)
            return pin == 10 || pin == 14 || pin == 26;
        if (strcmp(name, "mosi") == 0)
            return pin == 11 || pin == 15 || pin == 27;
        if (strcmp(name, "miso") == 0)
            return pin == 8 || pin == 12 || pin == 24 || pin == 28;
        return false;
    }

    return false;
}

static bool uart_pin_valid(uint8_t uart_idx, const char *name, int pin)
{
    // RP2040: UART signals are only available on specific GPIOs.
    if (uart_idx == 0) {
        if (strcmp(name, "tx") == 0)
            return pin == 0 || pin == 12 || pin == 16 || pin == 28;
        if (strcmp(name, "rx") == 0)
            return pin == 1 || pin == 13 || pin == 17 || pin == 29;
        return false;
    }

    if (uart_idx == 1) {
        if (strcmp(name, "tx") == 0)
            return pin == 4 || pin == 8 || pin == 20 || pin == 24;
        if (strcmp(name, "rx") == 0)
            return pin == 5 || pin == 9 || pin == 21 || pin == 25;
        return false;
    }

    return false;
}

static bool i2c_pin_valid(uint8_t i2c_idx, const char *name, int pin)
{
    // RP2040: I2C signals are only available on specific GPIOs.
    if (i2c_idx == 0) {
        if (strcmp(name, "sda") == 0)
            return pin == 0 || pin == 4 || pin == 8 || pin == 12 || pin == 16 || pin == 20 || pin == 24 || pin == 28;
        if (strcmp(name, "scl") == 0)
            return pin == 1 || pin == 5 || pin == 9 || pin == 13 || pin == 17 || pin == 21 || pin == 25 || pin == 29;
        return false;
    }

    if (i2c_idx == 1) {
        if (strcmp(name, "sda") == 0)
            return pin == 2 || pin == 6 || pin == 10 || pin == 14 || pin == 18 || pin == 22 || pin == 26;
        if (strcmp(name, "scl") == 0)
            return pin == 3 || pin == 7 || pin == 11 || pin == 15 || pin == 19 || pin == 23 || pin == 27;
        return false;
    }

    return false;
}

void dbg3config(const config_t *config)
{
    #define FIELD(name, type, default_value) dbg3("config.%s=%d", #name, (int)config->name);
    CONFIG_FIELDS
    #undef FIELD
    dbg3("config.os=%s", config->os ? config->os : "NULL");

    dbg3("config.nr_leds=%u", config->nr_leds);

    for (uint8_t i = 0; i < config->nr_leds; ++i)
        dbg3("config.led_pins[%u]=%d", (unsigned)i, (int)config->led_pins[i]);

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

    dbg3("config.spi_mask=0x%02x", config->spi_mask);
    dbg3("config.spi0={sck=%d,mosi=%d,miso=%d,baud=%lu}",
         (int)config->spi[0].sck, (int)config->spi[0].mosi, (int)config->spi[0].miso, (unsigned long)config->spi[0].baud);
    dbg3("config.spi1={sck=%d,mosi=%d,miso=%d,baud=%lu}",
         (int)config->spi[1].sck, (int)config->spi[1].mosi, (int)config->spi[1].miso, (unsigned long)config->spi[1].baud);

    dbg3("config.uart_mask=0x%02x", config->uart_mask);
    dbg3("config.uart0={rx=%d,tx=%d}", (int)config->uart[0].rx, (int)config->uart[0].tx);
    dbg3("config.uart1={rx=%d,tx=%d}", (int)config->uart[1].rx, (int)config->uart[1].tx);

    dbg3("config.i2c_mask=0x%02x", config->i2c_mask);
    dbg3("config.i2c0={sda=%d,scl=%d,baud=%lu}", (int)config->i2c[0].sda, (int)config->i2c[0].scl, (unsigned long)config->i2c[0].baud);
    dbg3("config.i2c1={sda=%d,scl=%d,baud=%lu}", (int)config->i2c[1].sda, (int)config->i2c[1].scl, (unsigned long)config->i2c[1].baud);

    dbg3("config.ssd1306={i2c_idx=%d,addr=%u,width=%u,height=%u}",
         (int)config->ssd1306.i2c_idx,
         (unsigned)config->ssd1306.addr,
         (unsigned)config->ssd1306.width,
         (unsigned)config->ssd1306.height);

    dbg3("config.nr_pmw3360=%u", config->nr_pmw3360);
    for (uint8_t i = 0; i < config->nr_pmw3360; ++i) {
        const pmw33xx_cfg_t *d = &config->pmw3360[i];
        dbg3("config.pmw3360[%u]={role=%u,spi_idx=%d,cs=%d,irq=%d,cpi=%u}",
             i, d->role, (int)d->spi_idx, (int)d->cs, (int)d->irq,
             d->cpi);
    }

    dbg3("config.nr_pmw3389=%u", config->nr_pmw3389);
    for (uint8_t i = 0; i < config->nr_pmw3389; ++i) {
        const pmw33xx_cfg_t *d = &config->pmw3389[i];
        dbg3("config.pmw3389[%u]={role=%u,spi_idx=%d,cs=%d,irq=%d,cpi=%u}",
             i, d->role, (int)d->spi_idx, (int)d->cs, (int)d->irq,
             d->cpi);
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


static uint8_t parse_pin_array(const char *json,
                               size_t json_len,
                               const char *key,
                               uint8_t *array,
                               size_t max_size)
{
    JSONStatus_t result;
    const char *array_start;
    size_t array_length;
    uint8_t element_count = 0;

    // Search for the JSON array with the given key
    result = JSON_SearchConst(json, json_len, key, strlen(key), &array_start, &array_length, NULL);
    if (result != JSONSuccess) {
        return 0;
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
            int v = atoi(pair.value);
            if (claim_gpio(v))
                array[element_count++] = (uint8_t)v;

            // Restore the original character
            ((char *)pair.value)[pair.valueLength] = save;
        } else {
            err("Non-integer value found, skipping.");
        }
    }

    return element_count;
}

static void parse_led_pins(const char *json, size_t json_len, config_t *config)
{
    config->nr_leds = parse_pin_array(json,
                                      json_len,
                                      "led_pins",
                                      (uint8_t *)config->led_pins,
                                      MAX_LED);
}

static uint8_t lookup_keycode(const char *name)
{
    for (uint8_t i = 0;; ++i) {
        const struct keycode_table_ent *ent = &keycode_table[i];
        if (ent->name && !strcmp(ent->name, name)) {
            return i;
        }
        if (i == 255)
            return KEYD_NOOP;
    }
}

static void parse_keymap(const char *json, size_t json_len, matrix_t *matrix) {
    JSONStatus_t result;
    const char *array_start;
    size_t array_length;

    // Search for the "keymap" array in the JSON
    result = JSON_SearchConst(json, json_len, "keymap", strlen("keymap"), &array_start, &array_length, NULL);
    if (result != JSONSuccess) {
        return;
    }

    // Variables for iterating through the JSON array
    size_t start = 0, next = 0;
    JSONPair_t pair = {0};

    // Iterate over the rows of the 2D keymap array
    uint8_t row = 0;
    while (row < matrix->nr_rows && JSON_Iterate(array_start, array_length, &start, &next, &pair) == JSONSuccess) {
        if (pair.jsonType != JSONArray) {
            err("Expected a JSON array for row %zu", row);
            row++;
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
                // coreJSON strips quotes for JSONString values.
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
                keycode = KEYD_NOOP;
            }

            // Store the keycode in the 2D keymap array
            matrix->keymap[row][col] = keycode;
            col++;
        }

        row++;
    }
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

static void parse_encoders(const char *json, size_t json_len, config_t *config)
{
    JSONStatus_t result;
    const char *array_start = NULL;
    size_t array_length = 0;

    result = JSON_SearchConst(json, json_len, "encoders", strlen("encoders"), &array_start, &array_length, NULL);
    if (result != JSONSuccess) {
        config->nr_encoders = 0;
        return;
    }

    size_t start = 0, next = 0;
    JSONPair_t pair = {0};
    config->nr_encoders = 0;

    for (uint8_t idx = 0; idx < MAX_ENCODERS && config->nr_encoders < MAX_ENCODERS; idx++) {
        if (JSON_Iterate(array_start, array_length, &start, &next, &pair) != JSONSuccess)
            break;

        if (pair.jsonType != JSONObject) {
            err("encoders[%u]: expected object", idx);
            continue;
        }

        encoder_t enc = {0};
        uint8_t found = 0;

        // Defaults.
        #define FIELD(name, type, default_value) enc.name = (type)(default_value);
        ENCODER_FIELDS
        #undef FIELD

        // Iterate over fields using the X-Macro (same style as CONFIG_FIELDS).
        #define FIELD(name, type, default_value)                                   \
            {                                                                      \
                const char *value = NULL;                                          \
                size_t value_length = 0;                                           \
                JSONTypes_t value_type = JSONInvalid;                              \
                result = JSON_SearchConst(pair.value, pair.valueLength,            \
                                          #name, strlen(#name),                    \
                                          &value, &value_length, &value_type);    \
                if (result == JSONSuccess && value_type == JSONNumber) {          \
                    char save = value[value_length];                               \
                    ((char *)value)[value_length] = '\0';                          \
                    if (strcmp(#name, "div") == 0)                                 \
                        enc.div = (type)atoi(value);                               \
                    else {                                                         \
                        enc.name = lookup_pin_index(config, (type)atoi(value));    \
                        found += (enc.name != -1);                                  \
                    }                                                              \
                    ((char *)value)[value_length] = save;                          \
                }                                                                  \
            }
        ENCODER_FIELDS
        #undef FIELD

        if (found == 3)
            config->encoders[config->nr_encoders++] = enc;
    }
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
            if (claim_gpio(pin)) {
                GPIO_PULL_UP(pin);
                // dbg3("GPIO %d: Pulled up", pin);
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
            if (claim_gpio(pin)) {
                GPIO_PULL_DOWN(pin);
                // dbg3("GPIO %d: Pulled down", pin);
            }
        } else {
            err("Skipping non-integer value in 'pull_down_pins'");
        }
    }
}

static void parse_spi_buses(const char *json, size_t json_len, config_t *config)
{
    JSONStatus_t result;
    const char *value = NULL;
    size_t value_length = 0;
    JSONTypes_t value_type = JSONInvalid;

    config->spi_mask = 0;

    for (uint8_t spi_idx = 0; spi_idx < MAX_SPI; ++spi_idx) {
        uint8_t found = 0;

        // Defaults.
        #define FIELD(name, type, default_value) config->spi[spi_idx].name = (type)(default_value);
        SPI_CFG_FIELDS
        #undef FIELD

        #define FIELD(name, type, default_value)                                          \
            {                                                                             \
                char key[16];                                                            \
                int n = snprintf(key, sizeof(key), "spi%u.%s", (unsigned)spi_idx, #name); \
                result = JSON_SearchConst(json, json_len,                                 \
                                          key, (size_t)n,                                \
                                          &value, &value_length, &value_type);           \
                if (result == JSONSuccess && value_type == JSONNumber) {                  \
                    char save = value[value_length];                                      \
                    ((char *)value)[value_length] = '\0';                                 \
                    int v = atoi(value);                                                  \
                    ((char *)value)[value_length] = save;                                 \
                    if (strcmp(#name, "baud") == 0) {                                     \
                        config->spi[spi_idx].baud = (type)v;                              \
                    } else {                                                              \
                        if (spi_pin_valid(spi_idx, #name, v)) {                           \
                            config->spi[spi_idx].name = (type)v;                          \
                            found += claim_gpio(config->spi[spi_idx].name);               \
                        } else {                                                          \
                            err("spi%u." #name ": invalid GPIO %d", (unsigned)spi_idx, v); \
                        }                                                                 \
                    }                                                                     \
                }                                                                         \
            }
        SPI_CFG_FIELDS
        #undef FIELD

        config->spi_mask |= (uint8_t)((found == 3) << spi_idx);
    }
}

static void parse_uart_buses(const char *json, size_t json_len, config_t *config)
{
    JSONStatus_t result;
    const char *value = NULL;
    size_t value_length = 0;
    JSONTypes_t value_type = JSONInvalid;

    config->uart_mask = 0;

    for (uint8_t uart_idx = 0; uart_idx < MAX_UART; ++uart_idx) {
        uint8_t found = 0;

        // Defaults.
        #define FIELD(name, type, default_value) config->uart[uart_idx].name = (type)(default_value);
        UART_CFG_FIELDS
        #undef FIELD

        #define FIELD(name, type, default_value)                                           \
            {                                                                              \
                char key[16];                                                              \
                int n = snprintf(key, sizeof(key), "uart%u.%s", (unsigned)uart_idx, #name);\
                result = JSON_SearchConst(json, json_len,                                  \
                                          key, (size_t)n,                                 \
                                          &value, &value_length, &value_type);            \
                if (result == JSONSuccess && value_type == JSONNumber) {                   \
                    char save = value[value_length];                                       \
                    ((char *)value)[value_length] = '\0';                                  \
                    int pin = atoi(value);                                                 \
                    ((char *)value)[value_length] = save;                                  \
                    if (uart_pin_valid(uart_idx, #name, pin)) {                             \
                        config->uart[uart_idx].name = (type)pin;                            \
                        found += claim_gpio(config->uart[uart_idx].name);                  \
                    } else {                                                               \
                        err("uart%u." #name ": invalid GPIO %d", (unsigned)uart_idx, pin); \
                    }                                                                      \
                }                                                                          \
            }
        UART_CFG_FIELDS
        #undef FIELD

        config->uart_mask |= (uint8_t)((found == 2) << uart_idx);
    }
}

static void parse_i2c_buses(const char *json, size_t json_len, config_t *config)
{
    JSONStatus_t result;
    const char *value = NULL;
    size_t value_length = 0;
    JSONTypes_t value_type = JSONInvalid;

    config->i2c_mask = 0;

    for (uint8_t i2c_idx = 0; i2c_idx < MAX_I2C; ++i2c_idx) {
        uint8_t found = 0;

        // Defaults.
        #define FIELD(name, type, default_value) config->i2c[i2c_idx].name = (type)(default_value);
        I2C_CFG_FIELDS
        #undef FIELD

        #define FIELD(name, type, default_value)                                           \
            {                                                                              \
                char key[16];                                                              \
                int n = snprintf(key, sizeof(key), "i2c%u.%s", (unsigned)i2c_idx, #name);  \
                result = JSON_SearchConst(json, json_len,                                  \
                                          key, (size_t)n,                                 \
                                          &value, &value_length, &value_type);            \
                if (result == JSONSuccess && value_type == JSONNumber) {                   \
                    char save = value[value_length];                                       \
                    ((char *)value)[value_length] = '\0';                                  \
                    int v = atoi(value);                                                   \
                    ((char *)value)[value_length] = save;                                  \
                    if (strcmp(#name, "baud") == 0) {                                      \
                        config->i2c[i2c_idx].baud = (type)v;                                \
                    } else {                                                               \
                        if (i2c_pin_valid(i2c_idx, #name, v)) {                            \
                            config->i2c[i2c_idx].name = (type)v;                           \
                            found += claim_gpio(config->i2c[i2c_idx].name);                \
                        } else {                                                           \
                            err("i2c%u." #name ": invalid GPIO %d", (unsigned)i2c_idx, v); \
                        }                                                                  \
                    }                                                                      \
                }                                                                          \
            }
        I2C_CFG_FIELDS
        #undef FIELD

        config->i2c_mask |= (uint8_t)((found == 2) << i2c_idx);
    }
}

static void parse_ssd1306(const char *json, size_t json_len, config_t *config)
{
    JSONStatus_t result;
    const char *start = NULL;
    size_t length = 0;
    JSONTypes_t type = JSONInvalid;

    // Defaults.
    #define FIELD(name, type, default_value) config->ssd1306.name = (type)(default_value);
    SSD1306_CFG_FIELDS
    #undef FIELD

    result = JSON_SearchConst(json, json_len,
                              "ssd1306", strlen("ssd1306"),
                              &start, &length, &type);
    if (result != JSONSuccess)
        return;
    if (type != JSONObject) {
        err("ssd1306: expected object");
        config->ssd1306.i2c_idx = -1;
        return;
    }

    #define FIELD(name, type, default_value)                                           \
        {                                                                              \
            const char *value = NULL;                                                  \
            size_t value_length = 0;                                                   \
            JSONTypes_t value_type = JSONInvalid;                                      \
            result = JSON_SearchConst(start, length,                                   \
                                      #name, strlen(#name),                            \
                                      &value, &value_length, &value_type);             \
            if (result == JSONSuccess && value_type == JSONNumber) {                   \
                char save = value[value_length];                                       \
                ((char *)value)[value_length] = '\0';                                  \
                config->ssd1306.name = (type)atoi(value);                              \
                ((char *)value)[value_length] = save;                                  \
            }                                                                          \
        }
    SSD1306_CFG_FIELDS
    #undef FIELD

    if ((uint8_t)config->ssd1306.i2c_idx >= MAX_I2C) {
        err("ssd1306.i2c_idx: invalid %d", (int)config->ssd1306.i2c_idx);
        config->ssd1306.i2c_idx = -1;
        return;
    }

    if (!(config->i2c_mask & (uint8_t)(1u << (uint8_t)config->ssd1306.i2c_idx))) {
        err("ssd1306: i2c%u is not configured", (unsigned)config->ssd1306.i2c_idx);
        config->ssd1306.i2c_idx = -1;
        return;
    }
}

static uint8_t parse_sensor_role_name(const char *s, size_t len, uint8_t default_role)
{
    if (len == 9 && memcmp(s, "mousemove", 9) == 0)
        return SENSOR_ROLE_MOUSEMOVE;
    if (len == 5 && memcmp(s, "mouse", 5) == 0)
        return SENSOR_ROLE_MOUSEMOVE;
    if (len == 6 && memcmp(s, "scroll", 6) == 0)
        return SENSOR_ROLE_SCROLL;
    return default_role;
}

static void parse_pmw33xx_drivers(const char *json,
                                 size_t json_len,
                                 const char *query,
                                 const char *name,
                                 config_t *config,
                                 pmw33xx_cfg_t *out,
                                 uint8_t *out_count,
                                 uint8_t max_out)
{
    JSONStatus_t result;
    const char *start = NULL;
    size_t length = 0;
    JSONTypes_t type = JSONInvalid;

    *out_count = 0;

    result = JSON_SearchConst(json, json_len, query, strlen(query), &start, &length, &type);
    if (result != JSONSuccess) {
        return;
    }
    if (type != JSONArray) {
        err("drivers.%s: expected array", name);
        *out_count = 0;
        return;
    }

    size_t it_start = 0, it_next = 0;
    JSONPair_t pair = {0};
    for (uint8_t idx = 0; idx < max_out; idx++) {
        if (JSON_Iterate(start, length, &it_start, &it_next, &pair) != JSONSuccess)
            break;
        if (pair.jsonType != JSONObject) {
            err("drivers.%s[%u]: expected object", name, idx);
            continue;
        }

        pmw33xx_cfg_t dev = {0};
        #define FIELD(name, type, default_value) dev.name = (type)(default_value);
        PMW33XX_FIELDS(SENSOR_ROLE_MOUSEMOVE)
        #undef FIELD

        #define FIELD(name, type, default_value)                                              \
            {                                                                                 \
                const char *value = NULL;                                                     \
                size_t value_length = 0;                                                      \
                JSONTypes_t value_type = JSONInvalid;                                         \
                result = JSON_SearchConst(pair.value, pair.valueLength,                        \
                                          #name, strlen(#name),                                \
                                          &value, &value_length, &value_type);                \
                if (result != JSONSuccess) {                                                  \
                } else if (strcmp(#name, "role") == 0 && value_type == JSONString) {         \
                    dev.role = parse_sensor_role_name(value, value_length, dev.role);         \
                } else if (strcmp(#name, "role") != 0 && value_type == JSONNumber) {         \
                    char save = value[value_length];                                          \
                    ((char *)value)[value_length] = '\0';                                     \
                    dev.name = (type)atoi(value);                                             \
                    ((char *)value)[value_length] = save;                                     \
                }                                                                             \
            }
        PMW33XX_FIELDS(SENSOR_ROLE_MOUSEMOVE)
        #undef FIELD

        if ((uint8_t)dev.spi_idx >= MAX_SPI) {
            err("drivers.%s[%u].spi_idx: invalid %d", name, idx, (int)dev.spi_idx);
            continue;
        }

        if (!(config->spi_mask & (uint8_t)(1u << (uint8_t)dev.spi_idx)))
            continue;

        if (!claim_gpio(dev.cs) || !claim_gpio(dev.irq))
            continue;

        out[(*out_count)++] = dev;
    }
}

static void parse_pmw3360_drivers(const char *json, size_t json_len, config_t *config)
{
    parse_pmw33xx_drivers(json,
                          json_len,
                          "drivers.pmw3360",
                          "pmw3360",
                          config,
                          config->pmw3360,
                          &config->nr_pmw3360,
                          MAX_PMW3360);
}

static void parse_pmw3389_drivers(const char *json, size_t json_len, config_t *config)
{
    parse_pmw33xx_drivers(json,
                          json_len,
                          "drivers.pmw3389",
                          "pmw3389",
                          config,
                          config->pmw3389,
                          &config->nr_pmw3389,
                          MAX_PMW3389);
}

int parse(config_t *config, const char *filename)
{
    #define FIELD(name, type, default_value) config->name = default_value;
    CONFIG_FIELDS
    #undef FIELD

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

    // Iterate over fields using the X-macro.
    #define FIELD(name, type, default_value)                              \
        {                                                                 \
            const char *value = NULL;                                     \
            size_t value_length = 0;                                      \
            JSONTypes_t value_type = JSONInvalid;                         \
            result = JSON_SearchConst(json, json_len,                     \
                                        #name, strlen(#name),               \
                                        &value, &value_length,              \
                                        &value_type);                       \
            if (result != JSONSuccess) {                                  \
            } else if (value_type == JSONString) {                        \
                static char s[MAX_OS_NAME_LEN];                           \
                snprintf(s, sizeof(s), "%.*s", (int)value_length, value); \
                config->os = s;                                           \
            } else if (value_type == JSONNumber) {                        \
                char save = value[value_length];                          \
                ((char *)value)[value_length] = '\0';                     \
                int v = atoi(value);                                      \
                ((char *)value)[value_length] = save;                     \
                if (IS_PIN_FIELD(#name))                                  \
                    if (!claim_gpio(v))                                   \
                        v = default_value;                                \
                config->name = (type)v;                                   \
            }                                                             \
        }
    CONFIG_FIELDS
    #undef FIELD

    log_level = config->log_level;
    keyd_overlay_conf = config->os;

    parse_led_pins(json, json_len, config);
    parse_uart_buses(json, json_len, config);
    parse_i2c_buses(json, json_len, config);
    parse_ssd1306(json, json_len, config);


    config->matrix.nr_rows = parse_pin_array(json, json_len, "gpio_rows", config->matrix.gpio_rows, MAX_GPIOS);
    config->matrix.nr_cols = parse_pin_array(json, json_len, "gpio_cols", config->matrix.gpio_cols, MAX_GPIOS);
    
    parse_keymap(json, json_len, &config->matrix);

    parse_encoders(json, json_len, config);

    parse_spi_buses(json, json_len, config);
    parse_pmw3360_drivers(json, json_len, config);
    parse_pmw3389_drivers(json, json_len, config);

    pull_pins(json);


    if (config->matrix.nr_rows == 0 &&
        config->matrix.nr_cols == 0 &&
        config->nr_encoders == 0 &&
        config->nr_pmw3360 == 0 &&
        config->nr_pmw3389 == 0) {
        err("Config has no input devices.");
        vPortFree(json);
        return -1;
    }

    // Free the JSON buffer after parsing
    vPortFree(json);
    return 0;
}
