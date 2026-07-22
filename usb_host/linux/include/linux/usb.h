#ifndef USB_HOST_LINUX_USB_H
#define USB_HOST_LINUX_USB_H

#include <ctype.h>

#include "asm/byteorder.h"
#include "hid_compat.h"

// Upstream Linux USB core is not ported; this header keeps only the USB identity,
// control, string, and status contracts imported HID drivers use.
#define USB_INTERFACE_SUBCLASS_BOOT HID_SUBCLASS_BOOT
#define USB_INTERFACE_PROTOCOL_KEYBOARD HID_ITF_PROTOCOL_KEYBOARD
#define USB_INTERFACE_PROTOCOL_MOUSE HID_ITF_PROTOCOL_MOUSE
#define USB_WIRELESS_STATUS_CONNECTED 1
#define USB_WIRELESS_STATUS_DISCONNECTED 0
#define USB_DIR_OUT 0
#define USB_DIR_IN 0x80
#define USB_TYPE_STANDARD 0
#define USB_TYPE_CLASS 0x20
#define USB_TYPE_VENDOR 0x40
#define USB_RECIP_DEVICE 0
#define USB_RECIP_INTERFACE 0x01
#define USB_RECIP_ENDPOINT 0x02
/* Upstream ch9.h owns this mask; the compact port has no separate ch9 shim. */
#define USB_RECIP_MASK 0x1f
#define USB_REQ_GET_STATUS TUSB_REQ_GET_STATUS
#define USB_REQ_CLEAR_FEATURE 0x01
#define USB_ENDPOINT_HALT 0	/* IN/OUT will STALL */
#define USB_REQ_GET_DESCRIPTOR TUSB_REQ_GET_DESCRIPTOR
#define USB_STATUS_TYPE_STANDARD USB_TYPE_STANDARD
#define USB_STATUS_TYPE_PTM 1
#define USB_DT_STRING TUSB_DESC_STRING
/* Linux pipe encoding is distinct from endpoint bmAttributes values. */
#define PIPE_ISOCHRONOUS 0
#define PIPE_INTERRUPT 1
#define PIPE_CONTROL 2
#define PIPE_BULK 3
#define USB_CTRL_GET_TIMEOUT 5000
#define USB_CTRL_SET_TIMEOUT 5000
#define USB_MAX_SYNCHRONOUS_TIMEOUT 60000
/*
 * Linux USB core exposes every endpoint in the active altsetting. TinyUSB has
 * already opened the HID transport before this compact shim is built, so the
 * port retains only the first interrupt IN and first interrupt OUT selected by
 * the same rule as upstream usbhid_start(). No linked HID caller consumes bulk
 * or duplicate interrupt endpoints from this snapshot.
 */
#define USB_HOST_ENDPOINT_MAX 2
#define USB_DT_ENDPOINT TUSB_DESC_ENDPOINT
#define USB_ENDPOINT_XFER_INT TUSB_XFER_INTERRUPT
#define USB_ENDPOINT_DIR_MASK 0x80
#define USB_ENDPOINT_XFER_MASK 0x03

struct usb_device_descriptor {
	__le16 idVendor;
	__le16 idProduct;
	__le16 bcdDevice;
	u8 bMaxPacketSize0;
	u8 iManufacturer;
	u8 iProduct;
	u8 iSerialNumber;
};

struct usb_config_descriptor {
	u8 bNumInterfaces;
};

struct usb_host_config {
	struct usb_config_descriptor desc;
};

struct usb_device {
	struct device dev;
	struct usb_device *parent;
	struct usb_device_descriptor descriptor;
	// struct usb_host_config *config;
	// struct usb_host_config *actconfig;
	// Firmware supports one active TinyUSB configuration and stores only the
	// descriptor fields imported HID drivers currently inspect.
	struct usb_host_config config_storage;
	struct usb_host_config *config;
	struct usb_host_config *actconfig;
	char *product;
	char *manufacturer;
	char *serial;
	char product_buf[64];
	char manufacturer_buf[64];
	char serial_buf[64];
	unsigned have_langid:1;
	int string_langid;
	u8 dev_addr;
	u8 rhport;
	u8 hub_addr;
	u8 hub_port;
	u8 speed;
	u8 portnum;
	int maxchild;
	bool topology_valid;
};

struct usb_interface_descriptor {
	u8 bInterfaceNumber;
	u8 bInterfaceSubClass;
	u8 bInterfaceProtocol;
	u8 bNumEndpoints;
};

struct usb_endpoint_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bEndpointAddress;
	u8 bmAttributes;
	__le16 wMaxPacketSize;
	u8 bInterval;
};

struct usb_host_endpoint {
	struct usb_endpoint_descriptor desc;
};

struct usb_host_interface {
	struct usb_interface_descriptor desc;
	// struct usb_host_endpoint *endpoint;
	// Firmware stores a small fixed endpoint snapshot in the local
	// usb_interface shim instead of allocating Linux USB core altsettings.
	struct usb_host_endpoint endpoint[USB_HOST_ENDPOINT_MAX];
	bool has_interrupt_in;
	u8 interrupt_in_endpoint;
	bool has_interrupt_out;
	u8 interrupt_out_endpoint;
};

struct usb_interface {
	struct device dev;
	struct usb_host_interface *altsetting;
	struct usb_host_interface *cur_altsetting;
	int wireless_status;
};

extern const struct device_type usb_device_type;
extern const struct device_type usb_if_device_type;

static inline struct usb_interface *to_usb_interface(struct device *dev)
{
	return container_of(dev, struct usb_interface, dev);
}

static inline struct usb_device *to_usb_device(struct device *dev)
{
	return container_of(dev, struct usb_device, dev);
}

static inline struct usb_device *interface_to_usbdev(struct usb_interface *iface)
{
	return to_usb_device(iface->dev.parent);
}

static inline int usb_make_path(struct usb_device *dev, char *buf, size_t size)
{
	int actual;

	// actual = snprintf(buf, size, "usb-%s-%s", dev->bus->bus_name, dev->devpath);
	// TinyUSB port uses root-port/hub-port identity instead of Linux bus/devpath strings.
	if (dev->topology_valid && dev->hub_addr)
		actual = snprintf(buf, size, "usb-%u-%u.%u",
				  dev->rhport, dev->hub_addr, dev->hub_port);
	else if (dev->topology_valid)
		actual = snprintf(buf, size, "usb-%u-%u",
				  dev->rhport, dev->hub_port);
	else
		actual = snprintf(buf, size, "usb-%u", dev->dev_addr);
	return (actual >= (int)size) ? -1 : actual;
}

static inline void usb_set_intfdata(struct usb_interface *intf, void *data)
{
	dev_set_drvdata(&intf->dev, data);
}

static inline void *usb_get_intfdata(struct usb_interface *intf)
{
	return dev_get_drvdata(&intf->dev);
}

// extern struct usb_interface *usb_ifnum_to_if(const struct usb_device *dev,
// 					       unsigned ifnum);
// Firmware interface shims are HID-owned rather than configuration-owned;
// cross-interface users must take the scoped usbhid transport lease.
struct usb_device *usb_hub_find_child(struct usb_device *hdev, int port1);

/**
 * usb_hub_for_each_child - iterate over all child devices on the hub
 * @hdev:  USB device belonging to the usb hub
 * @port1: portnum associated with child device
 * @child: child device pointer
 */
#define usb_hub_for_each_child(hdev, port1, child) \
	for (port1 = 1,	child =	usb_hub_find_child(hdev, port1); \
			port1 <= hdev->maxchild; \
			child = usb_hub_find_child(hdev, ++port1)) \
		if (!child) continue; else

static inline int usb_set_wireless_status(struct usb_interface *iface, int status)
{
	// Linux USB core exposes this through sysfs; the port keeps only in-memory state.
	iface->wireless_status = status;
	return 0;
}

static inline unsigned int __create_pipe(struct usb_device *dev,
		unsigned int endpoint)
{
	// return (dev->devnum << 8) | (endpoint << 15);
	// TinyUSB device address is stored as dev_addr in this port.
	return ((unsigned int)dev->dev_addr << 8) | (endpoint << 15);
}

#define usb_pipein(pipe)	((pipe) & USB_DIR_IN)
#define usb_pipeout(pipe)	(!usb_pipein(pipe))
#define usb_pipedevice(pipe)	(((pipe) >> 8) & 0x7f)
#define usb_pipeendpoint(pipe)	(((pipe) >> 15) & 0x0f)
#define usb_pipetype(pipe)	(((pipe) >> 30) & 3)
#define usb_pipeint(pipe)	(usb_pipetype((pipe)) == PIPE_INTERRUPT)
#define usb_pipecontrol(pipe)	(usb_pipetype((pipe)) == PIPE_CONTROL)
#define usb_sndctrlpipe(dev, endpoint)	\
	((PIPE_CONTROL << 30) | __create_pipe(dev, endpoint))
#define usb_rcvctrlpipe(dev, endpoint)	\
	((PIPE_CONTROL << 30) | __create_pipe(dev, endpoint) | USB_DIR_IN)
#define usb_sndintpipe(dev, endpoint)	\
	((PIPE_INTERRUPT << 30) | __create_pipe(dev, endpoint))
#define usb_rcvintpipe(dev, endpoint)	\
	((PIPE_INTERRUPT << 30) | __create_pipe(dev, endpoint) | USB_DIR_IN)

static inline bool usb_endpoint_xfer_int(const struct usb_endpoint_descriptor *epd)
{
	return (epd->bmAttributes & USB_ENDPOINT_XFER_MASK) == USB_ENDPOINT_XFER_INT;
}

static inline bool usb_check_int_endpoints(const struct usb_interface *intf,
					   const u8 *ep_addrs)
{
	const struct usb_host_interface *alt = intf->cur_altsetting;

	for (size_t i = 0; ep_addrs[i]; i++) {
		bool found = false;

		for (u8 j = 0; j < alt->desc.bNumEndpoints; j++) {
			const struct usb_endpoint_descriptor *desc = &alt->endpoint[j].desc;

			if (desc->bEndpointAddress == ep_addrs[i] &&
			    usb_endpoint_xfer_int(desc)) {
				found = true;
				break;
			}
		}

		if (!found)
			return false;
	}

	return true;
}

#if 0
/*
 * Deferred: URB ownership needs request-specific kill/resubmit semantics; it
 * must not use urb->context as a HID owner or reuse the synchronous bridge.
 */
struct usb_ctrlrequest {
	u8 bRequestType;
	u8 bRequest;
	__le16 wValue;
	__le16 wIndex;
	__le16 wLength;
} __packed;

struct urb;
typedef void (*usb_complete_t)(struct urb *);

struct urb {
	struct usb_device *dev;
	unsigned int pipe;
	int status;
	void *transfer_buffer;
	u32 transfer_buffer_length;
	u32 actual_length;
	unsigned char *setup_packet;
	void *context;
	usb_complete_t complete;
};

struct urb *usb_alloc_urb(int iso_packets, gfp_t mem_flags);
void usb_free_urb(struct urb *urb);
int usb_submit_urb(struct urb *urb, gfp_t mem_flags);
void usb_kill_urb(struct urb *urb);

static inline void usb_fill_control_urb(struct urb *urb,
					struct usb_device *dev,
					unsigned int pipe,
					unsigned char *setup_packet,
					void *transfer_buffer,
					int buffer_length,
					usb_complete_t complete_fn,
					void *context)
{
	urb->dev = dev;
	urb->pipe = pipe;
	urb->setup_packet = setup_packet;
	urb->transfer_buffer = transfer_buffer;
	urb->transfer_buffer_length = buffer_length;
	urb->complete = complete_fn;
	urb->context = context;
}
#endif

/*
 * Linux USB core normally blocks the task caller here. TinyUSB callbacks stay
 * nonblocking; the port keeps this contract over its fixed async slot owner.
 */
extern int usb_control_msg(struct usb_device *dev, unsigned int pipe,
	__u8 request, __u8 requesttype, __u16 value, __u16 index,
	void *data, __u16 size, int timeout);
extern int usb_interrupt_msg(struct usb_device *usb_dev, unsigned int pipe,
	void *data, int len, int *actual_length, int timeout);

/*
 * Upstream implements these wrappers in drivers/usb/core/message.c. The
 * compact port keeps the same bodies inline because no USB core library owns
 * them here.
 */
static inline int usb_control_msg_send(struct usb_device *dev, u8 endpoint,
				       u8 request, u8 requesttype,
				       u16 value, u16 index,
				       const void *driver_data,
				       u16 size, int timeout, gfp_t memflags)
{
	unsigned int pipe = usb_sndctrlpipe(dev, endpoint);
	int ret;
	u8 *data = NULL;

	if (size) {
		data = kmemdup(driver_data, size, memflags);
		if (!data)
			return -ENOMEM;
	}

	ret = usb_control_msg(dev, pipe, request, requesttype, value, index,
			      data, size, timeout);
	kfree(data);
	if (ret < 0)
		return ret;

	return 0;
}

static inline int usb_control_msg_recv(struct usb_device *dev, u8 endpoint,
				       u8 request, u8 requesttype,
				       u16 value, u16 index, void *driver_data,
				       u16 size, int timeout, gfp_t memflags)
{
	unsigned int pipe = usb_rcvctrlpipe(dev, endpoint);
	int ret;
	u8 *data;

	if (!size || !driver_data)
		return -EINVAL;

	data = kmalloc(size, memflags);
	if (!data)
		return -ENOMEM;

	ret = usb_control_msg(dev, pipe, request, requesttype, value, index,
			      data, size, timeout);
	if (ret < 0)
		goto exit;

	if (ret == size) {
		memcpy(driver_data, data, size);
		ret = 0;
	} else {
		ret = -EREMOTEIO;
	}

exit:
	kfree(data);
	return ret;
}

#if 0
/*
 * Deferred: enumeration already caches descriptors and strings asynchronously;
 * expose these allocation-heavy USB-core helpers only with a linked consumer.
 */
static inline int usb_get_descriptor(struct usb_device *dev, unsigned char desctype,
				     unsigned char descindex, void *buf, int size)
{
	int i;
	int ret;

	if (size <= 0)
		return -EINVAL;

	memset(buf, 0, (size_t)size);

	for (i = 0; i < 3; i++) {
		ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
				      USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
				      ((u16)desctype << 8) | descindex, 0,
				      buf, (u16)size, USB_CTRL_GET_TIMEOUT);
		if (ret <= 0 && ret != -ETIMEDOUT)
			continue;
		if (ret > 1 && ((u8 *)buf)[1] != desctype) {
			ret = -ENODATA;
			continue;
		}
		break;
	}

	return ret;
}

static inline int usb_get_string(struct usb_device *dev, unsigned short langid,
					 unsigned char index, void *buf, int size)
{
	int i;
	int result;

	if (size <= 0)
		return -EINVAL;

	for (i = 0; i < 3; i++) {
		result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
					 USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
					 (USB_DT_STRING << 8) + index, langid,
					 buf, (u16)size, USB_CTRL_GET_TIMEOUT);
		if (result == 0 || result == -EPIPE)
			continue;
		if (result > 1 && ((u8 *)buf)[1] != USB_DT_STRING) {
			result = -ENODATA;
			continue;
		}
		break;
	}
	return result;
}

static inline void usb_try_string_workarounds(unsigned char *buf, int *length)
{
	int newlength, oldlength = *length;

	for (newlength = 2; newlength + 1 < oldlength; newlength += 2)
		if (!isprint((unsigned char)buf[newlength]) || buf[newlength + 1])
			break;

	if (newlength > 2) {
		buf[0] = newlength;
		*length = newlength;
	}
}

static inline int usb_string_sub(struct usb_device *dev, unsigned int langid,
					unsigned int index, unsigned char *buf)
{
	int rc;

	// if (dev->quirks & USB_QUIRK_STRING_FETCH_255)
	// 	rc = -EIO;
	// else
	// Local usb_device has no USB core quirk table; use the upstream default max fetch.
	rc = usb_get_string(dev, langid, index, buf, 255);

	if (rc < 2) {
		rc = usb_get_string(dev, langid, index, buf, 2);
		if (rc == 2)
			rc = usb_get_string(dev, langid, index, buf, buf[0]);
	}

	if (rc >= 2) {
		if (!buf[0] && !buf[1])
			usb_try_string_workarounds(buf, &rc);
		if (buf[0] < rc)
			rc = buf[0];
		rc = rc - (rc & 1);
	}

	if (rc < 2)
		rc = (rc < 0 ? rc : -EINVAL);

	return rc;
}

static inline int usb_string_decode(const u8 *desc, int actual, char *buf, size_t size)
{
	size_t out = 0;
	u8 len;

	if (!size)
		return -EINVAL;

	buf[0] = '\0';

	if (actual < 2)
		return -EINVAL;

	len = desc[0];
	if (desc[1] != USB_DT_STRING)
		return -EINVAL;
	if (len > actual)
		len = (u8)actual;

	for (size_t i = 2; i + 1u < len; i += 2u) {
		u16 c = (u16)desc[i] | ((u16)desc[i + 1u] << 8);

		if (c >= 0xd800u && c <= 0xdfffu)
			c = '?';

		if (c < 0x80u) {
			if (out + 1u >= size)
				break;
			buf[out++] = (char)c;
		} else if (c < 0x800u) {
			if (out + 2u >= size)
				break;
			buf[out++] = (char)(0xc0u | (c >> 6));
			buf[out++] = (char)(0x80u | (c & 0x3fu));
		} else {
			if (out + 3u >= size)
				break;
			buf[out++] = (char)(0xe0u | (c >> 12));
			buf[out++] = (char)(0x80u | ((c >> 6) & 0x3fu));
			buf[out++] = (char)(0x80u | (c & 0x3fu));
		}
	}

	buf[out] = '\0';
	return (int)out;
}

static inline int usb_get_langid(struct usb_device *dev, unsigned char *tbuf)
{
	int err;

	if (dev->have_langid)
		return 0;

	if (dev->string_langid < 0)
		return -EPIPE;

	err = usb_string_sub(dev, 0, 0, tbuf);
	if (err == -ENODATA || (err > 0 && err < 4)) {
		dev->string_langid = 0x0409;
		dev->have_langid = 1;
		dev_err(&dev->dev,
			"language id specifier not provided by device, defaulting to English\n");
		return 0;
	}

	if (err < 0) {
		dev_info(&dev->dev, "string descriptor 0 read error: %d\n", err);
		dev->string_langid = -1;
		return -EPIPE;
	}

	dev->string_langid = tbuf[2] | (tbuf[3] << 8);
	dev->have_langid = 1;
	dev_dbg(&dev->dev, "default language 0x%04x\n", dev->string_langid);
	return 0;
}

static inline int usb_string(struct usb_device *dev, int index, char *buf, size_t size)
{
	u8 tbuf[256];
	int err;

	if (!size || !buf)
		return -EINVAL;

	buf[0] = 0;
	if (index <= 0 || index >= 256)
		return -EINVAL;

	err = usb_get_langid(dev, tbuf);
	if (err < 0)
		return err;

	err = usb_string_sub(dev, dev->string_langid, index, tbuf);
	if (err < 0)
		return err;

	err = usb_string_decode(tbuf, err, buf, size);
	if (err >= 0 && tbuf[1] != USB_DT_STRING)
		dev_dbg(&dev->dev,
			"wrong descriptor type %02x for string %d (\"%s\")\n",
			tbuf[1], index, buf);
	return err;
}

static inline int usb_get_status(struct usb_device *dev,
				 int recip, int type, int target, void *data)
{
	u8 *status;
	int length;
	int ret;

	switch (type) {
	case USB_STATUS_TYPE_STANDARD:
		length = 2;
		break;
	case USB_STATUS_TYPE_PTM:
		if (recip != USB_RECIP_DEVICE)
			return -EINVAL;

		length = 4;
		break;
	default:
		return -EINVAL;
	}

	status = kmalloc((size_t)length, GFP_KERNEL);
	if (!status)
		return -ENOMEM;

        ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
                              USB_REQ_GET_STATUS, USB_DIR_IN | recip,
                              (u16)type, (u16)target, status,
                              (u16)length, USB_CTRL_GET_TIMEOUT);

	switch (ret) {
	case 4:
		if (type != USB_STATUS_TYPE_PTM) {
			ret = -EIO;
			break;
		}

		*(u32 *)data = (u32)status[0] | ((u32)status[1] << 8) |
			       ((u32)status[2] << 16) | ((u32)status[3] << 24);
		ret = 0;
		break;
	case 2:
		if (type != USB_STATUS_TYPE_STANDARD) {
			ret = -EIO;
			break;
		}

		*(u16 *)data = (u16)status[0] | ((u16)status[1] << 8);
		ret = 0;
		break;
	default:
		ret = -EIO;
	}

	kfree(status);
	return ret;
}

static inline int usb_get_std_status(struct usb_device *dev,
	int recip, int target, void *data)
{
	return usb_get_status(dev, recip, USB_STATUS_TYPE_STANDARD, target,
		data);
}
#endif

#endif
