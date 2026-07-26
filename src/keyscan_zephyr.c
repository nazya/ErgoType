#include <stdbool.h>
#include <stdint.h>

#include "device.h"
#include "jconfig.h"
#include "keys.h"
#include "platform/events.h"
#include "platform/gpio.h"
#include "platform/time.h"

static uint8_t debouncing_time;
static bool debouncing;

typedef struct {
    uint8_t prev;
    int16_t accum;
} encoder_state_t;

static bool is_encoder_channel(const config_t *config, uint8_t row, uint8_t col)
{
    for (uint8_t i = 0; i < config->nr_encoders; ++i) {
        const encoder_t *enc = &config->encoders[i];
        if ((row == enc->a && col == enc->c) || (row == enc->b && col == enc->c))
            return true;
    }
    return false;
}

static inline bool matrix_pressed(const matrix_row_t *matrix, uint8_t row, uint8_t col)
{
    return (matrix[row] & ((matrix_row_t)1 << col)) != 0;
}

static void send_key_tap(uint8_t code)
{
    struct device_event ev = {0};
    ev.type = DEV_KEY;
    ev.code = code;

    ev.pressed = 1;
    platform_input_event_send(&ev, PLATFORM_EVENT_WAIT_FOREVER);
    ev.pressed = 0;
    platform_input_event_send(&ev, PLATFORM_EVENT_WAIT_FOREVER);
}

static void process_encoders(const config_t *config,
                             encoder_state_t *states,
                             const matrix_row_t *raw_matrix)
{
    static const int8_t quad_table[16] = {
        0, -1, 1, 0,
        1, 0, 0, -1,
        -1, 0, 0, 1,
        0, 1, -1, 0,
    };

    for (uint8_t i = 0; i < config->nr_encoders; ++i) {
        const encoder_t *enc = &config->encoders[i];
        encoder_state_t *st = &states[i];

        uint8_t curr = 0;
        if (matrix_pressed(raw_matrix, enc->a, enc->c))
            curr |= 0x1;
        if (matrix_pressed(raw_matrix, enc->b, enc->c))
            curr |= 0x2;

        if (st->prev == 0xFF) {
            st->prev = curr;
            st->accum = 0;
            continue;
        }

        uint8_t idx = (uint8_t)((st->prev << 2) | curr);
        int16_t delta = quad_table[idx];

        st->prev = curr;
        if (delta > 0 && st->accum < 0)
            st->accum = 0;
        else if (delta < 0 && st->accum > 0)
            st->accum = 0;
        st->accum += delta;

        while (st->accum >= (int16_t)enc->div) {
            st->accum -= (int16_t)enc->div;
            send_key_tap(config->matrix.keymap[enc->a][enc->c]);
        }
        while (st->accum <= -(int16_t)enc->div) {
            st->accum += (int16_t)enc->div;
            send_key_tap(config->matrix.keymap[enc->b][enc->c]);
        }
    }
}

static const uint8_t row2col = 0;
static uint8_t col2row = 1;

static void select_row(matrix_t *matrix, uint8_t row)
{
    platform_gpio_output(matrix->gpio_rows[row], false);
}

static void unselect_row(matrix_t *matrix, uint8_t row)
{
    platform_gpio_input_pullup(matrix->gpio_rows[row]);
}

static void select_col(matrix_t *matrix, uint8_t col)
{
    platform_gpio_output(matrix->gpio_cols[col], false);
}

static void unselect_col(matrix_t *matrix, uint8_t col)
{
    platform_gpio_input_pullup(matrix->gpio_cols[col]);
}

void matrix_init(matrix_t *matrix)
{
    if (col2row) {
        for (uint8_t row = 0; row < matrix->nr_rows; row++)
            unselect_row(matrix, row);
        for (uint8_t col = 0; col < matrix->nr_cols; col++)
            unselect_col(matrix, col);
    } else if (row2col) {
        for (uint8_t col = 0; col < matrix->nr_cols; col++)
            unselect_col(matrix, col);
        for (uint8_t row = 0; row < matrix->nr_rows; row++)
            unselect_row(matrix, row);
    } else {
        for (uint8_t row = 0; row < matrix->nr_rows; row++)
            unselect_row(matrix, row);
        for (uint8_t col = 0; col < matrix->nr_cols; col++)
            platform_gpio_input_pullup(matrix->gpio_cols[col]);
    }
}

static inline bool popcount_more_than_one(matrix_row_t rowdata)
{
    rowdata &= rowdata - 1;
    return rowdata != 0;
}

static matrix_row_t get_real_keys(matrix_t *matrix, uint8_t row, matrix_row_t rowdata)
{
    matrix_row_t out = 0;
    for (uint8_t col = 0; col < matrix->nr_cols; col++) {
        if (matrix->keymap[row][col] && (rowdata & ((matrix_row_t)1 << col)))
            out |= ((matrix_row_t)1 << col);
    }
    return out;
}

static bool has_ghost_in_row(matrix_t *matrix, uint8_t row, matrix_row_t rowdata,
                             matrix_row_t *current_matrix)
{
    if (col2row || row2col)
        return false;
    rowdata = get_real_keys(matrix, row, rowdata);
    if (!popcount_more_than_one(rowdata))
        return false;
    for (uint8_t i = 0; i < matrix->nr_rows; i++) {
        if (i != row && popcount_more_than_one(get_real_keys(matrix, i, current_matrix[i]) & rowdata))
            return true;
    }
    return false;
}

void matrix_scan(matrix_t *matrix, matrix_row_t *raw_matrix)
{
    if (col2row) {
        for (uint8_t row = 0; row < matrix->nr_rows; row++) {
            matrix_row_t row_value = 0;
            select_row(matrix, row);
            platform_sleep_us(20);
            for (uint8_t col = 0; col < matrix->nr_cols; col++) {
                int pin_state = platform_gpio_read(matrix->gpio_cols[col]);
                if (pin_state == 0)
                    row_value |= ((matrix_row_t)1 << col);
            }
            unselect_row(matrix, row);
            raw_matrix[row] = row_value;
        }
    } else if (row2col) {
        for (uint8_t col = 0; col < matrix->nr_cols; col++) {
            matrix_row_t col_mask = ((matrix_row_t)1 << col);
            select_col(matrix, col);
            platform_sleep_us(20);
            for (uint8_t row = 0; row < matrix->nr_rows; row++) {
                int pin_state = platform_gpio_read(matrix->gpio_rows[row]);
                if (pin_state == 0)
                    raw_matrix[row] |= col_mask;
                else
                    raw_matrix[row] &= ~col_mask;
            }
            unselect_col(matrix, col);
        }
    } else {
        for (uint8_t row = 0; row < matrix->nr_rows; row++) {
            matrix_row_t row_value = 0;
            uint8_t row_pin = matrix->gpio_rows[row];
            platform_gpio_output(row_pin, false);

            for (uint8_t r = 0; r < matrix->nr_rows; r++) {
                if (r != row)
                    platform_gpio_input_pullup(matrix->gpio_rows[r]);
            }
            platform_sleep_us(20);
            for (uint8_t col = 0; col < matrix->nr_cols; col++) {
                int pin_state = platform_gpio_read(matrix->gpio_cols[col]);
                if (pin_state == 0)
                    row_value |= ((matrix_row_t)1 << col);
            }
            platform_gpio_input_pullup(row_pin);
            raw_matrix[row] = row_value;
        }
    }
}

bool debounce(matrix_row_t *raw_matrix, matrix_row_t *debounced_matrix, uint8_t nr_rows)
{
    static matrix_row_t matrix_debouncing[MAX_GPIOS];
    static uint32_t last_update;
    bool changed = false;

    for (uint8_t i = 0; i < nr_rows; i++) {
        if (matrix_debouncing[i] != raw_matrix[i]) {
            matrix_debouncing[i] = raw_matrix[i];
            debouncing = true;
            last_update = platform_uptime_ms();
        }
    }

    if (debouncing && (platform_uptime_ms() - last_update > debouncing_time)) {
        for (uint8_t i = 0; i < nr_rows; i++) {
            if (debounced_matrix[i] != matrix_debouncing[i]) {
                debounced_matrix[i] = matrix_debouncing[i];
                changed = true;
            }
        }
        debouncing = false;
    }

    return changed;
}

uint8_t count_pressed_keys(config_t *config, uint8_t *single_code)
{
    matrix_row_t raw_matrix[MAX_GPIOS] = {0};
    uint8_t pressed_count = 0;
    uint8_t code = 0;

    matrix_init(&config->matrix);
    matrix_scan(&config->matrix, raw_matrix);

    for (uint8_t row = 0; row < config->matrix.nr_rows; row++) {
        for (uint8_t col = 0; col < config->matrix.nr_cols; col++) {
            if (raw_matrix[row] & ((matrix_row_t)1 << col)) {
                if (!is_encoder_channel(config, row, col)) {
                    pressed_count++;
                    if (pressed_count == 1)
                        code = config->matrix.keymap[row][col];
                }
            }
        }
    }

    *single_code = pressed_count == 1 ? code : 0;
    return pressed_count;
}

void keyscan_task(void *arg)
{
    config_t *config = arg;
    debouncing_time = config->debounce;
    debouncing = debouncing_time > 0;
    static matrix_row_t raw_matrix[MAX_GPIOS];
    static matrix_row_t debounced_matrix[MAX_GPIOS];
    static matrix_row_t previous_debounced_matrix[MAX_GPIOS];
    static encoder_state_t encoder_states[MAX_ENCODERS];
    struct device_event devev = {
        .type = DEV_KEY,
    };

    matrix_init(&config->matrix);
    for (uint8_t i = 0; i < MAX_ENCODERS; ++i) {
        encoder_states[i].prev = 0xFF;
        encoder_states[i].accum = 0;
    }

    for (;;) {
        uint32_t started = platform_uptime_ms();
        matrix_scan(&config->matrix, raw_matrix);
        if (config->nr_encoders > 0)
            process_encoders(config, encoder_states, raw_matrix);

        bool debounced_changed = debounce(raw_matrix, debounced_matrix, config->matrix.nr_rows);
        if (debounced_changed) {
            for (uint8_t row = 0; row < config->matrix.nr_rows; row++) {
                matrix_row_t changed_keys = debounced_matrix[row] ^ previous_debounced_matrix[row];
                if (!changed_keys) {
                    previous_debounced_matrix[row] = debounced_matrix[row];
                    continue;
                }
                if (has_ghost_in_row(&config->matrix, row, debounced_matrix[row], debounced_matrix)) {
                    debounced_matrix[row] = 0;
                    previous_debounced_matrix[row] = debounced_matrix[row];
                    continue;
                }
                for (uint8_t col = 0; col < config->matrix.nr_cols; col++) {
                    matrix_row_t col_mask = ((matrix_row_t)1 << col);
                    if (!(changed_keys & col_mask))
                        continue;
                    if (is_encoder_channel(config, row, col))
                        continue;

                    devev.pressed = (debounced_matrix[row] & col_mask) != 0;
                    devev.code = config->matrix.keymap[row][col];
                    platform_input_event_send(&devev, PLATFORM_EVENT_WAIT_FOREVER);
                }
                previous_debounced_matrix[row] = debounced_matrix[row];
            }
        }

        uint32_t elapsed = platform_uptime_ms() - started;
        if (elapsed < (uint32_t)config->scan_period)
            platform_sleep_ms((uint32_t)config->scan_period - elapsed);
    }
}
