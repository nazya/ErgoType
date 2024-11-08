 /*
 * ErgoType - Keyboard Solutions
 *
 * © 2024 Nazarii Tupitsa (see also: LICENSE-ErgoType).
 */
#include "tusb.h"
#include "usb_descriptors.h"
#include <string.h>

// USB Vendor ID and Product IDs
#define USB_VID        0xCafe
#define USB_PID_HID    0x1001  // Example PID for HID mode
#define USB_PID_CDC_MSC 0x1002 // Example PID for CDC + MSC mode
#define USB_BCD        0x0200

// Mode selection variable from main.c
extern uint32_t mode;

// Define MSC subclass and protocol constants
#define MSC_SUBCLASS_SCSI 0x06
#define MSC_PROTOCOL_BOT  0x50 // Bulk-Only Transport

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+

// Device descriptor for HID mode
tusb_desc_device_t const desc_device_hid =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,

    // Class, subclass, protocol
    .bDeviceClass       = TUSB_CLASS_HID,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID_HID, // PID for HID

    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

// Device descriptor for CDC + MSC mode
tusb_desc_device_t const desc_device_cdc_msc =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,

    // Use Interface Association Descriptor (IAD) for CDC
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID_CDC_MSC, // PID for CDC + MSC

    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

//--------------------------------------------------------------------+
// HID Report Descriptor
//--------------------------------------------------------------------+

// HID Report Descriptor for Keyboard
uint8_t const desc_hid_report[] =
{
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD))
};

//--------------------------------------------------------------------+
// Configuration Descriptors
//--------------------------------------------------------------------+

// Enumeration of Interface Numbers for CDC + MSC mode
enum
{
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MSC,
    ITF_NUM_TOTAL_CDC_MSC
};

// Enumeration of Interface Numbers for HID mode
enum
{
    ITF_NUM_HID = 0,
    ITF_NUM_TOTAL_HID
};

// Endpoint Numbers
#define EPNUM_HID          0x81
#define EPNUM_CDC_NOTIF    0x81
#define EPNUM_CDC_OUT      0x02
#define EPNUM_CDC_IN       0x82
#define EPNUM_MSC_OUT      0x03
#define EPNUM_MSC_IN       0x83

// Configuration Lengths
#define CONFIG_TOTAL_LEN_HID     (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define CONFIG_TOTAL_LEN_CDC_MSC (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)

// Configuration Descriptor for HID mode
uint8_t const desc_configuration_hid[] =
{
    // Config number, interface count, string index, total length, attributes, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_HID, 0, CONFIG_TOTAL_LEN_HID, 0x80, 100),

    // Interface number, string index, protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_report), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 10)
};

// Configuration Descriptor for CDC + MSC mode
uint8_t const desc_configuration_cdc_msc[] =
{
    // Config number, interface count, string index, total length, attributes, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_CDC_MSC, 0, CONFIG_TOTAL_LEN_CDC_MSC, 0x80, 100),

    // CDC Interface
    // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    // MSC Interface
    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 5, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

#if TUD_OPT_HIGH_SPEED
// High-Speed Configuration for CDC + MSC mode
uint8_t const desc_hs_configuration_cdc_msc[] =
{
    // Config number, interface count, string index, total length, attributes, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_CDC_MSC, 0, CONFIG_TOTAL_LEN_CDC_MSC, 0x80, 100),

    // CDC Interface
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 512),

    // MSC Interface
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 5, EPNUM_MSC_OUT, EPNUM_MSC_IN, 512),
};

// Device Qualifier Descriptor
tusb_desc_device_qualifier_t const desc_device_qualifier =
{
    .bLength            = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType    = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB             = USB_BCD,

    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved          = 0x00
};

// Other Speed Configuration Descriptor Callback
uint8_t const* tud_descriptor_other_speed_configuration_cb(uint8_t index)
{
    (void) index; // for multiple configurations

    static uint8_t desc_other_speed_config[CONFIG_TOTAL_LEN_CDC_MSC];

    // if link speed is high return fullspeed config, and vice versa
    // Note: the descriptor type is OTHER_SPEED_CONFIG instead of CONFIG
    memcpy(desc_other_speed_config,
           (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_configuration_cdc_msc : desc_hs_configuration_cdc_msc,
           CONFIG_TOTAL_LEN_CDC_MSC);

    desc_other_speed_config[1] = TUSB_DESC_OTHER_SPEED_CONFIG;

    return desc_other_speed_config;
}

// Device Qualifier Descriptor Callback
uint8_t const* tud_descriptor_device_qualifier_cb(void)
{
    return (uint8_t const*) &desc_device_qualifier;
}
#endif // TUD_OPT_HIGH_SPEED

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// String Descriptor Index
enum
{
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC_IF,
    STRID_MSC_IF,
    STRID_HID_IF
};

// Array of pointer to string descriptors
char const* string_desc_arr [] =
{
    (const char[]) { 0x09, 0x04 }, // 0: Language ID (0x0409 for English)
    "ErgoType",                      // 1: Manufacturer
    "HID/CDC+MSC Device",          // 2: Product
    "1",                           // 3: Serial Number
    "TinyUSB CDC",                 // 4: CDC Interface
    "TinyUSB MSC",                 // 5: MSC Interface
    "TinyUSB HID"                  // 6: HID Interface
};

static uint16_t _desc_str[32 + 1];

// String Descriptor Callback
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void) langid;

    uint8_t chr_count;

    if (index == 0)
    {
        // Language ID
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    }
    else
    {
        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) return NULL;

        const char* str = string_desc_arr[index];

        // Convert ASCII string into UTF-16
        chr_count = strlen(str);
        if (chr_count > 31) chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++)
        {
            _desc_str[1 + i] = str[i];
        }
    }

    // First byte is length (including header), second byte is string type
    _desc_str[0] = (TUSB_DESC_STRING << 8 ) | (2 * chr_count + 2);

    return _desc_str;
}

//--------------------------------------------------------------------+
// Descriptor Callback Functions
//--------------------------------------------------------------------+

// Device Descriptor Callback
uint8_t const * tud_descriptor_device_cb(void)
{
    if (mode == 0)
    {
        // HID Mode
        return (uint8_t const *) &desc_device_hid;
    }
    else
    {
        // CDC + MSC Mode
        return (uint8_t const *) &desc_device_cdc_msc;
    }
}

// Configuration Descriptor Callback
uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
    if (mode == 0)
    {
        // HID Mode
        return desc_configuration_hid;
    }
    else
    {
        // CDC + MSC Mode
#if TUD_OPT_HIGH_SPEED
        return (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_hs_configuration_cdc_msc : desc_configuration_cdc_msc;
#else
        return desc_configuration_cdc_msc;
#endif
    }
}

// HID Report Descriptor Callback (Only relevant in HID mode)
uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void) instance;
    if (mode == 0)
    {
        return desc_hid_report;
    }
    else
    {
        return NULL; // No HID report in CDC + MSC mode
    }
}
