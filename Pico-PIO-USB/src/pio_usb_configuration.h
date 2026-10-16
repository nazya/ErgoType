
#pragma once

typedef enum {
  PIO_USB_PINOUT_DPDM = 0,  // DM = DP+1
  PIO_USB_PINOUT_DMDP,      // DM = DP-1
} PIO_USB_PINOUT;

typedef struct {
    uint8_t pin_dp;
    uint8_t pio_tx_num;
    uint8_t sm_tx;
    uint8_t tx_ch;
    uint8_t pio_rx_num;
    uint8_t sm_rx;
    uint8_t sm_eop;
    void* alarm_pool;
    int8_t debug_pin_rx;
    int8_t debug_pin_eop;
    bool skip_alarm_pool;
    PIO_USB_PINOUT pinout;
} pio_usb_configuration_t;

#ifndef PIO_USB_DP_PIN_DEFAULT
#define PIO_USB_DP_PIN_DEFAULT 0
#endif

#define PIO_USB_TX_DEFAULT 0
#define PIO_SM_USB_TX_DEFAULT 0
#define PIO_USB_DMA_TX_DEFAULT 0

#define PIO_USB_RX_DEFAULT 0
#define PIO_SM_USB_RX_DEFAULT 1
#define PIO_SM_USB_EOP_DEFAULT 2

#define PIO_USB_DEBUG_PIN_NONE (-1)

#define PIO_USB_DEFAULT_CONFIG                                             \
  {                                                                        \
    PIO_USB_DP_PIN_DEFAULT, PIO_USB_TX_DEFAULT, PIO_SM_USB_TX_DEFAULT,     \
        PIO_USB_DMA_TX_DEFAULT, PIO_USB_RX_DEFAULT, PIO_SM_USB_RX_DEFAULT, \
        PIO_SM_USB_EOP_DEFAULT, NULL, PIO_USB_DEBUG_PIN_NONE,              \
        PIO_USB_DEBUG_PIN_NONE, false, PIO_USB_PINOUT_DPDM                 \
  }

#define PIO_USB_EP_POOL_CNT 8 // Pico-PIO-USB default is 32. Max low-level USB endpoints opened by connected devices. Too low means a composite device enumerates but some interfaces/endpoints, for example mouse or WebHID, do not start. 32->8 saves about 4.5 KB.
#define PIO_USB_DEV_EP_CNT 16 // Endpoint-id table entries inside each pio_usb_device record. Low memory impact: one byte per entry per PIO_USB_DEVICE_CNT.
#define PIO_USB_DEVICE_CNT 4 // PIO-USB device records (76 B each). The TinyUSB PIO HCD overrides the weak IRQ handler that scans them and does not use records 1..3 for hub children; 4->1 would save 228 B but changes shared host/device storage and needs an audit and hardware regression.
#define PIO_USB_HUB_PORT_CNT 8 // Size of child_devices[] inside each pio_usb_device record. Nothing reads or writes it, so it does not limit TinyUSB hub support.
#define PIO_USB_ROOT_PORT_CNT 1 // Physical PIO root ports. One D+/D- pair needs 1; 2->1 saves roughly one root_port_t, about 50 B.

#define PIO_USB_EP_SIZE 64 // Sizes only the temporary TX packet buffer in pio_usb.c; endpoint_t::buffer is fixed for 64-byte payloads in usb_definitions.h. Lowering it saves no endpoint memory and breaks normal full-speed 64-byte packets; do not reduce for HID host.
