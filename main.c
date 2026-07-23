 /*
 * ErgoType - Keyboard Solutions
 *
 * © 2024 Nazarii Tupitsa (see also: LICENSE-ErgoType).
 */
#include "platform/board.h"
#include "platform/cdc.h"
#include "platform/gpio.h"
#include "platform/i2c.h"
#include "platform/os.h"
#include "platform/time.h"
#include "platform/usb.h"

#include "usb_descriptors.h"

#include "keyd.h"
#include "vkbd/vkbd_event.h"
#include "jconfig.h"
#include "platform/storage.h"
#include "ui/ui.h"
#include "pointing/pointer.h"
#include "log.h"

// #define TUD_STACK_SIZE 16384 // storage writes may use 4096 byte work buffers
#define TUD_STACK_SIZE PLATFORM_STACK_DEPTH(16384u) // storage writes may use 4096 byte work buffers
#define MIN_STACK_SIZE PLATFORM_STACK_DEPTH(2048u)
#define IDLE_PRIORITY tskIDLE_PRIORITY

#define TUSB_PRIORITY ( configMAX_PRIORITIES - 2 ) // keep below timer task

#define CORE0 ( ( UBaseType_t ) ( 1u << 0 ) )
#define CORE1 ( ( UBaseType_t ) ( 1u << 1 ) )

// Shared runtime symbols (owned by main.c).
uint8_t mode; // read by USB descriptor callbacks
uint8_t hid_output_profile = HID_OUTPUT_PROFILE_NKRO_KB_MOUSE;
SemaphoreHandle_t log_mutex;
SemaphoreHandle_t fatfs_mutex;
QueueHandle_t vkbd_event_queue;
QueueHandle_t input_event_queue;

static const uint8_t default_hid_output_profile = HID_OUTPUT_PROFILE_NKRO_KB_MOUSE;

// FreeRTOS tasks
void keyscan_task(void* pvParameters); // keyscan.c
void keyd_task(void *pvParameters); // keyd/port/task.c:
void vkbd_hid_boot_task(void *pvParameters); // keyd/port/vkbd/tusb_hid.c
void vkbd_hid_nkro_task(void *pvParameters); // keyd/port/vkbd/tusb_hid.c
uint8_t count_pressed_keys(config_t *config, uint8_t *single_code); // keyscan.c
bool uart_stdio_init(const config_t *config); // uart_stdio.c
extern const char *keyd_overlay_conf;

static void app_task(void *pvParameters);

static void init_i2c_buses(const config_t *config)
{
    static platform_i2c_t *const i2c_by_idx[MAX_I2C] = { &platform_i2c0, &platform_i2c1 };
    for (uint8_t bus = 0; bus < MAX_I2C; ++bus) {
        if (!(config->i2c_mask & (uint8_t)(1u << bus)))
            continue;
        platform_i2c_t *i2c = i2c_by_idx[bus];
        const i2c_cfg_t *bus_cfg = &config->i2c[bus];
        platform_gpio_set_function((uint)bus_cfg->sda, PLATFORM_GPIO_FUNC_I2C);
        platform_gpio_set_function((uint)bus_cfg->scl, PLATFORM_GPIO_FUNC_I2C);
        platform_gpio_pull_up((uint)bus_cfg->sda);
        platform_gpio_pull_up((uint)bus_cfg->scl);
        (void)platform_i2c_init(i2c, bus_cfg->baud);
    }
}

void on_layout_change(const char *name)
{
    ui_notify_layout(name);
}

int8_t init_and_read_pin(int pin) {
    if (!IS_GPIO_PIN(pin))
        return -1;
    platform_gpio_set_function(pin, PLATFORM_GPIO_FUNC_SIO);
    platform_gpio_init(pin);
    platform_gpio_pull_up(pin);
    platform_gpio_set_dir(pin, PLATFORM_GPIO_IN);
    platform_sleep_us(2000); // for YD2040 USR button
    return platform_gpio_get(pin);
}

typedef struct {
    uint8_t mode;
    const char *reason;
} mode_resolution_t;

static void select_profile(uint8_t code)
{
    switch (code) {
    case KEYD_A:
        keyd_overlay_conf = "android.conf";
        break;
    case KEYD_W:
        keyd_overlay_conf = "windows.conf";
        break;
    case KEYD_M:
        keyd_overlay_conf = "macos.conf";
        break;
    case KEYD_L:
        hid_output_profile = HID_OUTPUT_PROFILE_BOOT_KB_MOUSE;
        /* fallthrough */
    case KEYD_D:
        keyd_overlay_conf = NULL;
        break;
    default:
        return;
    }
}

static mode_resolution_t resolve_mode(config_t *config, uint8_t base_mode, uint8_t nr_pressed) {
    mode_resolution_t resolution = {
        .mode = base_mode,
        .reason = base_mode == HID ? "base HID" : "config parse failed",
    };

    if (resolution.mode == HID) {

        int erase_pin_state = init_and_read_pin(config->erase_pin);
        switch (erase_pin_state) { // Read the button state (active low)
        case 0:
            platform_storage_format();
            resolution.mode = MSC;
            resolution.reason = "erase pin active";
            break;
        case 1:
            break;
        default:
            if (config->nr_pressed_erase > 0 && nr_pressed >= config->nr_pressed_erase) {
                platform_storage_format();
                resolution.mode = MSC;
                resolution.reason = "nr_pressed >= nr_pressed_erase";
            }
        }
    }

    if (resolution.mode == HID) {
        int msc_pin_state = init_and_read_pin(config->msc_pin);
        switch (msc_pin_state) { // Read the button state (active low)
        case 0:
            resolution.mode = MSC;
            resolution.reason = "msc pin active";
            break;
        case 1:
            break;
        default:
            if (config->nr_pressed_msc > 0 && nr_pressed >= config->nr_pressed_msc) {
                resolution.mode = MSC;
                resolution.reason = "nr_pressed >= nr_pressed_msc";
            }
        }
    }

	return resolution;
}

static void app_task(void *pvParameters)
{
    (void)pvParameters;

    msg("");

    static config_t config;

    int parse_rc = parse(&config, "config.json");
    uint8_t base_mode = (parse_rc == 0) ? HID : MSC; // HID by default; if config parse failed -> start MSC
    hid_output_profile = default_hid_output_profile;
    init_i2c_buses(&config);

    // Optional: enable "plain" printf()/puts() over UART if config pins are valid.
    // uart_stdio_init(&config);

    uint8_t startup_key = 0;
    uint8_t nr_pressed = count_pressed_keys(&config, &startup_key);
    select_profile(startup_key);
    mode_resolution_t mode_resolution = resolve_mode(&config, base_mode, nr_pressed);
    mode = mode_resolution.mode;

    fatfs_mutex = xSemaphoreCreateMutex();

    // if (mode == HID) {
    if (mode == HID && hid_output_profile != HID_OUTPUT_PROFILE_NKRO_KB_MOUSE) {
        platform_task_create_affinity(platform_usb_device_task, NULL, MIN_STACK_SIZE, NULL, TUSB_PRIORITY,
                               CORE0, NULL);
    } else {
        platform_task_create_affinity(platform_usb_device_task, NULL, TUD_STACK_SIZE, NULL, TUSB_PRIORITY,
                               CORE0, NULL);
    }
    dbg("startup keys: count=%u key=%u", nr_pressed, startup_key);

    // Print mode/config diagnostics (buffered until CDC is actually opened by the host).
    if (base_mode != HID) {
        warn("base mode=MSC: config parse failed");
    }
    msg("mode resolved: %s (%s)",
        mode == HID ? "HID" : "MSC",
        mode_resolution.reason);
    dbg3config(&config);
    

    if (config.nr_leds != 0 ||
        config.ws2812_pin != -1 ||
        config.ssd1306.i2c_idx != -1) {
        TaskHandle_t ui_task_handle = NULL;
        platform_task_create_affinity(ui_task, NULL, MIN_STACK_SIZE, &config, IDLE_PRIORITY, CORE1, &ui_task_handle);
        configASSERT(ui_task_handle);
        ui_handle = ui_task_handle;
    }

    
    if (mode == HID) {
        vkbd_event_queue = xQueueCreate(256, sizeof(vkbd_event_t));
        configASSERT(vkbd_event_queue);

        input_event_queue = xQueueCreate(64, sizeof(struct device_event));
        configASSERT(input_event_queue);

        platform_task_create_affinity(keyscan_task,  NULL, MIN_STACK_SIZE, &config, IDLE_PRIORITY + 3, CORE1, NULL);

        if (hid_output_profile == HID_OUTPUT_PROFILE_NKRO_KB_MOUSE) {
            dbg("hid output profile: nkro");
            platform_task_create_affinity(vkbd_hid_nkro_task, NULL, MIN_STACK_SIZE, NULL, IDLE_PRIORITY + 2, CORE0, NULL);
        } else {
            dbg("hid output profile: boot");
            platform_task_create_affinity(vkbd_hid_boot_task, NULL, MIN_STACK_SIZE, NULL, IDLE_PRIORITY + 2, CORE0, NULL);
        }

        if (config.nr_pmw3360 || config.nr_pmw3389) {
            TaskHandle_t pointing_task_handle = NULL;
            platform_task_create_affinity(pointing_device_task, NULL, MIN_STACK_SIZE, &config, IDLE_PRIORITY + 5, CORE1, &pointing_task_handle);
            for (uint8_t i = 0; i < config.nr_pmw3360; ++i)
                pointing_motion_irq_init(pointing_task_handle, config.pmw3360[i].irq, i);
            for (uint8_t i = 0; i < config.nr_pmw3389; ++i)
                pointing_motion_irq_init(pointing_task_handle, config.pmw3389[i].irq, (uint8_t)(MAX_PMW3360 + i));
        }

        platform_task_create_affinity(keyd_task, NULL, PLATFORM_STACK_DEPTH(32768u), NULL, IDLE_PRIORITY + 4, CORE0, NULL); // empirically: min free watermark was 3408 words
    }

    vTaskDelete(NULL);
}

void app_start(void)
{
    platform_board_init();
    log_mutex = xSemaphoreCreateMutex();
    platform_cdc_init();
    platform_task_create_affinity(app_task, NULL, 4*MIN_STACK_SIZE, NULL, TUSB_PRIORITY - 1, CORE0, NULL);
}
