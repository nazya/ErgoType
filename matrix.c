#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "bsp/board.h"
#include "ff.h"           // FatFS for TinyUSB
#include "keys.h"         // Contains keycode_table and other keyd definitions
#include "matrix.h"

#define MAX_COLS 16
#define MAX_ROWS 16

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

int parse_matrix_file(matrix_t *matrix)
{
    int res = 0;
    FATFS filesystem;
    res = f_mount(&filesystem, "/", 1);
    if (res != FR_OK) {
        printf("f_mount fail rc=%d\n", res);
        return -1;
    }

    FRESULT fr;
    FIL file;
    char line[MAX_LINE_LENGTH + 1];
    char *token;

    // Open the matrix.csv file for reading
    fr = f_open(&file, "matrix.csv", FA_READ);
    if (fr != FR_OK) {
        f_unmount("/");
        printf("Failed to open matrix.csv: %d\n", fr);
        return -1;
    }

    // Initialize matrix structure to zero
    memset(matrix, 0, sizeof(matrix_t));

    // ----- Parse Configuration Lines -----

    // Read first line: ,1,2,3,4 (Column GPIOs)
    if (f_gets(line, sizeof(line), &file) == NULL) {
        printf("Failed to read column GPIOs line\n");
        res = -1;
        goto cleanup;
    }
    printf("Read line: '%s'\n", line);

    // Remove leading whitespace
    char *ptr = line;
    while (isspace((unsigned char)*ptr))
        ptr++;

    size_t len = strlen(ptr);

    // Remove trailing whitespace
    while (len > 0 && isspace((unsigned char)ptr[len - 1]))
        len--;

    ptr[len] = '\0'; // Null-terminate the trimmed line

    printf("Trimmed line: '%s'\n", ptr);

    token = strtok(ptr, ",");
    printf("First token: '%s'\n", token ? token : "NULL");
    if (token != NULL) {
        while (isspace((unsigned char)*token))
            token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace((unsigned char)*end))
            *end-- = '\0';

        printf("Trimmed token: '%s'\n", token);

        matrix->gpio_cols[matrix->num_cols++] = (uint8_t)atoi(token);
        printf("Added GPIO column: %d\n", matrix->gpio_cols[matrix->num_cols - 1]);
    }

    while ((token = strtok(NULL, ",")) != NULL && matrix->num_cols < MAX_COLS) {
        printf("Parsed token for column GPIO: '%s'\n", token);
        // Remove leading and trailing whitespace from token
        while (isspace((unsigned char)*token))
            token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace((unsigned char)*end))
            *end-- = '\0';

        printf("Trimmed token: '%s'\n", token);

        matrix->gpio_cols[matrix->num_cols++] = (uint8_t)atoi(token);
        printf("Added GPIO column: %d\n", matrix->gpio_cols[matrix->num_cols - 1]);
    }

    if (matrix->num_cols == 0) {
        printf("No columns defined in matrix.csv\n");
        res = -1;
        goto cleanup;
    } else {
        printf("Total columns parsed: %d\n", matrix->num_cols);
    }

    // ----- Parse Keymap Lines -----

    // Read subsequent lines: row GPIO and key names per column
    matrix->num_rows = 0;
    while (f_gets(line, sizeof(line), &file) != NULL && matrix->num_rows < MAX_ROWS) {
        printf("Read line: '%s'\n", line);

        // Remove leading whitespace
        ptr = line;
        while (isspace((unsigned char)*ptr))
            ptr++;

        len = strlen(ptr);

        // Remove trailing whitespace
        while (len > 0 && isspace((unsigned char)ptr[len - 1]))
            len--;

        ptr[len] = '\0'; // Null-terminate the trimmed line

        printf("Trimmed line: '%s'\n", ptr);

        if (len == 0)
            continue; // Empty line

        token = strtok(ptr, ",");
        if (token == NULL) {
            printf("Invalid row line, missing row GPIO\n");
            continue; // Skip invalid line
        }
        printf("Parsed row GPIO: '%s'\n", token);
        matrix->gpio_rows[matrix->num_rows] = (uint8_t)atoi(token);
        printf("Added GPIO row: %d\n", matrix->gpio_rows[matrix->num_rows]);

        // Parse key names for each column
        int c = 0;
        while ((token = strtok(NULL, ",")) != NULL && c < matrix->num_cols) {
            // Remove leading and trailing whitespace from token
            while (isspace((unsigned char)*token))
                token++;
            char *end = token + strlen(token) - 1;
            while (end > token && isspace((unsigned char)*end))
                *end-- = '\0';

            printf("Parsed key name: '%s'\n", token);

            if (strlen(token) == 0) {
                // No key assigned for this column in this row
                matrix->keymap[matrix->num_rows][c++] = KEYD_NOOP;
                continue;
            }

            uint8_t keycode = lookup_keycode(token);
            printf("Mapped keycode: %d\n", keycode);
            matrix->keymap[matrix->num_rows][c++] = keycode;
        }

        // Fill remaining columns with KEYD_NOOP
        while (c < matrix->num_cols) {
            matrix->keymap[matrix->num_rows][c++] = KEYD_NOOP;
        }

        matrix->num_rows++;
    }

    // ----- Initialize GPIOs Based on Parsed Matrix -----

    // // Initialize column GPIOs as outputs and set them high
    // for (int c = 0; c < matrix->num_cols; c++) {
    //     uint8_t gpio = matrix->gpio_cols[c];
    //     gpio_init(gpio);
    //     gpio_set_dir(gpio, GPIO_OUT);
    //     gpio_put(gpio, 1); // Set high by default; adjust if needed based on scanning direction
    //     printf("Initialized column GPIO %d as OUTPUT, set HIGH\n", gpio);
    // }

    // // Print column GPIOs
    // printf(",");
    // for (int col = 0; col < matrix->num_cols; col++) {
    //     printf("%d", matrix->gpio_cols[col]);
    //     if (col < matrix->num_cols - 1) {
    //         printf(",");
    //     }
    // }
    // printf("\n");

    // Print each row's GPIO and its corresponding keymap
    for (int row = 0; row < matrix->num_rows; row++) {
        // Print row GPIO
        printf("%d", matrix->gpio_rows[row]);

        // Print the key names for each column in this row
        for (int col = 0; col < matrix->num_cols; col++) {
            uint8_t keycode = matrix->keymap[row][col];
            const char *keyname = keycode_table[keycode].name;

            // Print the key name if it exists, otherwise print "noop"
            if (keyname) {
                printf(",%s", keyname);
            } else {
                printf(",noop");
            }
        }
        printf("\n");  // Newline at the end of the row
    }

cleanup:
    f_close(&file);
    f_unmount("/");
    return res;
}