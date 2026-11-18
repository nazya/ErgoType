
#pragma once

#include "pio_usb_configuration.h"
#include "usb_definitions.h"

#ifdef __cplusplus
 extern "C" {
#endif

// Host functions
usb_device_t *pio_usb_host_init(const pio_usb_configuration_t *c);
int pio_usb_host_add_port(uint8_t pin_dp, PIO_USB_PINOUT pinout);
void pio_usb_host_task(void);
void pio_usb_host_stop(void);
void pio_usb_host_restart(void);
uint32_t pio_usb_host_get_frame_number(void);
/* Host-owner PM extension; suspended remains true through resume recovery. */
bool pio_usb_host_port_suspend(uint8_t root_idx, bool remote_wakeup);
bool pio_usb_host_port_resume(uint8_t root_idx);
bool pio_usb_host_port_is_suspended(uint8_t root_idx);
bool pio_usb_host_port_resume_failed(uint8_t root_idx);
void pio_usb_host_port_pm_changed_cb(uint8_t root_idx, bool remote_wakeup);

/* Host-owner helper: reset only the local data toggle after remote clear-halt. */
bool pio_usb_host_endpoint_reset_data_toggle(uint8_t root_idx,
                                             uint8_t device_address,
                                             uint8_t ep_address);

// Call this every 1ms when skip_alarm_pool is true.
void pio_usb_host_frame(void);

// Device functions
usb_device_t *pio_usb_device_init(const pio_usb_configuration_t *c,
                                  const usb_descriptor_buffers_t *buffers);
void pio_usb_device_task(void);

// Common functions
endpoint_t *pio_usb_get_endpoint(usb_device_t *device, uint8_t idx);
int pio_usb_get_in_data(endpoint_t *ep, uint8_t *buffer, uint8_t len);
int pio_usb_set_out_data(endpoint_t *ep, const uint8_t *buffer, uint8_t len);

#ifdef __cplusplus
 }
#endif
