/*
 * ErgoType - Keyboard Solutions
 *
 * © 2024 Nazarii Tupitsa (see also: LICENSE-ErgoType).
 */
#include "tusb.h"
#include "usb_descriptors.h"
#include <stdint.h>
#include <string.h>

#define USB_VID         0xCafe
#define USB_PID_HID     0x1001
#define USB_PID_CDC_MSC 0x1002
#define USB_BCD         0x0200

extern uint8_t mode;

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+

static tusb_desc_device_t const desc_device_hid_cdc =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID_HID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

static tusb_desc_device_t const desc_device_cdc_msc =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID_CDC_MSC,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

//--------------------------------------------------------------------+
// HID Report Descriptor
//--------------------------------------------------------------------+

static uint8_t const desc_hid_report[] =
{
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE)),
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
};

static char const* const string_desc_arr[] =
{
    (const char[]) { 0x09, 0x04 },
    "ErgoType",
    "ErgoType USB Device",
    "1",
    "TinyUSB CDC",
    "TinyUSB MSC",
    "TinyUSB HID",
};

static uint16_t _desc_str[32 + 1];

//--------------------------------------------------------------------+
// Configuration Descriptors
//--------------------------------------------------------------------+

enum
{
    ITF_NUM_HID_CDC_CDC = 0,
    ITF_NUM_HID_CDC_CDC_DATA,
    ITF_NUM_HID_CDC_HID,
    ITF_NUM_TOTAL_HID_CDC,
};

enum
{
    ITF_NUM_CDC_MSC_CDC = 0,
    ITF_NUM_CDC_MSC_CDC_DATA,
    ITF_NUM_CDC_MSC_MSC,
    ITF_NUM_TOTAL_CDC_MSC,
};

#define EPNUM_HID_CDC_NOTIF 0x81
#define EPNUM_HID_CDC_OUT   0x02
#define EPNUM_HID_CDC_IN    0x82
#define EPNUM_HID_CDC_HID   0x83

#define EPNUM_CDC_MSC_NOTIF 0x81
#define EPNUM_CDC_MSC_OUT   0x02
#define EPNUM_CDC_MSC_IN    0x82
#define EPNUM_MSC_OUT       0x03
#define EPNUM_MSC_IN        0x83

#define CONFIG_TOTAL_LEN_HID_CDC (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)
#define CONFIG_TOTAL_LEN_CDC_MSC (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)
#define CONFIG_TOTAL_LEN_MAX     ((CONFIG_TOTAL_LEN_HID_CDC > CONFIG_TOTAL_LEN_CDC_MSC) ? CONFIG_TOTAL_LEN_HID_CDC : CONFIG_TOTAL_LEN_CDC_MSC)

static uint8_t const desc_configuration_hid_cdc[] =
{
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_HID_CDC, 0, CONFIG_TOTAL_LEN_HID_CDC, 0x80, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_HID_CDC_CDC, STRID_CDC_IF, EPNUM_HID_CDC_NOTIF, 8, EPNUM_HID_CDC_OUT, EPNUM_HID_CDC_IN, 64),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_CDC_HID, STRID_HID_IF, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_report), EPNUM_HID_CDC_HID, CFG_TUD_HID_EP_BUFSIZE, 10),
};

static uint8_t const desc_configuration_cdc_msc[] =
{
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_CDC_MSC, 0, CONFIG_TOTAL_LEN_CDC_MSC, 0x80, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_MSC_CDC, STRID_CDC_IF, EPNUM_CDC_MSC_NOTIF, 8, EPNUM_CDC_MSC_OUT, EPNUM_CDC_MSC_IN, 64),
    TUD_MSC_DESCRIPTOR(ITF_NUM_CDC_MSC_MSC, STRID_MSC_IF, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

#if TUD_OPT_HIGH_SPEED
static uint8_t const desc_hs_configuration_hid_cdc[] =
{
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_HID_CDC, 0, CONFIG_TOTAL_LEN_HID_CDC, 0x80, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_HID_CDC_CDC, STRID_CDC_IF, EPNUM_HID_CDC_NOTIF, 8, EPNUM_HID_CDC_OUT, EPNUM_HID_CDC_IN, 512),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_CDC_HID, STRID_HID_IF, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_report), EPNUM_HID_CDC_HID, CFG_TUD_HID_EP_BUFSIZE, 10),
};

static uint8_t const desc_hs_configuration_cdc_msc[] =
{
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_CDC_MSC, 0, CONFIG_TOTAL_LEN_CDC_MSC, 0x80, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_MSC_CDC, STRID_CDC_IF, EPNUM_CDC_MSC_NOTIF, 8, EPNUM_CDC_MSC_OUT, EPNUM_CDC_MSC_IN, 512),
    TUD_MSC_DESCRIPTOR(ITF_NUM_CDC_MSC_MSC, STRID_MSC_IF, EPNUM_MSC_OUT, EPNUM_MSC_IN, 512),
};

static tusb_desc_device_qualifier_t const desc_device_qualifier =
{
    .bLength            = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType    = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved          = 0x00,
};
#endif

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
    return (uint8_t const*) ((mode == HID) ? &desc_device_hid_cdc : &desc_device_cdc_msc);
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index)
{
    (void) index;

    if (mode == HID)
    {
#if TUD_OPT_HIGH_SPEED
        return (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_hs_configuration_hid_cdc : desc_configuration_hid_cdc;
#else
        return desc_configuration_hid_cdc;
#endif
    }

#if TUD_OPT_HIGH_SPEED
    return (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_hs_configuration_cdc_msc : desc_configuration_cdc_msc;
#else
    return desc_configuration_cdc_msc;
#endif
}

uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void) instance;
    return (mode == HID) ? desc_hid_report : NULL;
}

#if TUD_OPT_HIGH_SPEED
uint8_t const* tud_descriptor_other_speed_configuration_cb(uint8_t index)
{
    (void) index;

    static uint8_t desc_other_speed_config[CONFIG_TOTAL_LEN_MAX];
    uint8_t const* source;
    uint16_t config_len;

    if (mode == HID)
    {
        source = (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_configuration_hid_cdc : desc_hs_configuration_hid_cdc;
        config_len = CONFIG_TOTAL_LEN_HID_CDC;
    }
    else
    {
        source = (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_configuration_cdc_msc : desc_hs_configuration_cdc_msc;
        config_len = CONFIG_TOTAL_LEN_CDC_MSC;
    }

    memcpy(desc_other_speed_config, source, config_len);
    desc_other_speed_config[1] = TUSB_DESC_OTHER_SPEED_CONFIG;

    return desc_other_speed_config;
}

uint8_t const* tud_descriptor_device_qualifier_cb(void)
{
    return (uint8_t const*) &desc_device_qualifier;
}
#endif
