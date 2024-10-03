/* main.c */
#include "bsp/board.h"
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "tusb.h"
#include "ff.h"
// #include "ffconf.h"
// #include "pico/cyw43_arch.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// #include "bootsel_button.h"
#include "flash.h"
#include "porting.h"
#include "daemon.h"


//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+
#define MODE_PIN 18 // GPIO18 for mode selection
#define CONFIG_TASK_PRIORITY (configMAX_PRIORITIES - 2)
#define CONFIG_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 4)


#define TUD_TASK_STACK_SIZE 16384 // flash_fat_write requires 4096 bytes
// #define MAIN_TASK_STACK_SIZE 1024
#define HID_STACK_SZIE      configMINIMAL_STACK_SIZE

// static FATFS filesystem;

// static void test_and_init_filesystem(void) {
//     f_mount(&filesystem, "/", 1);
//     f_unmount("/");
// }

// Mode selection variable
uint32_t mode = 1; // 0: HID mode, 1: MSC mode
QueueHandle_t eventQueue;

// FreeRTOS tasks
void led_task(void* pvParameters); // led.c
void hid_app_task(void* pvParameters); // hid.c
void msc_app_task(void* pvParameters); // msc.c
void usb_device_task(void* pvParameters); // tud.c

int main() {

    mode = 1;
    board_init();
    stdio_init_all();


    // Read mode selection button
    gpio_init(MODE_PIN);
    gpio_pull_up(MODE_PIN);
    gpio_set_dir(MODE_PIN, GPIO_IN);

    // Read the button state (active low)
    mode = gpio_get(MODE_PIN) ? 0 : 1;
    printf("Mode selected: %s\n", mode == 0 ? "HID" : "MSC");

    tud_init(BOARD_TUD_RHPORT);
    if (board_init_after_tusb) {
        board_init_after_tusb();
    }
    tusb_init();
    printf("Initialized\n");

    // test_and_init_filesystem();
    printf("Filesystem tested\n");

    
    
    printf("Starting os\n");



    //--------------------------------------------------------------------------------

    if (mode == 0) { // HID mode
        printf("Enter HID mode\n");
        xTaskCreate(usb_device_task, "usb_device_task", HID_STACK_SZIE, NULL, configMAX_PRIORITIES - 2, NULL);
        eventQueue = xQueueCreate(10, sizeof(struct event));
        // xTaskCreate(hid_app_task, "hid_app", 256, NULL, tskIDLE_PRIORITY + 2, NULL);
        BaseType_t taskCreated;
        xTaskCreate(keys_task, "hid_app", 256, NULL, configMAX_PRIORITIES  - 4, NULL);
        
        printf("before task free heap size: %u bytes\n", xPortGetFreeHeapSize());
        taskCreated = xTaskCreate(keyd_task, "hid_app", 8*1024, NULL, configMAX_PRIORITIES - 3, NULL);

        // Check if the task was created successfully and print the result
        if(taskCreated == pdPASS) {
            printf("Keyd task created successfully!\n");
        } else {
            printf("Failed to create keyd task, returned: %d\n", taskCreated);
        }
    }
    else {

        xTaskCreate(usb_device_task, "usb_device_task", TUD_TASK_STACK_SIZE, NULL, configMAX_PRIORITIES - 2, NULL);

    }

    xTaskCreate(led_task, "led", 128, NULL, tskIDLE_PRIORITY + 1, NULL);
    vTaskStartScheduler(); // block thread and pass control to FreeRTOS

    while (1) { };
    // task_read_conf();
    // TODO: flash format mechanism by calling flash_fat_initialize();
    // todo: sdk/2.0.0/lib/tinyusb/lib/fatfs/source/ffconf.h
    return 0;
}


// static char *read_file()
// {
//     char *buf = NULL;
//     char line[MAX_LINE_LEN+1];
//     int buf_sz = MAX_LINE_LEN;     // Current size of the buffer
//     int total_sz = 0;   // Total size of the data read

//     // Mount the filesystem
//     FRESULT res = f_mount(&filesystem, "/", 1);
//     if (res != FR_OK) {
//         printf("f_mount fail rc=%d\n", res);
//         return NULL;
//     }

//     FIL fh;
//     // Open the file for reading
//     res = f_open(&fh, "keyd.conf", FA_READ);
//     if (res != FR_OK) {
//         f_unmount("/");
//         printf("failed to open keyd.conf\n");
//         return NULL;
//     }

//     // Read file line by line
//     while (f_gets(line, sizeof line, &fh)) {
//         // Skip lines that start with '#'
//         if (line[0] == '#') {
//             continue;  // Ignore this line and go to the next one
//         }

//         int len = strlen(line);

//         // Ensure the line ends with a newline character
//         if (line[len-1] != '\n') {
//             if (len >= MAX_LINE_LEN) {
//                 printf("maximum line length exceeded (%d)\n", MAX_LINE_LEN);
//                 goto fail;
//             } else {
//                 line[len++] = '\n';
//             }
//         }

//         if ((len+total_sz) > MAX_FILE_SZ) {
// 			printf("maximum file size exceed (%d)", MAX_FILE_SZ);
// 			goto fail;
// 		}
        
//         if (!total_sz) {
//             buf = (char *)malloc(len);
//             if (!buf) {
//                 printf("failed to allocate more memory for buf\n");
//                 goto fail;
//             }
//         } else {
//             buf_sz += len;  // Double the buffer size
//             char *new_buf = realloc(buf, buf_sz);
//             if (!new_buf) {
//                 printf("failed to allocate more memory for buf\n");
//                 goto fail;
//             }
//             buf = new_buf;
//         }

//         // str(line, include_prefix) == line) {
// 		// 	FIL *fd;
// 		// 	const char *resolved_path;
// 		// 	char *include_path = line+sizeof(include_prefix)-1;

// 		// 	line[len-1] = 0;

// 		// 	while (include_path[0] == ' ')
// 		// 		include_path++;

// 		// 	resolved_path = resolve_include_path(path, include_path);

// 		// 	if (!resolved_path) {
// 		// 		// warn("failed to resolve include path: %s", include_path);
// 		// 		continue;
// 		// 	}

// 		// 	fd = f_open(resolved_path, O_RDONLY);

// 		// 	if (fd < 0) {
// 		// 		// warn("failed to include %s", include_path);
// 		// 		// perror("open");
// 		// 	} else {
// 		// 		int n;
// 		// 		while ((n = f_read(fd, buf+sz, sizeof(buf)-sz)) > 0)
// 		// 			sz += n;
// 		// 		f_close(fd);
// 		// 	}
// 		// } else {

//         // Copy the line to the buffer
//         strcpy(buf + total_sz, line);
//         total_sz += len;
//     }

//     f_close(&fh);
//     f_unmount("/");

//     if (!buf) {
//         buf[total_sz] = '\0';  // Null-terminate the buffer
//     }
//     return buf;

// fail:
//     free(buf);  // Free the memory in case of failure
//     f_close(&fh);
//     f_unmount("/");
//     return NULL;
// }

// int config_parse(struct config *config)
// {
// 	char *content;
//     if (!(content = read_file())) {
//         printf("no content read\n");
// 		return -1;
//     } else {
//         printf("Config\n");
//         printf("%s\n", content);
//         printf("\nConfig End\n");
//     }

// 	config_init(config);
// 	snprintf(config->path, sizeof(config->path), "%s", "\0");
// 	return config_parse_string(config, content);
// }
