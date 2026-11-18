/* Firmware-only public lifecycle API for the TinyUSB-to-Linux HID adapter. */
#ifndef USB_HOST_USBHID_H
#define USB_HOST_USBHID_H

#include <stdbool.h>

struct hid_device;
struct work_struct;
struct usb_device;

enum usbhid_pm_state {
	USBHID_PM_ACTIVE,
	USBHID_PM_SUSPENDED,
};

typedef void (*usbhid_pm_complete_t)(enum usbhid_pm_state state, int status,
				      void *context);

int usbhid_lifecycle_init(void);
void usbhid_lifecycle_task(void *pvParameters);
void usbhid_lifecycle_schedule_work(struct work_struct *work);
void usbhid_lifecycle_cancel_work(struct work_struct *work);
struct hid_device *usbhid_lifecycle_find_hid(const struct usb_device *dev,
					      unsigned int ifnum);
struct hid_device *usbhid_ifnum_io_get(const struct usb_device *dev,
					       unsigned int ifnum);
void usbhid_ifnum_io_put(struct hid_device *hid);
/*
 * The request is copied synchronously. An accepted request completes exactly
 * once on the HID lifecycle task; context remains caller-owned until then.
 */
int usbhid_pm_request(enum usbhid_pm_state state,
		      usbhid_pm_complete_t complete, void *context);
/* Latest desired state, retained even before host initialization; never waits
 * for a transition. A completed transition invokes the callback in lifecycle
 * context. Overwritten targets do not receive individual completions. */
void usbhid_pm_set_target(enum usbhid_pm_state state, bool remote_wakeup);
void usbhid_pm_state_changed_cb(enum usbhid_pm_state state, int status);
/* Lifecycle context; a device's wake request, not a resume result. */
void usbhid_pm_wakeup_cb(void);

#endif
