#include <stdio.h>
// #include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "ff.h" // FatFS header
#include "core_json.h"
#include "jconfig.h"
#include "keys.h"
#include "log.h"

#include "pico/stdlib.h"


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
        err("f_mount failed with error code: %d", res);
        return -1;
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
}

// Set a configuration value based on the field name
static int set_cfg_value(config_t *config, const char *field_name, int value) {
    #define FIELD(name, type, default_value) \
        if (strcmp(field_name, #name) == 0) { \
            config->name = value; \
            return 0; \
        }
    CONFIG_FIELDS
    #undef FIELD

    warn("Unknown field: %s", field_name);
    return -1;  // Field not found
}

static int parse_int_array(const char *json, size_t json_len, const char *key, int *array, size_t max_size) {
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
            array[element_count++] = atoi(pair.value);

            // Restore the original character
            ((char *)pair.value)[pair.valueLength] = save;
        } else {
            err("Non-integer value found, skipping.");
        }
    }

    return element_count;  // Return the number of elements parsed
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
    return KEYD_NOOP; // Assuming KEYD_NOOP is defined as 0 in keyd.h
}

static int parse_keymap(const char *json, size_t json_len, matrix_t *matrix) {
    JSONStatus_t result;
    const char *array_start;
    size_t array_length;

    // Search for the "keymap" array in the JSON
    result = JSON_SearchConst(json, json_len, "keymap", strlen("keymap"), &array_start, &array_length, NULL);
    if (result != JSONSuccess) {
        err("Failed to find 'keymap' in JSON.");
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
                warn("Unexpected type for keymap element at [%zu][%zu].", row, col);
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
        dbg("Failed to find 'pull_up_pins' array in JSON");
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
            GPIO_PULL_UP(pin);
            dbg3("GPIO %d: Pulled up", pin);
        } else {
            dbg3("Skipping non-integer value.");
        }
    }

    result = JSON_SearchConst(json_data, strlen(json_data),
                              "pull_down_pins", strlen("pull_down_pins"),
                              &array_start, &array_length, NULL);

    if (result != JSONSuccess) {
        dbg("Failed to find 'pull_down_pins' array in JSON");
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
            GPIO_PULL_DOWN(pin);
            dbg3("GPIO %d: Pulled down", pin);
        } else {
            dbg3("Skipping non-integer value.");
        }
    }
}

int parse(config_t *config, const char *filename) {
    init_default_cfg(config);

    char *json = NULL;
    int json_len = load(&json, filename);

    if (json_len < 0) {
        err("Could not find 'config.json'");
        vPortFree(json);
        return NO_JSON;
    }

    JSONStatus_t result = JSON_Validate(json, json_len);
    if (result != JSONSuccess) {
        err("Invalid JSON format.");
        vPortFree(json);  // Free the buffer before returning
        return INVALID_JSON;
    }

    pull_pins(json);
    
    char *value;
    size_t value_length;
    result = JSON_Search(json, json_len, "log_level", strlen("log_level"), &value, &value_length);
    if (result == JSONSuccess) {
        char save = value[value_length];  // Save the original character
        value[value_length] = '\0';       // Null-terminate the string

        log_level = atoi(value);          // Convert the value to an integer

        value[value_length] = save;       // Restore the original character
        dbg3("log_level: %d", log_level);
    } else {
        dbg3("Field not found: %s", "log_level");
    }

    // Iterate over fields using the X-Macro
    #define FIELD(name, type, default_value)                       \
        {                                                          \
            char *value;                                           \
            size_t value_length;                                   \
            result = JSON_Search(json, json_len, #name, strlen(#name), \
                                 &value, &value_length);           \
            if (result == JSONSuccess) {                           \
                char save = value[value_length];                   \
                value[value_length] = '\0';                        \
                set_cfg_value(config, #name, atoi(value));         \
                value[value_length] = save;                        \
            } else {                                               \
                dbg3("Field not found: %s", #name);            \
            }                                                      \
        }
    CONFIG_FIELDS
    #undef FIELD


    // Parse the integer array from the JSON
    int nr;
    nr = parse_int_array(json, json_len, "gpio_rows", config->matrix.gpio_rows, MAX_GPIOS);
    if (nr < 0) {
        err("Failed to parse gpio_rows.");
        vPortFree(json);
        return NO_GPIO_ROWS;
    }
    config->matrix.nr_rows = nr;

    // Parse the integer array from the JSON
    nr = parse_int_array(json, json_len, "gpio_cols", config->matrix.gpio_cols, MAX_GPIOS);
    if (nr < 0) {
        err("Failed to parse gpio_cols.");
        vPortFree(json);
        return NO_GPIO_COLS;
    }
    config->matrix.nr_cols = nr;

    if (parse_keymap(json, json_len, &config->matrix)) {
        err("Failed to parse keymap.");
        vPortFree(json);
        return KEYMAP_ERROR;
    }

    // Free the JSON buffer after parsing
    vPortFree(json);
    return SUCCESS;
}