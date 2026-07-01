/*
 * ErgoType - Keyboard Solutions
 *
 * © 2024 Nazarii Tupitsa (see also: LICENSE-ErgoType).
 */
#include "tusb.h"
#include "usb_descriptors.h"
#include <stdint.h>
#include <string.h>

#define USB_DEVICE_DESCRIPTOR(product_id) { \
    .bLength            = sizeof(tusb_desc_device_t), \
    .bDescriptorType    = TUSB_DESC_DEVICE, \
    .bcdUSB             = USB_BCD, \
    .bDeviceClass       = TUSB_CLASS_MISC, \
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON, \
    .bDeviceProtocol    = MISC_PROTOCOL_IAD, \
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE, \
    .idVendor           = USB_VID, \
    .idProduct          = (product_id), \
    .bcdDevice          = USB_DEVICE_BCD, \
    .iManufacturer      = USB_STRID_MANUFACTURER, \
    .iProduct           = USB_STRID_PRODUCT, \
    .iSerialNumber      = USB_STRID_SERIAL, \
    .bNumConfigurations = 0x01, \
}

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+

static tusb_desc_device_t const desc_device_hid_cdc_boot_kb_mouse =
    USB_DEVICE_DESCRIPTOR(USB_PID_HID);

static tusb_desc_device_t const desc_device_hid_cdc_nkro =
    USB_DEVICE_DESCRIPTOR(USB_PID_HID_NKRO);

static tusb_desc_device_t const desc_device_cdc_msc =
    USB_DEVICE_DESCRIPTOR(USB_PID_CDC_MSC);

//--------------------------------------------------------------------+
// HID Report Descriptor
//--------------------------------------------------------------------+

static uint8_t const desc_hid_report_boot_keyboard[] =
{
    TUD_HID_REPORT_DESC_KEYBOARD(),
};

static uint8_t const desc_hid_report_mouse[] =
{
    TUD_HID_REPORT_DESC_MOUSE(),
};

static uint8_t const desc_hid_report_nkro[] =
{
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_KEYBOARD),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_REPORT_ID(REPORT_ID_KEYBOARD) // Same HID interface also has consumer report ID 2.
    HID_LOGICAL_MIN(0), // Boolean fields use 0 for released/off.
    HID_LOGICAL_MAX(1), // Boolean fields use 1 for pressed/on.

    HID_USAGE_PAGE(HID_USAGE_PAGE_LED),
    HID_USAGE_MIN(1),    // LED Page usage 1 starts the standard keyboard LED range.
    HID_USAGE_MAX(5),    // LED usages 1..5 are Num Lock, Caps Lock, Scroll Lock, Compose, Kana.
    HID_REPORT_COUNT(5), // One bit for each LED usage in that range.
    HID_REPORT_SIZE(1),  // LED states are packed as single bits.
    HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), // Host writes one independent bit per LED.
    HID_REPORT_COUNT(1), // One padding field after the five LED bits.
    HID_REPORT_SIZE(3),  // 5 LED bits + 3 padding bits = one output byte.
    HID_OUTPUT(HID_CONSTANT | HID_VARIABLE | HID_ABSOLUTE), // Padding bits are ignored by the device.

    HID_USAGE_PAGE(HID_USAGE_PAGE_KEYBOARD),
    HID_USAGE_MIN(0),         // Start at 0 so bitmap bit index equals HID usage.
    HID_USAGE_MAX_N(0xFF, 2), // Full 8-bit Keyboard Page range, including modifiers 0xE0..0xE7.
    HID_REPORT_SIZE(1),       // One bit per keyboard usage.
    HID_REPORT_COUNT_N(HID_NKRO_KEY_BYTES * 8, 2), // Keep descriptor count tied to nkro_keys[] size.
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), // Bitmap input: bit set means usage is pressed.
    HID_COLLECTION_END,

    HID_USAGE_PAGE(HID_USAGE_PAGE_CONSUMER),
    HID_USAGE(HID_USAGE_CONSUMER_CONTROL),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_REPORT_ID(REPORT_ID_CONSUMER) // Separate report ID because this is not Keyboard Page data.
    HID_LOGICAL_MIN(0),       // 0 is the empty array slot.
    HID_LOGICAL_MAX_N(0x03FF, 2), // TinyUSB consumer range; covers current max usage 0x02A2.
    HID_USAGE_MIN(0),         // 0 is allowed so unused array entries can be zero.
    HID_USAGE_MAX_N(0x03FF, 2), // Keep declared usage range equal to the accepted array value range.
    HID_REPORT_SIZE(16),      // send_consumer_report() sends uint16_t usage values.
    HID_REPORT_COUNT(6),      // Matches keys[6]: up to six simultaneous consumer usages.
    HID_INPUT(HID_DATA | HID_ARRAY | HID_ABSOLUTE), // Array input: each slot contains one usage ID.
    HID_COLLECTION_END,
};

static uint8_t const desc_hid_report_webhid[] =
{
    HID_USAGE_PAGE_N(0xFF00, 2),
    HID_USAGE(0x20),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_REPORT_ID(REPORT_ID_WEBHID_CONFIG)
    HID_REPORT_SIZE(8),
    HID_REPORT_COUNT(WEBHID_REPORT_SIZE),
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,
};

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

enum
{
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC_IF,
    STRID_MSC_IF,
    STRID_HID_IF,
    STRID_WEBHID_IF,
};

static char const* const string_desc_arr[] =
{
    (const char[]) { 0x09, 0x04 },
    "ErgoType",
    "USB Device",
    "1",
    "TinyUSB CDC",
    "TinyUSB MSC",
    "TinyUSB HID",
    "ErgoType WebHID",
};

static uint16_t _desc_str[32 + 1];

//--------------------------------------------------------------------+
// Configuration Descriptors
//--------------------------------------------------------------------+

enum
{
    ITF_BOOT_CDC = 0,
    ITF_BOOT_CDC_DATA,
    ITF_BOOT_KEYBOARD,
    ITF_BOOT_MOUSE,
    ITF_BOOT_TOTAL,
};

enum
{
    ITF_NKRO_CDC = 0,
    ITF_NKRO_CDC_DATA,
    ITF_NKRO_KEYBOARD,
    ITF_NKRO_MOUSE,
    ITF_NKRO_WEBHID,
    ITF_NKRO_TOTAL,
};

enum
{
    ITF_STORAGE_CDC = 0,
    ITF_STORAGE_CDC_DATA,
    ITF_STORAGE_MSC,
    ITF_STORAGE_TOTAL,
};

#define EP_CDC_NOTIF     0x81
#define EP_CDC_OUT       0x02
#define EP_CDC_IN        0x82
#define EP_HID_IN        0x83
#define EP_HID_MOUSE_IN  0x84
#define EP_HID_WEBHID_IN 0x85
#define EP_MSC_OUT       0x03
#define EP_MSC_IN        0x83

#define CONFIG_TOTAL_LEN_HID_CDC_NKRO (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_DESC_LEN)
#define CONFIG_TOTAL_LEN_HID_CDC_BOOT_KB_MOUSE (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_DESC_LEN)
#define CONFIG_TOTAL_LEN_CDC_MSC (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)

static uint8_t const desc_configuration_hid_cdc_boot_kb_mouse[] =
{
    TUD_CONFIG_DESCRIPTOR(USB_CONFIG_NUMBER, ITF_BOOT_TOTAL, USB_CONFIG_STRIDX, CONFIG_TOTAL_LEN_HID_CDC_BOOT_KB_MOUSE, USB_CONFIG_ATTR, USB_CONFIG_POWER_MA),
    TUD_CDC_DESCRIPTOR(ITF_BOOT_CDC, STRID_CDC_IF, EP_CDC_NOTIF, USB_CDC_NOTIF_EP_SIZE, EP_CDC_OUT, EP_CDC_IN, USB_CDC_BULK_MPS_FS),
    TUD_HID_DESCRIPTOR(ITF_BOOT_KEYBOARD, STRID_HID_IF, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_report_boot_keyboard), EP_HID_IN, CFG_TUD_HID_EP_BUFSIZE, USB_HID_POLL_INTERVAL_MS),
    TUD_HID_DESCRIPTOR(ITF_BOOT_MOUSE, STRID_HID_IF, HID_ITF_PROTOCOL_MOUSE, sizeof(desc_hid_report_mouse), EP_HID_MOUSE_IN, CFG_TUD_HID_EP_BUFSIZE, USB_HID_POLL_INTERVAL_MS),
};

static uint8_t const desc_configuration_hid_cdc_nkro[] =
{
    TUD_CONFIG_DESCRIPTOR(USB_CONFIG_NUMBER, ITF_NKRO_TOTAL, USB_CONFIG_STRIDX, CONFIG_TOTAL_LEN_HID_CDC_NKRO, USB_CONFIG_ATTR, USB_CONFIG_POWER_MA),
    TUD_CDC_DESCRIPTOR(ITF_NKRO_CDC, STRID_CDC_IF, EP_CDC_NOTIF, USB_CDC_NOTIF_EP_SIZE, EP_CDC_OUT, EP_CDC_IN, USB_CDC_BULK_MPS_FS),
    TUD_HID_DESCRIPTOR(ITF_NKRO_KEYBOARD, STRID_HID_IF, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report_nkro), EP_HID_IN, CFG_TUD_HID_EP_BUFSIZE, USB_HID_POLL_INTERVAL_MS),
    TUD_HID_DESCRIPTOR(ITF_NKRO_MOUSE, STRID_HID_IF, HID_ITF_PROTOCOL_MOUSE, sizeof(desc_hid_report_mouse), EP_HID_MOUSE_IN, CFG_TUD_HID_EP_BUFSIZE, USB_HID_POLL_INTERVAL_MS),
    TUD_HID_DESCRIPTOR(ITF_NKRO_WEBHID, STRID_WEBHID_IF, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report_webhid), EP_HID_WEBHID_IN, CFG_TUD_HID_EP_BUFSIZE, USB_HID_POLL_INTERVAL_MS),
};

static uint8_t const desc_configuration_cdc_msc[] =
{
    TUD_CONFIG_DESCRIPTOR(USB_CONFIG_NUMBER, ITF_STORAGE_TOTAL, USB_CONFIG_STRIDX, CONFIG_TOTAL_LEN_CDC_MSC, USB_CONFIG_ATTR, USB_CONFIG_POWER_MA),
    TUD_CDC_DESCRIPTOR(ITF_STORAGE_CDC, STRID_CDC_IF, EP_CDC_NOTIF, USB_CDC_NOTIF_EP_SIZE, EP_CDC_OUT, EP_CDC_IN, USB_CDC_BULK_MPS_FS),
    TUD_MSC_DESCRIPTOR(ITF_STORAGE_MSC, STRID_MSC_IF, EP_MSC_OUT, EP_MSC_IN, USB_MSC_MPS_FS),
};

//--------------------------------------------------------------------+
// Descriptor Callback Functions
//--------------------------------------------------------------------+

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void) langid;

    uint8_t chr_count;

    if (index == 0)
    {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    }
    else
    {
        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) return NULL;

        char const* str = string_desc_arr[index];
        chr_count = strlen(str);
        if (chr_count > 31) chr_count = 31;

        for (uint8_t i = 0; i < chr_count; ++i)
        {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
    return _desc_str;
}

uint8_t const* tud_descriptor_device_cb(void)
{
    if (mode != HID)
        return (uint8_t const*) &desc_device_cdc_msc;

    return (uint8_t const*) (
        hid_output_profile == HID_OUTPUT_PROFILE_NKRO_KB_MOUSE
        ? &desc_device_hid_cdc_nkro
        : &desc_device_hid_cdc_boot_kb_mouse
    );
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index)
{
    (void) index;

    if (mode == HID)
        return (hid_output_profile == HID_OUTPUT_PROFILE_NKRO_KB_MOUSE) ? desc_configuration_hid_cdc_nkro : desc_configuration_hid_cdc_boot_kb_mouse;

    return desc_configuration_cdc_msc;
}

uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance)
{
    if (mode != HID)
        return NULL;

    if (hid_output_profile == HID_OUTPUT_PROFILE_NKRO_KB_MOUSE && instance == HID_WEBHID_INSTANCE)
        return desc_hid_report_webhid;

    if (hid_output_profile == HID_OUTPUT_PROFILE_NKRO_KB_MOUSE)
        return (instance == HID_KEYBOARD_INSTANCE) ? desc_hid_report_nkro : desc_hid_report_mouse;

    return (instance == HID_KEYBOARD_INSTANCE) ? desc_hid_report_boot_keyboard : desc_hid_report_mouse;
}
