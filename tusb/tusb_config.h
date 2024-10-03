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

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

// defined by compiler flags for flexibility
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE | BOARD_TUD_MAX_SPEED)

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
#define CFG_TUD_HID               1
#define CFG_TUD_MSC               1
#define CFG_TUD_CDC               1
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

// HID buffer size Should be sufficient to hold ID (if any) + Data
#define CFG_TUD_HID_EP_BUFSIZE    16

// // MSC Buffer size
// #define CFG_TUD_MSC_EP_BUFSIZE    64

// MSC Buffer size of Device Mass storage
#define CFG_TUD_MSC_EP_BUFSIZE   512



// CDC FIFO size of TX and RX
#define CFG_TUD_CDC_RX_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)
#define CFG_TUD_CDC_TX_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)


#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
