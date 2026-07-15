#ifndef __HID_H
#define __HID_H

/*
 * Reduced Linux HID compatibility contract for the firmware port. The active
 * parser/driver C files stay close to upstream Linux; this header carries only
 * the declarations/macros those files need plus local transport fields.
 */

#include "hid_compat.h"
#include "power_supply.h"
#include "tusb.h"

#include "input.h"
#include "log.h"
#include "usb.h"

#ifdef HID_USAGE_PAGE
#undef HID_USAGE_PAGE
#endif

#ifdef HID_USAGE
#undef HID_USAGE
#endif

#ifdef HID_REPORT_ID
#undef HID_REPORT_ID
#endif

#define HID_MAX_IDS 256
#define HID_MAX_DESCRIPTOR_SIZE 4096
#define HID_MAX_FIELDS 256
// #define HID_MAX_USAGES 12288
// KeyD discards generic Consumer events above usage 0x02A2. Limit parser
// usage arrays to 675 entries instead of allocating mappings that cannot
// reach the current KeyD input boundary. HID_MAX_USAGES also bounds Report
// Count, so descriptors declaring a larger count are rejected while parsing.
#define HID_MAX_USAGES 675
#define HID_DEFAULT_NUM_COLLECTIONS 16
// #define HID_COLLECTION_STACK_SIZE 4
// Port grows the parser collection stack in larger chunks; open_collection() still grows on demand.
#define HID_COLLECTION_STACK_SIZE 16
#define HID_GLOBAL_STACK_SIZE 4
#define HID_MAX_BUFFER_SIZE 16384
#define HID_MIN_BUFFER_SIZE	64		/* make sure there is at least a packet size of space */
#define HID_CONTROL_FIFO_SIZE	256		/* to init devices with >100 reports */
#define HID_OUTPUT_FIFO_SIZE	64

#define HID_ITEM_FORMAT_SHORT 0
#define HID_ITEM_FORMAT_LONG 1
#define HID_ITEM_TAG_LONG 0xf

#define HID_ITEM_TYPE_MAIN 0
#define HID_ITEM_TYPE_GLOBAL 1
#define HID_ITEM_TYPE_LOCAL 2
#define HID_ITEM_TYPE_RESERVED 3

#define HID_MAIN_ITEM_TAG_INPUT 8
#define HID_MAIN_ITEM_TAG_OUTPUT 9
#define HID_MAIN_ITEM_TAG_FEATURE 11
#define HID_MAIN_ITEM_TAG_BEGIN_COLLECTION 10
#define HID_MAIN_ITEM_TAG_END_COLLECTION 12
#define HID_MAIN_ITEM_TAG_RESERVED_MIN 13
#define HID_MAIN_ITEM_TAG_RESERVED_MAX 15

#define HID_GLOBAL_ITEM_TAG_USAGE_PAGE 0
#define HID_GLOBAL_ITEM_TAG_LOGICAL_MINIMUM 1
#define HID_GLOBAL_ITEM_TAG_LOGICAL_MAXIMUM 2
#define HID_GLOBAL_ITEM_TAG_PHYSICAL_MINIMUM 3
#define HID_GLOBAL_ITEM_TAG_PHYSICAL_MAXIMUM 4
#define HID_GLOBAL_ITEM_TAG_UNIT_EXPONENT 5
#define HID_GLOBAL_ITEM_TAG_UNIT 6
#define HID_GLOBAL_ITEM_TAG_REPORT_SIZE 7
#define HID_GLOBAL_ITEM_TAG_REPORT_ID 8
#define HID_GLOBAL_ITEM_TAG_REPORT_COUNT 9
#define HID_GLOBAL_ITEM_TAG_PUSH 10
#define HID_GLOBAL_ITEM_TAG_POP 11

#define HID_LOCAL_ITEM_TAG_USAGE 0
#define HID_LOCAL_ITEM_TAG_USAGE_MINIMUM 1
#define HID_LOCAL_ITEM_TAG_USAGE_MAXIMUM 2
#define HID_LOCAL_ITEM_TAG_DESIGNATOR_INDEX 3
#define HID_LOCAL_ITEM_TAG_DESIGNATOR_MINIMUM 4
#define HID_LOCAL_ITEM_TAG_DESIGNATOR_MAXIMUM 5
#define HID_LOCAL_ITEM_TAG_STRING_INDEX 7
#define HID_LOCAL_ITEM_TAG_STRING_MINIMUM 8
#define HID_LOCAL_ITEM_TAG_STRING_MAXIMUM 9
#define HID_LOCAL_ITEM_TAG_DELIMITER 10

#define HID_MAIN_ITEM_CONSTANT 0x001
#define HID_MAIN_ITEM_VARIABLE 0x002
#define HID_MAIN_ITEM_RELATIVE 0x004
#define HID_MAIN_ITEM_WRAP 0x008
#define HID_MAIN_ITEM_NONLINEAR 0x010
#define HID_MAIN_ITEM_NO_PREFERRED 0x020
#define HID_MAIN_ITEM_NULL_STATE 0x040
#define HID_MAIN_ITEM_VOLATILE 0x080
#define HID_MAIN_ITEM_BUFFERED_BYTE 0x100

#define HID_USAGE_PAGE 0xffff0000u
#define HID_USAGE 0x0000ffffu

#define HID_UP_UNDEFINED	0x00000000
#define HID_UP_GENDESK		0x00010000
#define HID_UP_SIMULATION	0x00020000
#define HID_UP_GENDEVCTRLS	0x00060000
#define HID_UP_KEYBOARD		0x00070000
#define HID_UP_LED		0x00080000
#define HID_UP_BUTTON		0x00090000
#define HID_UP_ORDINAL		0x000a0000
#define HID_UP_TELEPHONY	0x000b0000
#define HID_UP_CONSUMER		0x000c0000
#define HID_UP_DIGITIZER	0x000d0000
#define HID_UP_HAPTIC		0x000e0000
#define HID_UP_PID		0x000f0000
#define HID_UP_BATTERY		0x00850000
#define HID_UP_CAMERA		0x00900000
#define HID_UP_HPVENDOR         0xff7f0000
#define HID_UP_HPVENDOR2        0xff010000
#define HID_UP_MSVENDOR		0xff000000
#define HID_UP_CUSTOM		0x00ff0000
#define HID_UP_LOGIVENDOR	0xffbc0000
#define HID_UP_LOGIVENDOR2   0xff090000
#define HID_UP_LOGIVENDOR3   0xff430000
#define HID_UP_LNVENDOR		0xffa00000
#define HID_UP_SENSOR		0x00200000
#define HID_UP_ASUSVENDOR	0xff310000
#define HID_UP_GOOGLEVENDOR	0xffd10000

#define HID_GD_POINTER		0x00010001
#define HID_GD_MOUSE		0x00010002
#define HID_GD_JOYSTICK		0x00010004
#define HID_GD_GAMEPAD		0x00010005
#define HID_GD_KEYBOARD		0x00010006
#define HID_GD_KEYPAD		0x00010007
#define HID_GD_MULTIAXIS	0x00010008
#define HID_GD_WIRELESS_RADIO_CTLS	0x0001000c
#define HID_GD_SYSTEM_MULTIAXIS	0x0001000e
#define HID_GD_X		0x00010030
#define HID_GD_Y		0x00010031
#define HID_GD_Z		0x00010032
#define HID_GD_RX		0x00010033
#define HID_GD_RY		0x00010034
#define HID_GD_RZ		0x00010035
#define HID_GD_SLIDER		0x00010036
#define HID_GD_DIAL		0x00010037
#define HID_GD_WHEEL		0x00010038
#define HID_GD_HATSWITCH	0x00010039
#define HID_GD_BUFFER		0x0001003a
#define HID_GD_BYTECOUNT	0x0001003b
#define HID_GD_MOTION		0x0001003c
#define HID_GD_START		0x0001003d
#define HID_GD_SELECT		0x0001003e
#define HID_GD_VX		0x00010040
#define HID_GD_VY		0x00010041
#define HID_GD_VZ		0x00010042
#define HID_GD_VBRX		0x00010043
#define HID_GD_VBRY		0x00010044
#define HID_GD_VBRZ		0x00010045
#define HID_GD_VNO		0x00010046
#define HID_GD_FEATURE		0x00010047
#define HID_GD_RESOLUTION_MULTIPLIER	0x00010048
#define HID_GD_SYSTEM_CONTROL	0x00010080
#define HID_GD_SYSTEM_POWER_DOWN	0x00010081
#define HID_GD_SYSTEM_SLEEP	0x00010082
#define HID_GD_SYSTEM_WAKE_UP	0x00010083
#define HID_GD_SYSTEM_CONTEXT_MENU	0x00010084
#define HID_GD_SYSTEM_MAIN_MENU	0x00010085
#define HID_GD_SYSTEM_APP_MENU	0x00010086
#define HID_GD_SYSTEM_MENU_HELP	0x00010087
#define HID_GD_SYSTEM_MENU_EXIT	0x00010088
#define HID_GD_SYSTEM_MENU_SELECT	0x00010089
#define HID_GD_SYSTEM_MENU_RIGHT	0x0001008a
#define HID_GD_SYSTEM_MENU_LEFT	0x0001008b
#define HID_GD_SYSTEM_MENU_UP	0x0001008c
#define HID_GD_SYSTEM_MENU_DOWN	0x0001008d
#define HID_GD_SYSTEM_COLD_RESTART	0x0001008e
#define HID_GD_SYSTEM_WARM_RESTART	0x0001008f
#define HID_GD_UP		0x00010090
#define HID_GD_DOWN		0x00010091
#define HID_GD_RIGHT		0x00010092
#define HID_GD_LEFT		0x00010093
#define HID_GD_DO_NOT_DISTURB	0x0001009b
#define HID_GD_SYSTEM_SPEAKER_MUTE	0x000100a7
#define HID_GD_SYSTEM_HIBERNATE	0x000100a8
#define HID_GD_SYSTEM_DISPLAY_TOGGLE_INT_EXT	0x000100b5
#define HID_GD_RFKILL_BTN	0x000100c6
#define HID_GD_RFKILL_LED	0x000100c7
#define HID_GD_RFKILL_SWITCH	0x000100c8

#define HID_DC_BATTERYSTRENGTH	0x00060020

#define HID_CP_CONSUMER_CONTROL	0x000c0001
#define HID_CP_AC_PAN		0x000c0238

#define HID_DG_DIGITIZER	0x000d0001
#define HID_DG_PEN		0x000d0002
#define HID_DG_LIGHTPEN		0x000d0003
#define HID_DG_TOUCHSCREEN	0x000d0004
#define HID_DG_TOUCHPAD		0x000d0005
#define HID_DG_WHITEBOARD	0x000d0006
#define HID_DG_STYLUS		0x000d0020
#define HID_DG_PUCK		0x000d0021
#define HID_DG_FINGER		0x000d0022
#define HID_DG_TIPPRESSURE	0x000d0030
#define HID_DG_BARRELPRESSURE	0x000d0031
#define HID_DG_INRANGE		0x000d0032
#define HID_DG_TOUCH		0x000d0033
#define HID_DG_UNTOUCH		0x000d0034
#define HID_DG_TAP		0x000d0035
#define HID_DG_TRANSDUCER_INDEX	0x000d0038
#define HID_DG_TABLETFUNCTIONKEY	0x000d0039
#define HID_DG_PROGRAMCHANGEKEY	0x000d003a
#define HID_DG_BATTERYSTRENGTH	0x000d003b
#define HID_DG_INVERT		0x000d003c
#define HID_DG_TILT_X		0x000d003d
#define HID_DG_TILT_Y		0x000d003e
#define HID_DG_TWIST		0x000d0041
#define HID_DG_TIPSWITCH	0x000d0042
#define HID_DG_TIPSWITCH2	0x000d0043
#define HID_DG_BARRELSWITCH	0x000d0044
#define HID_DG_ERASER		0x000d0045
#define HID_DG_TABLETPICK	0x000d0046
#define HID_DG_PEN_COLOR			0x000d005c
#define HID_DG_PEN_LINE_WIDTH			0x000d005e
#define HID_DG_PEN_LINE_STYLE			0x000d0070
#define HID_DG_PEN_LINE_STYLE_INK		0x000d0072
#define HID_DG_PEN_LINE_STYLE_PENCIL		0x000d0073
#define HID_DG_PEN_LINE_STYLE_HIGHLIGHTER	0x000d0074
#define HID_DG_PEN_LINE_STYLE_CHISEL_MARKER	0x000d0075
#define HID_DG_PEN_LINE_STYLE_BRUSH		0x000d0076
#define HID_DG_PEN_LINE_STYLE_NO_PREFERENCE	0x000d0077

#define HID_CP_CONSUMERCONTROL	0x000c0001
#define HID_CP_NUMERICKEYPAD	0x000c0002
#define HID_CP_PROGRAMMABLEBUTTONS	0x000c0003
#define HID_CP_MICROPHONE	0x000c0004
#define HID_CP_HEADPHONE	0x000c0005
#define HID_CP_GRAPHICEQUALIZER	0x000c0006
#define HID_CP_FUNCTIONBUTTONS	0x000c0036
#define HID_CP_SELECTION	0x000c0080
#define HID_CP_MEDIASELECTION	0x000c0087
#define HID_CP_SELECTDISC	0x000c00ba
#define HID_CP_VOLUMEUP		0x000c00e9
#define HID_CP_VOLUMEDOWN	0x000c00ea
#define HID_CP_PLAYBACKSPEED	0x000c00f1
#define HID_CP_PROXIMITY	0x000c0109
#define HID_CP_SPEAKERSYSTEM	0x000c0160
#define HID_CP_CHANNELLEFT	0x000c0161
#define HID_CP_CHANNELRIGHT	0x000c0162
#define HID_CP_CHANNELCENTER	0x000c0163
#define HID_CP_CHANNELFRONT	0x000c0164
#define HID_CP_CHANNELCENTERFRONT	0x000c0165
#define HID_CP_CHANNELSIDE	0x000c0166
#define HID_CP_CHANNELSURROUND	0x000c0167
#define HID_CP_CHANNELLOWFREQUENCYENHANCEMENT	0x000c0168
#define HID_CP_CHANNELTOP	0x000c0169
#define HID_CP_CHANNELUNKNOWN	0x000c016a
#define HID_CP_APPLICATIONLAUNCHBUTTONS	0x000c0180
#define HID_CP_GENERICGUIAPPLICATIONCONTROLS	0x000c0200

#define HID_DG_DEVICECONFIG	0x000d000e
#define HID_DG_DEVICESETTINGS	0x000d0023
#define HID_DG_AZIMUTH		0x000d003f
#define HID_DG_CONFIDENCE	0x000d0047
#define HID_DG_WIDTH		0x000d0048
#define HID_DG_HEIGHT		0x000d0049
#define HID_DG_CONTACTID	0x000d0051
#define HID_DG_INPUTMODE	0x000d0052
#define HID_DG_DEVICEINDEX	0x000d0053
#define HID_DG_CONTACTCOUNT	0x000d0054
#define HID_DG_CONTACTMAX	0x000d0055
#define HID_DG_SCANTIME		0x000d0056
#define HID_DG_SURFACESWITCH	0x000d0057
#define HID_DG_BUTTONSWITCH	0x000d0058
#define HID_DG_BUTTONTYPE	0x000d0059
#define HID_DG_BARRELSWITCH2	0x000d005a
#define HID_DG_TOOLSERIALNUMBER	0x000d005b
#define HID_DG_LATENCYMODE	0x000d0060

#define HID_HP_SIMPLECONTROLLER	0x000e0001
#define HID_HP_WAVEFORMLIST	0x000e0010
#define HID_HP_DURATIONLIST	0x000e0011
#define HID_HP_AUTOTRIGGER	0x000e0020
#define HID_HP_MANUALTRIGGER	0x000e0021
#define HID_HP_AUTOTRIGGERASSOCIATEDCONTROL 0x000e0022
#define HID_HP_INTENSITY	0x000e0023
#define HID_HP_REPEATCOUNT	0x000e0024
#define HID_HP_RETRIGGERPERIOD	0x000e0025
#define HID_HP_WAVEFORMVENDORPAGE	0x000e0026
#define HID_HP_WAVEFORMVENDORID	0x000e0027
#define HID_HP_WAVEFORMCUTOFFTIME	0x000e0028
#define HID_HP_WAVEFORMNONE	0x000e1001
#define HID_HP_WAVEFORMSTOP	0x000e1002
#define HID_HP_WAVEFORMCLICK	0x000e1003
#define HID_HP_WAVEFORMBUZZCONTINUOUS	0x000e1004
#define HID_HP_WAVEFORMRUMBLECONTINUOUS	0x000e1005
#define HID_HP_WAVEFORMPRESS	0x000e1006
#define HID_HP_WAVEFORMRELEASE	0x000e1007
#define HID_HP_VENDORWAVEFORMMIN	0x000e2001
#define HID_HP_VENDORWAVEFORMMAX	0x000e2fff

#define HID_BAT_ABSOLUTESTATEOFCHARGE	0x00850065
#define HID_BAT_CHARGING		0x00850044

#define HID_VD_ASUS_CUSTOM_MEDIA_KEYS	0xff310076

#define HID_COLLECTION_PHYSICAL		0
#define HID_COLLECTION_APPLICATION	1
#define HID_COLLECTION_LOGICAL		2
#define HID_COLLECTION_NAMED_ARRAY	4

#define HID_CONNECT_HIDINPUT		BIT(0)
#define HID_CONNECT_HIDINPUT_FORCE	BIT(1)
#define HID_CONNECT_HIDRAW		BIT(2)
#define HID_CONNECT_HIDDEV		BIT(3)
#define HID_CONNECT_HIDDEV_FORCE	BIT(4)
#define HID_CONNECT_FF			BIT(5)
#define HID_CONNECT_DRIVER		BIT(6)
#define HID_CONNECT_DEFAULT	(HID_CONNECT_HIDINPUT|HID_CONNECT_HIDRAW| \
		HID_CONNECT_HIDDEV|HID_CONNECT_FF)

#define HID_QUIRK_NOTOUCH			BIT(1)
#define HID_QUIRK_IGNORE			BIT(2)
#define HID_QUIRK_NOGET				BIT(3)
#define HID_QUIRK_HIDDEV_FORCE			BIT(4)
#define HID_QUIRK_BADPAD			BIT(5)
#define HID_QUIRK_MULTI_INPUT			BIT(6)
#define HID_QUIRK_HIDINPUT_FORCE		BIT(7)
#define HID_QUIRK_ALWAYS_POLL			BIT(10)
#define HID_QUIRK_INPUT_PER_APP			BIT(11)
#define HID_QUIRK_X_INVERT			BIT(12)
#define HID_QUIRK_Y_INVERT			BIT(13)
#define HID_QUIRK_IGNORE_MOUSE			BIT(14)
#define HID_QUIRK_SKIP_OUTPUT_REPORTS		BIT(16)
#define HID_QUIRK_SKIP_OUTPUT_REPORT_ID		BIT(17)
#define HID_QUIRK_NO_OUTPUT_REPORTS_ON_INTR_EP	BIT(18)
#define HID_QUIRK_HAVE_SPECIAL_DRIVER		BIT(19)
#define HID_QUIRK_INCREMENT_USAGE_ON_DUPLICATE	BIT(20)
#define HID_QUIRK_NOINVERT			BIT(21)
#define HID_QUIRK_IGNORE_SPECIAL_DRIVER		BIT(22)
#define HID_QUIRK_POWER_ON_AFTER_BACKLIGHT	BIT(23)
#define HID_QUIRK_FULLSPEED_INTERVAL		BIT(28)
#define HID_QUIRK_NO_INIT_REPORTS		BIT(29)
#define HID_QUIRK_NO_IGNORE			BIT(30)
#define HID_QUIRK_NO_INPUT_SYNC			BIT(31)

#define MAX_USBHID_BOOT_QUIRKS 4

#define HID_GROUP_GENERIC			0x0001
#define HID_GROUP_MULTITOUCH			0x0002
#define HID_GROUP_SENSOR_HUB			0x0003
#define HID_GROUP_MULTITOUCH_WIN_8		0x0004

#define HID_GROUP_RMI				0x0100
#define HID_GROUP_WACOM				0x0101
#define HID_GROUP_LOGITECH_DJ_DEVICE		0x0102
#define HID_GROUP_STEAM				0x0103
#define HID_GROUP_LOGITECH_27MHZ_DEVICE		0x0104
#define HID_GROUP_VIVALDI			0x0105

#define HID_REPORT_PROTOCOL	1
#define HID_BOOT_PROTOCOL	0

#define HID_UNIT_GRAM		0x0101
#define HID_UNIT_NEWTON		0xe111

#define HID_SCAN_FLAG_MT_WIN_8			BIT(0)
#define HID_SCAN_FLAG_VENDOR_SPECIFIC		BIT(1)
#define HID_SCAN_FLAG_GD_POINTER		BIT(2)

#define HID_CLAIMED_INPUT	BIT(0)
#define HID_CLAIMED_HIDDEV	BIT(1)
#define HID_CLAIMED_HIDRAW	BIT(2)
#define HID_CLAIMED_DRIVER	BIT(3)

#define HID_STAT_ADDED		BIT(0)
#define HID_STAT_PARSED		BIT(1)
#define HID_STAT_DUP_DETECTED	BIT(2)
#define HID_STAT_REPROBED	BIT(3)

enum hid_report_type {
	HID_INPUT_REPORT = 0,
	HID_OUTPUT_REPORT,
	HID_FEATURE_REPORT,
	HID_REPORT_TYPES,
};

enum hid_class_request {
	HID_REQ_GET_REPORT		= 0x01,
	HID_REQ_GET_IDLE		= 0x02,
	HID_REQ_GET_PROTOCOL		= 0x03,
	HID_REQ_SET_REPORT		= 0x09,
	HID_REQ_SET_IDLE		= 0x0A,
	HID_REQ_SET_PROTOCOL		= 0x0B,
};

struct hid_collection {
	int parent_idx; /* device->collection */
	unsigned type;
	unsigned usage;
	unsigned level;
};

struct hid_usage {
	unsigned  hid;			/* hid usage code */
	unsigned  collection_index;	/* index into collection array */
	unsigned  usage_index;		/* index into usage array */
	__s8	  resolution_multiplier;/* Effective Resolution Multiplier
					   (HUT v1.12, 4.3.1), default: 1 */
	/* hidinput data */
	__s8	  wheel_factor;		/* 120/resolution_multiplier */
	__u16     code;			/* input driver code */
	__u8      type;			/* input driver type */
	__s16	  hat_min;		/* hat switch fun */
	__s16	  hat_max;		/* ditto */
	__s16	  hat_dir;		/* ditto */
	__s16	  wheel_accumulated;	/* hi-res wheel */
};

struct hid_input;
struct hid_report;
struct input_dev;

struct hid_field {
	unsigned  physical;		/* physical usage for this field */
	unsigned  logical;		/* logical usage for this field */
	unsigned  application;		/* application usage for this field */
	struct hid_usage *usage;	/* usage table for this function */
	unsigned  maxusage;		/* maximum usage index */
	unsigned  flags;		/* main-item flags (i.e. volatile,array,constant) */
	unsigned  report_offset;	/* bit offset in the report */
	unsigned  report_size;		/* size of this field in the report */
	unsigned  report_count;		/* number of this field in the report */
	unsigned  report_type;		/* (input,output,feature) */
	__s32    *value;		/* last known value(s) */
	__s32    *new_value;		/* newly read value(s) */
	__s32    *usages_priorities;	/* priority of each usage when reading the report
					 * bits 8-16 are reserved for hid-input usage
					 */
	__s32     logical_minimum;
	__s32     logical_maximum;
	__s32     physical_minimum;
	__s32     physical_maximum;
	__s32     unit_exponent;
	unsigned  unit;
	bool      ignored;		/* this field is ignored in this event */
	struct hid_report *report;	/* associated report */
	unsigned index;			/* index into report->field[] */
	/* hidinput data */
	struct hid_input *hidinput;	/* associated input structure */
	__u16 dpad;			/* dpad input code */
	unsigned int slot_idx;		/* slot index in a report */
};

struct hid_field_entry {
	struct list_head list;
	struct hid_field *field;
	unsigned int index;
	__s32 priority;
};

struct hid_report {
	struct list_head list;
	struct list_head hidinput_list;
	struct list_head field_entry_list;		/* ordered list of input fields */
	unsigned int id;				/* id of this report */
	enum hid_report_type type;			/* report type */
	unsigned int application;			/* application usage for this report */
	struct hid_field *field[HID_MAX_FIELDS];	/* fields of the report */
	struct hid_field_entry *field_entries;		/* allocated memory of input field_entry */
	unsigned maxfield;				/* maximum valid field index */
	unsigned size;					/* size of the report (bits) */
	struct hid_device *device;			/* associated device */

	/* tool related state */
	bool tool_active;				/* whether the current tool is active */
	unsigned int tool;				/* BTN_TOOL_* */
};

struct hid_report_enum {
	unsigned numbered;
	struct list_head report_list;
	struct hid_report *report_id_hash[HID_MAX_IDS];
};

struct hid_control_fifo {
	unsigned char dir;
	struct hid_report *report;
	char *raw_report;
};

struct hid_output_fifo {
	struct hid_report *report;
	char *raw_report;
};

struct hid_input {
	struct list_head list;
	struct hid_report *report;
	struct input_dev *input;
	const char *name;
	struct list_head reports;	/* the list of reports */
	unsigned int application;	/* application usage for this input */
	bool registered;
};

enum hid_battery_status {
	HID_BATTERY_UNKNOWN = 0,
	HID_BATTERY_QUERIED,		/* Kernel explicitly queried battery strength */
	HID_BATTERY_REPORTED,		/* Device sent unsolicited battery strength report */
};

struct hid_battery {
	struct hid_device *dev;
	struct power_supply *ps;
	__s32 min;
	__s32 max;
	__s32 report_type;
	__s32 report_id;
	__s32 charge_status;
	enum hid_battery_status status;
	__s32 capacity;
	bool avoid_query;
	bool present;
	ktime_t ratelimit_time;
	struct list_head list;
};

struct hid_global {
	unsigned int usage_page;
	__s32 logical_minimum;
	__s32 logical_maximum;
	__s32 physical_minimum;
	__s32 physical_maximum;
	__s32 unit_exponent;
	unsigned int unit;
	unsigned int report_id;
	unsigned int report_size;
	unsigned int report_count;
};

struct hid_local {
	// unsigned usage[HID_MAX_USAGES]; /* usage array */
	// u8 usage_size[HID_MAX_USAGES]; /* usage size array */
	// unsigned collection_index[HID_MAX_USAGES]; /* collection index array */
	// Firmware allocates parser-local arrays on demand up to this port's
	// HID_MAX_USAGES cap instead of embedding them in each parser.
	unsigned int *usage;
	u8 *usage_size;
	unsigned int *collection_index;
	unsigned int usage_alloc;
	unsigned int usage_index;
	unsigned int usage_minimum;
	unsigned int delimiter_depth;
	unsigned int delimiter_branch;
};

struct hid_parser {
	struct hid_global global;
	struct hid_global global_stack[HID_GLOBAL_STACK_SIZE];
	unsigned int global_stack_ptr;
	struct hid_local local;
	unsigned int *collection_stack;
	unsigned int collection_stack_ptr;
	unsigned int collection_stack_size;
	struct hid_device *device;
	unsigned int scan_flags;
};

enum hid_type {
	HID_TYPE_OTHER = 0,
	HID_TYPE_USBMOUSE,
	HID_TYPE_USBNONE
};

struct hid_driver;
struct hid_ll_driver;

struct hid_device {
	const __u8 *dev_rdesc;						/* device report descriptor */
	const __u8 *bpf_rdesc;						/* bpf modified report descriptor, if any */
	const __u8 *rdesc;						/* currently used report descriptor */
	const __u8 *ll_rdesc;						/* port transport report descriptor */
	unsigned int dev_rsize;
	unsigned int bpf_rsize;
	unsigned int rsize;
	unsigned int ll_rsize;
	unsigned int collection_size;					/* Number of allocated hid_collections */
	struct hid_collection *collection;				/* List of HID collections */
	unsigned int maxcollection;						/* Number of parsed collections */
	unsigned int maxapplication;					/* Number of applications */
	__u16 bus;							/* BUS ID */
	__u16 group;							/* Report group */
	__u32 vendor;							/* Vendor ID */
	__u32 product;							/* Product ID */
	__u32 version;							/* HID version */
	enum hid_type type;						/* device type (mouse, kbd, ...) */
	unsigned country;						/* HID country */
	struct hid_report_enum report_enum[HID_REPORT_TYPES];
	struct work_struct led_work;					/* delayed LED worker */

	struct semaphore driver_input_lock;				/* protects the current driver */
	struct device dev;						/* Linux device */
	// struct hid_driver *driver;
	// Imported driver descriptors are const; mutable registration state lives in hid_driver_runtime.
	const struct hid_driver *driver;
	void *devres_group_id;						/* ID of probe devres group	*/

	const struct hid_ll_driver *ll_driver;
	struct mutex ll_open_lock;
	unsigned int ll_open_count;

#ifdef CONFIG_HID_BATTERY_STRENGTH
	/*
	 * Power supply information for HID devices which report
	 * battery strength. Each battery is tracked separately in the
	 * batteries list.
	 */
	struct list_head batteries;
#endif

	unsigned long status;						/* see STAT flags above */
	unsigned claimed;						/* Claimed by hidinput, hiddev? */
	unsigned quirks;						/* Various quirks the device can pull on us */
	unsigned initial_quirks;					/* Initial set of quirks supplied when creating device */
	bool io_started;						/* If IO has started */

	struct list_head inputs;					/* The list of inputs */
	void *hiddev;							/* The hiddev structure */
	void *hidraw;

	char name[128];							/* Device name */
	char phys[64];							/* Device physical location */
	char uniq[64];							/* Device unique identifier (serial #) */
	u64 firmware_version;						/* Firmware version */

	void *driver_data;

	/* temporary hid_ff handling (until moved to the drivers) */
	int (*ff_init)(struct hid_device *);

	/* hiddev event handler */
	int (*hiddev_connect)(struct hid_device *, unsigned int);
	void (*hiddev_disconnect)(struct hid_device *);
	void (*hiddev_hid_event) (struct hid_device *, struct hid_field *field,
				  struct hid_usage *, __s32);
	void (*hiddev_report_event) (struct hid_device *, struct hid_report *);

	/* debugging support via debugfs */
	unsigned short debug;
	struct dentry *debug_dir;
	struct dentry *debug_rdesc;
	struct dentry *debug_events;
	struct list_head debug_list;
	spinlock_t debug_list_lock;
	wait_queue_head_t debug_wait;
	struct kref ref;

	unsigned int id;						/* system unique id */

	/*
	 * Port-only TinyUSB callback identity. Upstream Linux gets this context
	 * through usbhid/usb_interface objects; TinyUSB callbacks pass dev_addr
	 * and HID instance, so the firmware lookup stores both on hid_device.
	 */
	u8 dev_addr;
	u8 instance;
	u32 ll_generation;
	wait_queue_head_t ll_wait;
	unsigned int ll_report_pending;
	unsigned int ll_report_active;
	bool ll_always_poll;
	bool ll_resume_running;
	unsigned long ll_resume_deadline;
	/*
	 * Port-only minimal USB core shim. Upstream hid_device is parented by
	 * Linux USB core objects; firmware embeds just enough usb_device,
	 * usb_host_interface, and usb_interface state for imported HID code.
	 */
	struct usb_device usb_dev;
	struct usb_host_interface usb_altsetting;
	struct usb_interface usb_intf;
};

#define to_hid_device(pdev) \
	container_of(pdev, struct hid_device, dev)

// #define hid_to_usb_dev(hid_dev) to_usb_device(hid_dev->dev.parent->parent)
// This port has the same hid_device -> usb_interface -> usb_device parent chain.
static inline struct usb_device *hid_to_usb_dev(struct hid_device *hid_dev)
{
	return to_usb_device(hid_dev->dev.parent->parent);
}

static inline void *hid_get_drvdata(struct hid_device *hdev)
{
	return dev_get_drvdata(&hdev->dev);
}

static inline void hid_set_drvdata(struct hid_device *hdev, void *data)
{
	dev_set_drvdata(&hdev->dev, data);
}

#ifdef CONFIG_HID_BATTERY_STRENGTH
static inline struct hid_battery *hid_get_battery(struct hid_device *hdev)
{
	if (list_empty(&hdev->batteries))
		return NULL;
	return list_first_entry(&hdev->batteries, struct hid_battery, list);
}
#endif

struct hid_item {
	unsigned int format;
	u8 size;
	u8 type;
	u8 tag;
	union {
		u8 u8;
		s8 s8;
		u16 u16;
		s16 s16;
		u32 u32;
		s32 s32;
	const u8 *longdata;
	} data;
};

struct hid_class_descriptor {
	__u8 bDescriptorType;
	__le16 wDescriptorLength;
} __attribute__ ((packed));

struct hid_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__le16 bcdHID;
	__u8 bCountryCode;
	__u8 bNumDescriptors;
	struct hid_class_descriptor rpt_desc;

	struct hid_class_descriptor opt_descs[];
} __attribute__ ((packed));

#define HID_DEVICE(b, g, ven, prod)					\
	.bus = (b), .group = (g), .vendor = (ven), .product = (prod)
#define HID_USB_DEVICE(ven, prod)				\
	.bus = BUS_USB, .vendor = (ven), .product = (prod)
#define HID_BLUETOOTH_DEVICE(ven, prod)					\
	.bus = BUS_BLUETOOTH, .vendor = (ven), .product = (prod)
#define HID_I2C_DEVICE(ven, prod)				\
	.bus = BUS_I2C, .vendor = (ven), .product = (prod)

#define HID_REPORT_ID(rep) \
	.report_type = (rep)
#define HID_USAGE_ID(uhid, utype, ucode) \
	.usage_hid = (uhid), .usage_type = (utype), .usage_code = (ucode)
#define HID_ANY_ID		(~0)
#define HID_BUS_ANY		0xffff
#define HID_GROUP_ANY		0x0000
/* we don't want to catch types and codes equal to 0 */
#define HID_TERMINATOR		(HID_ANY_ID - 1)

struct hid_report_id {
	__u32 report_type;
};

struct hid_usage_id {
	__u32 usage_hid;
	__u32 usage_type;
	__u32 usage_code;
};

struct hid_driver {
	const char *name;
	const struct hid_device_id *id_table;

	struct list_head dyn_list;
	spinlock_t dyn_lock;

	bool (*match)(struct hid_device *dev, bool ignore_special_driver);
	int (*probe)(struct hid_device *dev, const struct hid_device_id *id);
	void (*remove)(struct hid_device *dev);

	const struct hid_report_id *report_table;
	int (*raw_event)(struct hid_device *hdev, struct hid_report *report,
			u8 *data, int size);
	/* Upstream has no callback-safety marker; this port uses it only to keep
	 * audited nonblocking raw_event hooks inline in the TinyUSB callback path.
	 * Other raw_event hooks are deferred to the HID async task.
	 */
	bool raw_event_callback_safe;
	const struct hid_usage_id *usage_table;
	int (*event)(struct hid_device *hdev, struct hid_field *field,
			struct hid_usage *usage, __s32 value);
	void (*report)(struct hid_device *hdev, struct hid_report *report);

	const __u8 *(*report_fixup)(struct hid_device *hdev, __u8 *buf,
			unsigned int *size);

	int (*input_mapping)(struct hid_device *hdev,
			struct hid_input *hidinput, struct hid_field *field,
			struct hid_usage *usage, unsigned long **bit, int *max);
	int (*input_mapped)(struct hid_device *hdev,
			struct hid_input *hidinput, struct hid_field *field,
			struct hid_usage *usage, unsigned long **bit, int *max);
	int (*input_configured)(struct hid_device *hdev,
				struct hid_input *hidinput);
	void (*feature_mapping)(struct hid_device *hdev,
			struct hid_field *field,
			struct hid_usage *usage);

	int (*suspend)(struct hid_device *hdev, pm_message_t message);
	int (*resume)(struct hid_device *hdev);
	int (*reset_resume)(struct hid_device *hdev);
	void (*on_hid_hw_open)(struct hid_device *hdev);
	void (*on_hid_hw_close)(struct hid_device *hdev);

/* private: */
	struct device_driver driver;
};

// Upstream Linux stores this mutable state inside struct hid_driver. Firmware
// keeps imported descriptors const and gives each builtin driver separate
// .bss storage.
struct hid_driver_runtime {
	const struct hid_driver *hid_driver;
	struct list_head dyn_list;
	spinlock_t dyn_lock;
	struct device_driver driver;
};

struct hid_builtin_driver {
	const struct hid_driver *hid_driver;
	struct hid_driver_runtime *runtime;
};

// #define to_hid_driver(pdrv) \
// 	container_of(pdrv, struct hid_driver, driver)
// The port keeps mutable device_driver state in hid_driver_runtime so hid_driver can stay const.
#define to_hid_driver(pdrv) \
	((const struct hid_driver *)(pdrv)->hid_driver)

struct hid_ll_driver {
	int (*start)(struct hid_device *hdev);
	void (*stop)(struct hid_device *hdev);

	int (*open)(struct hid_device *hdev);
	void (*close)(struct hid_device *hdev);

	int (*power)(struct hid_device *hdev, int level);

	int (*parse)(struct hid_device *hdev);

	void (*request)(struct hid_device *hdev,
			struct hid_report *report, enum hid_class_request reqtype);

	int (*wait)(struct hid_device *hdev);

	int (*raw_request) (struct hid_device *hdev, unsigned char reportnum,
			    __u8 *buf, size_t len, unsigned char rtype,
			    int reqtype);

	int (*output_report) (struct hid_device *hdev, __u8 *buf, size_t len);

	int (*idle)(struct hid_device *hdev, int report, int idle, int reqtype);

	bool (*may_wakeup)(struct hid_device *hdev);

	unsigned int max_buffer_size;
};

#define	PM_HINT_FULLON	1<<5
#define PM_HINT_NORMAL	1<<1

/* Applications from HID Usage Tables 4/8/99 Version 1.1 */
/* We ignore a few input applications that are not widely used */
#define IS_INPUT_APPLICATION(a) \
		(((a >= HID_UP_GENDESK) && (a <= HID_GD_MULTIAXIS)) \
		|| ((a >= HID_DG_DIGITIZER) && (a <= HID_DG_WHITEBOARD)) \
		|| (a == HID_GD_SYSTEM_CONTROL) || (a == HID_CP_CONSUMER_CONTROL) \
		|| (a == HID_GD_WIRELESS_RADIO_CTLS))

// extern int __must_check __hid_register_driver(struct hid_driver *,
// 		struct module *, const char *mod_name);
// Firmware keeps imported driver descriptors const.
extern int __must_check __hid_register_driver(const struct hid_driver *,
		struct module *, const char *mod_name);
// Upstream Linux: no equivalent; fixed firmware linker entries own builtin
// runtime storage.
struct hid_driver_runtime *hid_builtin_driver_runtime(const struct hid_driver *hid_driver);

#define hid_register_driver(driver) \
	__hid_register_driver(driver, THIS_MODULE, KBUILD_MODNAME)

extern void hid_unregister_driver(const struct hid_driver *);
// extern const struct bus_type hid_bus_type;
// Port bus_register() stores runtime device/driver lists in the bus object.
extern struct bus_type hid_bus_type;

#define __HID_DRIVER_CONCAT(a, b) a##b
#define __HID_DRIVER_CONCAT2(a, b) __HID_DRIVER_CONCAT(a, b)
// #define module_hid_driver(__hid_driver) \
// 	module_driver(__hid_driver, hid_register_driver, \
// 		      hid_unregister_driver)
// Firmware has no module loader; hid-drivers.c registers linker-section
// entries instead.
// Each entry owns mutable .bss runtime storage so registration does not allocate it.
#define __MODULE_HID_DRIVER(__hid_driver, __id) \
	static struct hid_driver_runtime __HID_DRIVER_CONCAT2(__hid_builtin_runtime_, __id); \
	static const struct hid_builtin_driver __HID_DRIVER_CONCAT2(__hid_builtin_driver_, __id) \
	__attribute__((used, section("hid_drivers"))) = { \
		.hid_driver = &(__hid_driver), \
		.runtime = &__HID_DRIVER_CONCAT2(__hid_builtin_runtime_, __id), \
	}
#define module_hid_driver(__hid_driver) \
	__MODULE_HID_DRIVER(__hid_driver, __COUNTER__)

#define hid_dump_input(a,b,c) do { } while (0)
#define hid_dump_report(a,b,c,d) do { } while (0)
#define dbg_hid(fmt, ...) do { } while (0)
#define hid_err(hid, fmt, ...) dev_err(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_notice(hid, fmt, ...) dev_notice(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_warn(hid, fmt, ...) dev_warn(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_info(hid, fmt, ...) dev_info(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_dbg(hid, fmt, ...) dev_dbg(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_err_once(hid, fmt, ...) dev_err_once(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_notice_once(hid, fmt, ...) dev_notice_once(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_warn_once(hid, fmt, ...) dev_warn_once(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_info_once(hid, fmt, ...) dev_info_once(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_dbg_once(hid, fmt, ...) dev_dbg_once(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_err_ratelimited(hid, fmt, ...) dev_err_ratelimited(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_notice_ratelimited(hid, fmt, ...) dev_notice_ratelimited(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_warn_ratelimited(hid, fmt, ...) dev_warn_ratelimited(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_info_ratelimited(hid, fmt, ...) dev_info_ratelimited(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_dbg_ratelimited(hid, fmt, ...) dev_dbg_ratelimited(&(hid)->dev, fmt, ##__VA_ARGS__)

struct hid_device *hid_allocate_device(void);
void hid_destroy_device(struct hid_device *hid);
struct hid_report *hid_register_report(struct hid_device *device,
				       enum hid_report_type type, unsigned int id,
				       unsigned int application);
int hid_parse_report(struct hid_device *hid, const __u8 *start, unsigned int size);
u32 hid_field_extract(const struct hid_device *hid, u8 *report, unsigned offset, unsigned n);
int hid_report_raw_event(struct hid_device *hid, enum hid_report_type type, u8 *data,
			 size_t bufsize, u32 size, int interrupt);
int hid_add_device(struct hid_device *hdev);
bool hid_is_usb(const struct hid_device *hdev);
bool hid_match_one_id(const struct hid_device *hdev, const struct hid_device_id *id);
const struct hid_device_id *hid_match_id(const struct hid_device *hdev, const struct hid_device_id *id);
const struct hid_device_id *hid_match_device(struct hid_device *hdev, const struct hid_driver *hdrv);
bool hid_compare_device_paths(struct hid_device *hdev_a, struct hid_device *hdev_b, char separator);
bool hid_ignore(struct hid_device *hdev);
int hid_quirks_init(char **quirks_param, __u16 bus, int count);
void hid_quirks_exit(__u16 bus);
unsigned long hid_lookup_quirk(const struct hid_device *hdev);
int hid_core_init(void);
int linux_module_initcalls_init(void);
int hid_builtin_drivers_init(void);
int hid_open_report(struct hid_device *device);
static inline int __must_check hid_parse(struct hid_device *hdev)
{
	return hid_open_report(hdev);
}
int hid_connect(struct hid_device *hdev, unsigned int connect_mask);
void hid_disconnect(struct hid_device *hdev);
int hid_hw_start(struct hid_device *hdev, unsigned int connect_mask);
void hid_hw_stop(struct hid_device *hdev);
int hid_hw_open(struct hid_device *hdev);
void hid_hw_close(struct hid_device *hdev);
static inline void hid_hw_wait(struct hid_device *hdev)
{
	if (hdev->ll_driver->wait)
		hdev->ll_driver->wait(hdev);
}

static inline int hid_hw_power(struct hid_device *hdev, int level)
{
	return hdev->ll_driver->power ? hdev->ll_driver->power(hdev, level) : 0;
}

/**
 * hid_hw_idle - send idle request to device
 *
 * @hdev: hid device
 * @report: report to control
 * @idle: idle state
 * @reqtype: hid request type
 */
static inline int hid_hw_idle(struct hid_device *hdev, int report, int idle,
		enum hid_class_request reqtype)
{
	if (hdev->ll_driver->idle)
		return hdev->ll_driver->idle(hdev, report, idle, reqtype);

	return 0;
}

/**
 * hid_device_io_start - enable HID input during probe, remove
 *
 * @hid: the device
 *
 * This should only be called during probe or remove and only be
 * called by the thread calling probe or remove. It will allow
 * incoming packets to be delivered to the driver.
 */
static inline void hid_device_io_start(struct hid_device *hid) {
	if (hid->io_started) {
		dev_warn(&hid->dev, "io already started\n");
		return;
	}
	hid->io_started = true;
	// up(&hid->driver_input_lock);
	// Callback-driven slice does not allocate driver_input_lock.
}

/**
 * hid_device_io_stop - disable HID input during probe, remove
 *
 * @hid: the device
 *
 * Should only be called after hid_device_io_start. It will prevent
 * incoming packets from going to the driver for the duration of
 * probe, remove. If called during probe, packets will still go to the
 * driver after probe is complete. This function should only be called
 * by the thread calling probe or remove.
 */
static inline void hid_device_io_stop(struct hid_device *hid) {
	if (!hid->io_started) {
		dev_warn(&hid->dev, "io already stopped\n");
		return;
	}
	hid->io_started = false;
	// down(&hid->driver_input_lock);
	// Callback-driven slice does not allocate driver_input_lock.
}

/**
 * hid_hw_may_wakeup - return if the hid device may act as a wakeup source during system-suspend
 *
 * @hdev: hid device
 */
static inline bool hid_hw_may_wakeup(struct hid_device *hdev)
{
	if (hdev->ll_driver->may_wakeup)
		return hdev->ll_driver->may_wakeup(hdev);

	if (hdev->dev.parent)
		return device_may_wakeup(hdev->dev.parent);

	return false;
}

void hid_hw_request(struct hid_device *hdev, struct hid_report *report, enum hid_class_request reqtype);
int __hid_request(struct hid_device *hid, struct hid_report *report, enum hid_class_request reqtype);
int hid_hw_raw_request(struct hid_device *hdev, unsigned char reportnum, __u8 *buf,
		       size_t len, enum hid_report_type rtype, enum hid_class_request reqtype);
int hid_hw_output_report(struct hid_device *hdev, __u8 *buf, size_t len);
int __hid_hw_raw_request(struct hid_device *hdev, unsigned char reportnum, __u8 *buf,
			 size_t len, enum hid_report_type rtype,
			 enum hid_class_request reqtype, u64 source, bool from_bpf);
int __hid_hw_output_report(struct hid_device *hdev, __u8 *buf, size_t len, u64 source,
			   bool from_bpf);
// Upstream puts these no-op stubs in hid_bpf.h under !CONFIG_HID_BPF;
// firmware keeps them here while preserving inactive HID-BPF call sites.
static inline u8 *dispatch_hid_bpf_device_event(struct hid_device *hid, enum hid_report_type type,
						u8 *data, size_t *buf_size, u32 *size,
						int interrupt, u64 source, bool from_bpf)
{
	return data;
}
static inline int dispatch_hid_bpf_raw_requests(struct hid_device *hdev,
						unsigned char reportnum, u8 *buf,
						u32 size, enum hid_report_type rtype,
						enum hid_class_request reqtype,
						u64 source, bool from_bpf) { return 0; }
static inline int dispatch_hid_bpf_output_report(struct hid_device *hdev, __u8 *buf, u32 size,
						 u64 source, bool from_bpf) { return 0; }
static inline int hid_bpf_connect_device(struct hid_device *hdev) { return 0; }
static inline void hid_bpf_disconnect_device(struct hid_device *hdev) {}
static inline void hid_bpf_destroy_device(struct hid_device *hid) {}
static inline int hid_bpf_device_init(struct hid_device *hid) { return 0; }
static inline const u8 *call_hid_bpf_rdesc_fixup(struct hid_device *hdev, const u8 *rdesc,
						 unsigned int *size) { return rdesc; }
int hid_input_report(struct hid_device *hid, enum hid_report_type type, u8 *data, u32 size, int interrupt);
int hid_safe_input_report(struct hid_device *hid, enum hid_report_type type, u8 *data, size_t bufsize, u32 size, int interrupt);
int hid_deferred_input_report(struct hid_device *hid, enum hid_report_type type, u8 *data, size_t bufsize, u32 size, int interrupt);
void hid_output_report(struct hid_report *report, __u8 *data);
u8 *hid_alloc_report_buf(struct hid_report *report, gfp_t flags);
struct hid_report *hid_validate_values(struct hid_device *hid,
				       enum hid_report_type type, unsigned int id,
				       unsigned int field_index,
				       unsigned int report_counts);
struct hid_field *hid_find_field(struct hid_device *hdev, unsigned int report_type,
				 unsigned int application, unsigned int usage);
int hid_set_field(struct hid_field *field, unsigned offset, __s32 value);
void hid_setup_resolution_multiplier(struct hid_device *hid);
__s32 hidinput_calc_abs_res(const struct hid_field *field, __u16 code);
int hidinput_connect(struct hid_device *hid, unsigned int force);
void hidinput_disconnect(struct hid_device *hid);
void hidinput_reset_resume(struct hid_device *hid);

/**
 * hid_map_usage - map usage input bits
 *
 * @hidinput: hidinput which we are interested in
 * @usage: usage to fill in
 * @bit: pointer to input->{}bit (out parameter)
 * @max: maximal valid usage->code to consider later (out parameter)
 * @type: input event type (EV_KEY, EV_REL, ...)
 * @c: code which corresponds to this usage and type
 *
 * The value pointed to by @bit will be set to NULL if either @type is
 * an unhandled event type, or if @c is out of range for @type. This
 * can be used as an error condition.
 */
static inline void hid_map_usage(struct hid_input *hidinput,
		struct hid_usage *usage, unsigned long **bit, int *max,
		__u8 type, unsigned int c)
{
	struct input_dev *input = hidinput->input;
	unsigned long *bmap = NULL;
	unsigned int limit = 0;

	switch (type) {
	case EV_ABS:
		bmap = input->absbit;
		limit = ABS_MAX;
		break;
	case EV_REL:
		bmap = input->relbit;
		limit = REL_MAX;
		break;
	case EV_KEY:
		bmap = input->keybit;
		limit = KEY_MAX;
		break;
	case EV_LED:
		bmap = input->ledbit;
		limit = LED_MAX;
		break;
	case EV_MSC:
		bmap = input->mscbit;
		limit = MSC_MAX;
		break;
	}

	if (unlikely(c > limit || !bmap)) {
		pr_warn_ratelimited("%s: Invalid code %d type %d\n",
				    input->name, c, type);
		*bit = NULL;
		return;
	}

	usage->type = type;
	usage->code = c;
	*max = limit;
	*bit = bmap;
}

/**
 * hid_map_usage_clear - map usage input bits and clear the input bit
 *
 * @hidinput: hidinput which we are interested in
 * @usage: usage to fill in
 * @bit: pointer to input->{}bit (out parameter)
 * @max: maximal valid usage->code to consider later (out parameter)
 * @type: input event type (EV_KEY, EV_REL, ...)
 * @c: code which corresponds to this usage and type
 *
 * The same as hid_map_usage, except the @c bit is also cleared in supported
 * bits (@bit).
 */
static inline void hid_map_usage_clear(struct hid_input *hidinput,
		struct hid_usage *usage, unsigned long **bit, int *max,
		__u8 type, __u16 c)
{
	hid_map_usage(hidinput, usage, bit, max, type, c);
	if (*bit)
		clear_bit(usage->code, *bit);
}

/**
 * hid_report_len - calculate the report length
 *
 * @report: the report whose length we want to know
 *
 * The length counts the report ID byte, but only if the ID is nonzero
 * and therefore is included in the report.  Reports whose ID is zero
 * never include an ID byte.
 */
static inline u32 hid_report_len(struct hid_report *report)
{
	return DIV_ROUND_UP(report->size, 8) + (report->id > 0);
}

#endif
