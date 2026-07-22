/* tusb_config.h */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+

// RHPort number used for device can be defined by board.mk, default to port 0
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

// RHPort max operational speed can defined by board.mk
#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT      1
#endif

#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

// defined by compiler flags for flexibility
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE | BOARD_TUD_MAX_SPEED)
#define CFG_TUSB_RHPORT1_MODE     (OPT_MODE_HOST | BOARD_TUH_MAX_SPEED)
#define CFG_TUH_RPI_PIO_USB       1

// This examples use FreeRTOS
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_FREERTOS
#endif

// Espressif IDF requires "freertos/" prefix in include path
#if TUP_MCU_ESPRESSIF
#define CFG_TUSB_OS_INC_PATH  freertos/
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG            0
#endif

// Enable Device stack
#define CFG_TUD_ENABLED           1
#define CFG_TUH_ENABLED           1

// TinyUSB device task event queue depth (used when CFG_TUSB_OS != OPT_OS_NONE).
// Default in TinyUSB is 16. With FreeRTOS OSAL on RP2040 (SMP) + multiple classes
// (CDC+HID or CDC+MSC) + occasional long callbacks (e.g. flash I/O in MSC), the
// DCD ISR can temporarily outpace `tud_task_ext()` and overflow this queue,
// dropping XFER_COMPLETE events and causing "missing/disappearing" CDC output.
//
// Raising this is the smallest, most targeted fix: it increases burst tolerance
// without changing task structure or adding wait/notify experiments.
#ifndef CFG_TUD_TASK_QUEUE_SZ
#define CFG_TUD_TASK_QUEUE_SZ     64
#endif

// Default is max speed that hardware controller could support with on-chip PHY
#define CFG_TUD_MAX_SPEED         BOARD_TUD_MAX_SPEED

/* USB DMA on some MCUs can only access a specific SRAM region with restriction on alignment.
 * TinyUSB uses the following macros to declare transferring memory so that they can be put
 * into those specific sections.
 * e.g
 * - CFG_TUSB_MEM_SECTION : __attribute__ (( section(".usb_ram") ))
 * - CFG_TUSB_MEM_ALIGN   : __attribute__ ((aligned(4)))
 */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

// Endpoint 0 size
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

//------------- CLASS -------------//
#define CFG_TUD_HID               3
#define CFG_TUD_MSC               1
#define CFG_TUD_CDC               1
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

// HID buffer size Should be sufficient to hold ID (if any) + Data
#define CFG_TUD_HID_EP_BUFSIZE    64

// // MSC Buffer size
// #define CFG_TUD_MSC_EP_BUFSIZE    64

// MSC Buffer size of Device Mass storage
#define CFG_TUD_MSC_EP_BUFSIZE   512



// CDC FIFO size of TX and RX.
// 64 bytes at full-speed is easy to saturate with bursts of printf/logs and can lead to
// partial/discarded output when the stdio backend hits its stdout timeout.
#define CFG_TUD_CDC_RX_BUFSIZE   512
#define CFG_TUD_CDC_TX_BUFSIZE   512

#define CFG_TUH_ENUMERATION_BUFSIZE 512 // Permanent TinyUSB scratch for ordinary configuration descriptors.
#define ERGOTYPE_TUH_ENUMERATION_MAX_BUFSIZE 4096 // Larger configurations use one exact-size transient host-owner buffer.

// Keep one ordinary TinyUSB host-event FIFO. The default is 16; 32 retains the
// same event payload capacity as the removed 16-entry queue plus 16-entry spill.
#define CFG_TUH_TASK_QUEUE_SZ     32

#ifndef CFG_TUH_MEM_SECTION
/* Keep DMA-visible host transfer buffers below the core-1 stack in scratch X. */
#define CFG_TUH_MEM_SECTION      __attribute__((section(".scratch_x.tinyusb_host")))
#endif

#ifndef CFG_TUH_MEM_ALIGN
#define CFG_TUH_MEM_ALIGN        __attribute__ ((aligned(4)))
#endif

#define CFG_TUH_HUB              1  // USB hub class support. 1 costs one host hub/device slot plus hub state; 0 saves roughly 100-200 B but external hubs stop working.
#define CFG_TUH_DEVICE_MAX       4  // Max directly managed non-hub USB devices. Direct ErgoType-to-ErgoType needs 1; exact endpoint callbacks make each slot larger.
#define CFG_TUH_API_EDPT_XFER    1  // Preserve result/user_data for direct interrupt IN/OUT. Costs 1280 B with 5 host slots and 16 endpoint numbers.
#define CFG_TUH_HID              4  // Max HID interfaces, not report IDs. Peer ErgoType NKRO needs 3: keyboard/consumer, mouse, WebHID. Each extra costs HID state plus IN/OUT buffers.
#define CFG_TUH_HID_EPIN_BUFSIZE 1  // Direct endpoint IN owns its request buffer; retain a one-byte class placeholder and save 240 B of aligned staging.
#define CFG_TUH_HID_EPOUT_BUFSIZE 1  // Direct endpoint OUT owns its request buffer; retain a one-byte class placeholder and save 240 B of unused staging.


#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
