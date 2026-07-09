#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "tusb.h"
#include "host/usbh_pvt.h"

#include "hid_async.h"
#include "stdio_tusb_cdc.h"
#include "linux/include/linux/hid.h"
#include "linux/include/linux/usb.h"

/*
 * TinyUSB-to-Linux USB HID transport glue. Upstream Linux keeps this shape in
 * drivers/hid/usbhid/hid-core.c; this port uses TinyUSB as the USB backend.
 */

#define HID_HOST_MAX_DEVICES CFG_TUH_HID
#define HID_HOST_RAW_INTERFACE_MAX HID_HOST_MAX_DEVICES

static const struct hid_ll_driver usb_hid_driver;
static struct hid_device *usbhid_devices[HID_HOST_MAX_DEVICES];

struct usbhid_raw_interface {
	bool valid;
	uint8_t dev_addr;
	uint8_t ifnum;
	uint8_t subclass;
	uint8_t protocol;
};

static struct usbhid_raw_interface usbhid_raw_interfaces[HID_HOST_RAW_INTERFACE_MAX];

static bool usbhid_raw_interface_init(void)
{
	memset(usbhid_raw_interfaces, 0, sizeof(usbhid_raw_interfaces));
	return true;
}

static bool usbhid_raw_interface_deinit(void)
{
	memset(usbhid_raw_interfaces, 0, sizeof(usbhid_raw_interfaces));
	return true;
}

static void usbhid_raw_interface_store(uint8_t dev_addr,
				       tusb_desc_interface_t const *desc)
{
	struct usbhid_raw_interface *free_slot = NULL;

	for (size_t i = 0; i < HID_HOST_RAW_INTERFACE_MAX; i++) {
		struct usbhid_raw_interface *raw = &usbhid_raw_interfaces[i];

		if (!raw->valid) {
			if (!free_slot)
				free_slot = raw;
			continue;
		}

		if (raw->dev_addr == dev_addr &&
		    raw->ifnum == desc->bInterfaceNumber) {
			free_slot = raw;
			break;
		}
	}

	if (!free_slot)
		return;

	free_slot->valid = true;
	free_slot->dev_addr = dev_addr;
	free_slot->ifnum = desc->bInterfaceNumber;
	free_slot->subclass = desc->bInterfaceSubClass;
	free_slot->protocol = desc->bInterfaceProtocol;
}

static const struct usbhid_raw_interface *
usbhid_raw_interface_lookup(uint8_t dev_addr, uint8_t ifnum)
{
	for (size_t i = 0; i < HID_HOST_RAW_INTERFACE_MAX; i++) {
		const struct usbhid_raw_interface *raw = &usbhid_raw_interfaces[i];

		if (raw->valid && raw->dev_addr == dev_addr && raw->ifnum == ifnum)
			return raw;
	}

	return NULL;
}

static void usbhid_raw_interface_close(uint8_t dev_addr)
{
	for (size_t i = 0; i < HID_HOST_RAW_INTERFACE_MAX; i++) {
		struct usbhid_raw_interface *raw = &usbhid_raw_interfaces[i];

		if (raw->valid && raw->dev_addr == dev_addr)
			raw->valid = false;
	}
}

static bool usbhid_raw_interface_open(uint8_t rhport, uint8_t dev_addr,
				      tusb_desc_interface_t const *desc,
				      uint16_t max_len)
{
	(void)rhport;
	(void)max_len;

	if (desc->bInterfaceClass == TUSB_CLASS_HID)
		usbhid_raw_interface_store(dev_addr, desc);

	/*
	 * Upstream Linux: no equivalent; Linux USB core passes the original
	 * usb_interface to usbhid_probe(). TinyUSB normalizes HID protocol in
	 * its HID class driver, so this app driver snapshots raw interface
	 * fields first and returns false to leave normal TinyUSB HID open active.
	 */
	return false;
}

static const usbh_class_driver_t usbhid_raw_interface_driver[] = {
	{
		.name = "HID raw interface snapshot",
		.init = usbhid_raw_interface_init,
		.deinit = usbhid_raw_interface_deinit,
		.open = usbhid_raw_interface_open,
		.close = usbhid_raw_interface_close,
	},
};

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count)
{
	*driver_count = TU_ARRAY_SIZE(usbhid_raw_interface_driver);
	return usbhid_raw_interface_driver;
}

static struct hid_device *usbhid_lookup(uint8_t dev_addr, uint8_t instance)
{
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];

		if (hid && hid->dev_addr == dev_addr && hid->instance == instance)
			return hid;
	}

	return NULL;
}

static int usbhid_insert(struct hid_device *hid)
{
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		if (!usbhid_devices[i]) {
			usbhid_devices[i] = hid;
			return 0;
		}
	}

	return -ENOMEM;
}

static void usbhid_remove_slot(struct hid_device *hid)
{
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		if (usbhid_devices[i] == hid) {
			usbhid_devices[i] = NULL;
			return;
		}
	}
}

struct usb_interface *usb_ifnum_to_if(const struct usb_device *dev, unsigned int ifnum)
{
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];

		if (hid && hid->usb_dev.dev_addr == dev->dev_addr &&
		    hid->usb_intf.cur_altsetting &&
		    hid->usb_intf.cur_altsetting->desc.bInterfaceNumber == ifnum)
			return &hid->usb_intf;
	}

	return NULL;
}

// static int usbhid_probe(struct usb_interface *intf, const struct usb_device_id *id)
// TinyUSB mount callback provides dev_addr/instance/report descriptor instead
// of Linux usb_interface; build the local usb_interface shim before hid_add_device().
static int usbhid_probe(uint8_t dev_addr, uint8_t instance,
			uint8_t const *desc_report, uint16_t desc_len)
{
	struct hid_device *hid;
	uint8_t *rdesc;
	const struct usbhid_raw_interface *raw;
	uint16_t vid = 0;
	uint16_t pid = 0;
	tuh_itf_info_t itf_info;
	int ret;

	if (!desc_report || !desc_len) {
		async_msg("ERR: HID_DESC_MISSING");
		return -ENODEV;
	}

	hid = hid_allocate_device();
	if (IS_ERR(hid)) {
		async_msg("ERR: HID_ALLOC_FAIL");
		return PTR_ERR(hid);
	}

	// ret = hid_get_class_descriptor(dev, interface->desc.bInterfaceNumber,
	//				  HID_DT_REPORT, rdesc, rsize);
	// TinyUSB passes the report descriptor into the mount callback.
	rdesc = kmemdup(desc_report, desc_len, GFP_KERNEL);
	if (!rdesc) {
		hid_destroy_device(hid);
		async_msg("ERR: HID_DESC_ALLOC_FAIL");
		return -ENOMEM;
	}

	memset(&itf_info, 0, sizeof(itf_info));
	tuh_vid_pid_get(dev_addr, &vid, &pid);
	tuh_hid_itf_get_info(dev_addr, instance, &itf_info);

	device_initialize(&hid->usb_dev.dev);
	device_initialize(&hid->usb_intf.dev);

	// hid->dev.parent = &intf->dev;
	// TinyUSB mount callback builds a local usb_interface shim for this HID instance.
	hid->dev_addr = dev_addr;
	hid->instance = instance;
	hid->usb_dev.dev_addr = dev_addr;
	hid->usb_dev.descriptor.idVendor = vid;
	hid->usb_dev.descriptor.idProduct = pid;

	hid->usb_altsetting.desc.bInterfaceNumber = itf_info.desc.bInterfaceNumber;
	raw = usbhid_raw_interface_lookup(dev_addr, itf_info.desc.bInterfaceNumber);
	if (raw) {
		hid->usb_altsetting.desc.bInterfaceSubClass = raw->subclass;
		hid->usb_altsetting.desc.bInterfaceProtocol = raw->protocol;
	} else {
		hid->usb_altsetting.desc.bInterfaceSubClass = itf_info.desc.bInterfaceSubClass;
		hid->usb_altsetting.desc.bInterfaceProtocol = itf_info.desc.bInterfaceProtocol;
	}
	hid->usb_altsetting.desc.bNumEndpoints = itf_info.desc.bNumEndpoints;
	hid->usb_intf.altsetting = &hid->usb_altsetting;
	hid->usb_intf.cur_altsetting = &hid->usb_altsetting;
	hid->usb_intf.dev.parent = &hid->usb_dev.dev;
	hid->dev.parent = &hid->usb_intf.dev;
	usb_set_intfdata(&hid->usb_intf, hid);

	hid->ll_driver = &usb_hid_driver;
	hid->ll_rdesc = rdesc;
	hid->ll_rsize = desc_len;
	init_waitqueue_head(&hid->ll_wait);

	hid->bus = BUS_USB;
	hid->vendor = vid;
	hid->product = pid;
	hid->version = 0;
	// if (intf->cur_altsetting->desc.bInterfaceProtocol ==
	//		USB_INTERFACE_PROTOCOL_MOUSE)
	//	hid->type = HID_TYPE_USBMOUSE;
	// else if (intf->cur_altsetting->desc.bInterfaceProtocol == 0)
	//	hid->type = HID_TYPE_USBNONE;
	// Port has no Linux usb_interface from USB core; use the raw interface
	// descriptor snapshot captured before TinyUSB normalizes HID protocol.
	if (hid->usb_altsetting.desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_MOUSE)
		hid->type = HID_TYPE_USBMOUSE;
	else if (hid->usb_altsetting.desc.bInterfaceProtocol == 0)
		hid->type = HID_TYPE_USBNONE;

	snprintf(hid->name, sizeof(hid->name), "HID %04x:%04x", hid->vendor, hid->product);
	usb_make_path(&hid->usb_dev, hid->phys, sizeof(hid->phys));
	strlcat(hid->phys, "/input", sizeof(hid->phys));

	ret = usbhid_insert(hid);
	if (ret < 0) {
		async_msg("ERR: HID_TABLE_FULL");
		goto fail;
	}

	ret = hid_add_device(hid);
	if (ret < 0) {
		usbhid_remove_slot(hid);
		async_msg("ERR: HID_ADD_FAIL");
		goto fail;
	}

	bool receive_ok = tuh_hid_receive_report(dev_addr, instance);
	if (!receive_ok)
		async_msg("ERR: HID_RX_START_FAIL");
	hid->ll_rdesc = NULL;
	hid->ll_rsize = 0;
	kfree(rdesc);
	return 0;

fail:
	kfree(rdesc);
	hid_destroy_device(hid);
	return ret;
}

// static void usbhid_disconnect(struct usb_interface *intf)
// TinyUSB unmount callback identifies the HID interface by dev_addr + instance.
static void usbhid_disconnect(uint8_t dev_addr, uint8_t instance)
{
	struct hid_device *hid = usbhid_lookup(dev_addr, instance);

	if (hid) {
		int ret = hid_async_cancel_device(dev_addr, instance);

		if (ret)
			async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
		usbhid_remove_slot(hid);
		hid_destroy_device(hid);
	}
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
		      uint8_t const *desc_report, uint16_t desc_len)
{
	usbhid_probe(dev_addr, instance, desc_report, desc_len);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
	usbhid_disconnect(dev_addr, instance);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
				uint8_t const *report, uint16_t len)
{
	struct hid_device *hid = usbhid_lookup(dev_addr, instance);
	uint8_t protocol_mode = tuh_hid_get_protocol(dev_addr, instance);
	int ret = -ENODEV;

	if (protocol_mode == HID_PROTOCOL_BOOT) {
		async_msg("WARN: HID_PROTOCOL_BOOT");
	} else if (hid) {
		uint8_t report_buf[CFG_TUH_HID_EPIN_BUFSIZE];

		// ret = hid_safe_input_report(hid, HID_INPUT_REPORT, (u8 *)report,
		//			       CFG_TUH_HID_EPIN_BUFSIZE, len, 1);
		/*
		 * hid_safe_input_report() may zero-pad short reports in-place. TinyUSB
		 * gives us a const callback buffer, so give the Linux parser a writable
		 * transport-sized copy without allocating in the callback.
		 */
		memset(report_buf, 0, sizeof(report_buf));
		memcpy(report_buf, report, len);
		ret = hid_safe_input_report(hid, HID_INPUT_REPORT, report_buf,
					    sizeof(report_buf), len, 1);
		if (ret < 0)
			async_msg("ERR: HID_REPORT_SKIP");
	} else {
		async_msg("ERR: HID_REPORT_SKIP");
	}

	bool receive_ok = tuh_hid_receive_report(dev_addr, instance);
	if (!receive_ok)
		async_msg("ERR: HID_RX_REARM_FAIL");
}

static int usbhid_start(struct hid_device *hid)
{
	/*
	 * Upstream usbhid start may arm polling, reset LEDs, and enable wakeup.
	 * Current callback slice arms interrupt IN from TinyUSB mount/report
	 * callbacks and leaves control/output side effects disabled.
	 */
	(void)hid;
	return 0;
}

static void usbhid_stop(struct hid_device *hid)
{
	/*
	 * Upstream usbhid stop cancels URBs and queued delayed work. Current slice
	 * has no transport queues/work items to drain here.
	 */
	(void)hid;
}

static int usbhid_open(struct hid_device *hid)
{
	/*
	 * Temporary callback-driven slice: TinyUSB interrupt IN is already armed
	 * from mount/report callbacks. Do not start a second transport path here.
	 */
	(void)hid;
	return 0;
}

static void usbhid_close(struct hid_device *hid)
{
	/*
	 * Upstream close kills interrupt IN unless ALWAYS_POLL is active. Current
	 * slice keeps receive ownership in TinyUSB callbacks.
	 */
	(void)hid;
}

static int usbhid_parse(struct hid_device *hid)
{
	struct usb_interface *intf = to_usb_interface(hid->dev.parent);
	struct usb_host_interface *interface = intf->cur_altsetting;
	unsigned long quirks = hid->quirks;
	unsigned long transport_quirks = 0;
	int ret;

	if (!hid->ll_rdesc || !hid->ll_rsize)
		return -ENODEV;

	if (interface->desc.bInterfaceSubClass == USB_INTERFACE_SUBCLASS_BOOT) {
		if (interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_KEYBOARD ||
		    interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_MOUSE) {
			/*
			 * Keep upstream usbhid_parse() behavior for boot keyboards/mice:
			 * do not GET_REPORT during init. In this slice all hardware
			 * request paths are disabled anyway, but keeping the quirk matters
			 * for later async request work.
			 */
			quirks |= HID_QUIRK_NOGET;
			transport_quirks |= HID_QUIRK_NOGET;
		}
	}

	// ret = hid_get_class_descriptor(dev, interface->desc.bInterfaceNumber,
	//				  HID_DT_REPORT, rdesc, rsize);
	// TinyUSB supplies the report descriptor to tuh_hid_mount_cb(); this
	// ll_driver consumes it here so hid_add_device() keeps upstream parse flow.
	ret = hid_parse_report(hid, hid->ll_rdesc, hid->ll_rsize);
	if (ret)
		return ret;

	// hid->quirks |= quirks;
	// hid_device_probe() recomputes quirks from initial_quirks after parse();
	// keep transport-added usbhid_parse() quirks alive.
	hid->initial_quirks |= transport_quirks;
	hid->quirks |= quirks;
	return 0;
}

static void usbhid_request(struct hid_device *hid, struct hid_report *report,
			   enum hid_class_request reqtype)
{
	int ret;

	// usbhid_submit_report(hid, report, reqtype);
	// Port queues the same best-effort hid_hw_request() path into the HID async
	// control task; completion callbacks do not block TinyUSB callback context.
	ret = hid_async_queue_report(hid, report, reqtype, NULL, NULL);
	if (ret)
		async_msg("ERR: HID_ASYNC_REQ_FAIL");
}

static int usbhid_wait_io(struct hid_device *hid)
{
	/*
	 * usbhid_wait_io(hid);
	 * Do not block TinyUSB callbacks. This slice keeps parser/input behavior
	 * active while sync hardware wait semantics are deferred.
	 */
	(void)hid;
	return 0;
}

static int usbhid_raw_request(struct hid_device *hid, unsigned char reportnum,
			      __u8 *buf, size_t len, unsigned char rtype,
			      int reqtype)
{
	int ret;

	if (reqtype == HID_REQ_SET_REPORT) {
		// return usbhid_set_raw_report(hid, reportnum, buf, len, rtype);
		// Queue raw SET_REPORT through the HID async task. This returns after
		// enqueue, not after USB completion; callers needing completion data
		// still need an explicit async state machine.
		ret = hid_async_queue_raw_set_report(hid, reportnum, rtype, buf, len,
						     NULL, NULL);
		if (ret)
			return ret;
		return (int)len;
	}

	/*
	 * Upstream raw GET report is backed by synchronous USB control transfer.
	 * Current slice has no generic async continuation for return data yet.
	 */
	if (reqtype == HID_REQ_GET_REPORT)
		return -ENOSYS;

	return -ENOSYS;
}

static int usbhid_output_report(struct hid_device *hid, __u8 *buf, size_t len)
{
	int ret;

	// ret = usb_interrupt_msg(dev, usbhid->urbout->pipe, buf, count,
	//			   &actual_length, USB_CTRL_SET_TIMEOUT);
	// TinyUSB interrupt OUT is queued through the HID async task; the report
	// sent callback only wakes that task and never blocks TinyUSB callbacks.
	ret = hid_async_queue_output_report(hid, buf, len, NULL, NULL);
	if (ret)
		return ret;

	return (int)len;
}

static int usbhid_idle(struct hid_device *hid, int report, int idle, int reqtype)
{
	/*
	 * return hid_set_idle(dev, ifnum, report, idle);
	 * SET_IDLE is a synchronous control request; keep it deferred with the
	 * rest of the hardware request path.
	 */
	(void)hid;
	(void)report;
	(void)idle;
	(void)reqtype;
	return 0;
}

int tuh_usb_control_msg(struct usb_device *dev, unsigned int pipe,
			u8 request, u8 requesttype, u16 value, u16 index,
			void *data, u16 size, int timeout)
{
	/*
	 * Synchronous EP0 control is intentionally disabled in the callback-driven
	 * parser slice. Any caller reaching this must be converted to async later.
	 */
	(void)dev;
	(void)pipe;
	(void)request;
	(void)requesttype;
	(void)value;
	(void)index;
	(void)data;
	(void)size;
	(void)timeout;
	return -ENOSYS;
}

static const struct hid_ll_driver usb_hid_driver = {
	.parse = usbhid_parse,
	.start = usbhid_start,
	.stop = usbhid_stop,
	.open = usbhid_open,
	.close = usbhid_close,
	.request = usbhid_request,
	.wait = usbhid_wait_io,
	.raw_request = usbhid_raw_request,
	.output_report = usbhid_output_report,
	.idle = usbhid_idle,
	.max_buffer_size = HID_MAX_BUFFER_SIZE,
};
