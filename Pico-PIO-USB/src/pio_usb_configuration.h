
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
#define PIO_USB_DEVICE_CNT 4 // PIO-USB device records. One direct host device needs 1; 4->1 saves roughly 250-300 B, but hub children/multiple devices stop fitting.
#define PIO_USB_HUB_PORT_CNT 8 // Child-device slots inside each pio_usb_device record. Low direct cost, but only useful with hub support.
#define PIO_USB_ROOT_PORT_CNT 2 // Physical PIO root ports. One D+/D- pair needs 1; 2->1 saves roughly one root_port_t, about 50 B.

#define PIO_USB_EP_SIZE 64 // Max USB endpoint packet payload used to size endpoint_t encoded buffers. Lowering saves per endpoint but breaks normal full-speed 64-byte packets; do not reduce for HID host.
