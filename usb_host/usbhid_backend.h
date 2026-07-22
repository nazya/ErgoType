#ifndef USB_HOST_USBHID_BACKEND_H
#define USB_HOST_USBHID_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include "host/usbh_pvt.h"

struct hid_device;

/* Fixed callback-side reason; lifecycle owns the corresponding text logger. */
enum usbhid_async_invariant {
	USBHID_ASYNC_INVARIANT_NONE,
	USBHID_ASYNC_INVARIANT_XFER_TUPLE,
	USBHID_ASYNC_INVARIANT_HUB_PIN,
};

/*
 * Boundary between TinyUSB host callbacks and the Linux-style USB HID
 * transport. Callback entry points may only copy bounded ingress data, rotate
 * lifecycle state, and wake a task. They may briefly acquire the shared
 * transport mutex, whose holders never wait for TinyUSB progress; they must
 * not allocate, run Linux policy, wait for USB/lifecycle progress, or log.
 */
void usbhid_backend_hid_mount(uint8_t dev_addr, uint8_t instance,
			      const uint8_t *desc_report, uint16_t desc_len);
void usbhid_backend_hid_umount(uint8_t dev_addr, uint8_t instance);
void usbhid_backend_device_mount(uint8_t dev_addr);
void usbhid_backend_device_umount(uint8_t dev_addr);
/* Idle duplicate ATTACH waits until the old Linux/cache topology is retired. */
bool usbhid_backend_native_replace_begin(uint8_t rhport, uint8_t hub_addr,
					 uint8_t hub_port);
/* Host-owner ATTACH admission: no allocation, only bounded cache state. */
bool usbhid_backend_cache_available(void);
/* Transport fault ingress; lifecycle task performs the actual logging. */
void usbhid_backend_rx_report_dropped(void);
void usbhid_backend_rx_rearm_failed(void);
void usbhid_backend_rx_transfer_failed(uint8_t xfer_result);
void usbhid_backend_rx_invariant_failed(void);
void usbhid_backend_async_invariant_failed(
	enum usbhid_async_invariant reason);
/* Report recovery publishes work; the lifecycle task owns reset/re-enumeration. */
int usbhid_backend_queue_device_reset(struct hid_device *hid,
				      uint32_t report_revision,
				      bool reset_work_running);
/* Cancel only this interface's queued, not-yet-running reset_work ticket. */
bool usbhid_backend_cancel_device_reset(struct hid_device *hid);
/* Host-owner handoff matching TinyUSB hub.c's reset-to-attach callback. */
void usbhid_backend_hub_reset_host_complete(uint8_t hub_addr,
					    uint8_t hub_port,
					    void *context, int status);
/* Exact generation fence for SHA-pinned direct host-owner re-enumeration. */
bool usbhid_backend_hub_reenumerate_begin(uint8_t rhport,
					  uint8_t hub_addr,
					  uint8_t hub_port);
/* Exact host-enumeration progress/terminal fence for reset gate ownership. */
void usbhid_backend_enum_state(uint8_t rhport, uint8_t hub_addr,
			       uint8_t hub_port, bool active, bool success);
/* Physical enum owner released while a retained ATTACH remains retryable. */
void usbhid_backend_enum_parked(uint8_t rhport, uint8_t hub_addr,
				uint8_t hub_port);
/* Gated EP0 progress is a wake edge; reset state remains lifecycle-owned. */
void usbhid_backend_control_gate_idle(void);
/* TinyUSB's one physical control owner became idle in the host task. */
void usbhid_backend_host_control_idle(void);
/* TinyUSB released one exact non-control endpoint in the host task. */
void usbhid_backend_host_endpoint_idle(uint8_t dev_addr, uint8_t ep_addr);
usbh_class_driver_t const *usbhid_backend_app_driver_get(
	uint8_t *driver_count);

#endif
