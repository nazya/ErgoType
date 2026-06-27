/*
 * ErgoType - TinyUSB HID backend (vkbd -> USB).
 *
 * This file intentionally contains all HID report packing + tud_* calls.
 * daemon.c stays close to upstream keyd and only talks to vkbd_*.
 */

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "tusb.h"
#include "usb_descriptors.h"

#include "log.h"
#include "keyd.h"
#include "vkbd_event.h"

#define HID_READY_WAIT_TICKS 2

#define HID_CTRL 0x1
#define HID_RIGHTCTRL 0x10
#define HID_SHIFT 0x2
#define HID_RIGHTSHIFT 0x20
#define HID_ALT 0x4
#define HID_ALT_GR 0x40
#define HID_RIGHTSUPER 0x80
#define HID_SUPER 0x8

#define HID_USAGE_PAGE_NONE            0x00
#define HID_USAGE_PAGE_GENERIC_DESKTOP 0x01
#define HID_USAGE_PAGE_KEYBOARD        0x07
#define HID_USAGE_PAGE_BUTTON          0x09
#define HID_USAGE_PAGE_TELEPHONY       0x0b
#define HID_USAGE_PAGE_CONSUMER        0x0c
#define HID_USAGE_PAGE_CAMERA          0x90
// #define HID_USAGE_PAGE_HP_VENDOR       0xff7f

#define HID_USAGE_NONE                   { HID_USAGE_PAGE_NONE,            0x0000  }
#define HID_USAGE_GENERIC_DESKTOP(usage) { HID_USAGE_PAGE_GENERIC_DESKTOP, (usage) }
#define HID_USAGE_KEYBOARD(usage)        { HID_USAGE_PAGE_KEYBOARD,        (usage) }
#define HID_USAGE_BUTTON(usage)          { HID_USAGE_PAGE_BUTTON,          (usage) }
#define HID_USAGE_TELEPHONY(usage)       { HID_USAGE_PAGE_TELEPHONY,       (usage) }
#define HID_USAGE_CONSUMER(usage)        { HID_USAGE_PAGE_CONSUMER,        (usage) }
#define HID_USAGE_CAMERA(usage)          { HID_USAGE_PAGE_CAMERA,          (usage) }
#define HID_USAGE_HP_VENDOR(usage)       HID_USAGE_NONE

typedef struct {
	uint8_t page;
	uint16_t usage;
} hid_usage_t;

static const hid_usage_t keyd_hid_table[256] = {
	[KEYD_ESC]                   = HID_USAGE_KEYBOARD(0x29),
	[KEYD_1]                     = HID_USAGE_KEYBOARD(0x1e),
	[KEYD_2]                     = HID_USAGE_KEYBOARD(0x1f),
	[KEYD_3]                     = HID_USAGE_KEYBOARD(0x20),
	[KEYD_4]                     = HID_USAGE_KEYBOARD(0x21),
	[KEYD_5]                     = HID_USAGE_KEYBOARD(0x22),
	[KEYD_6]                     = HID_USAGE_KEYBOARD(0x23),
	[KEYD_7]                     = HID_USAGE_KEYBOARD(0x24),
	[KEYD_8]                     = HID_USAGE_KEYBOARD(0x25),
	[KEYD_9]                     = HID_USAGE_KEYBOARD(0x26),
	[KEYD_0]                     = HID_USAGE_KEYBOARD(0x27),
	[KEYD_MINUS]                 = HID_USAGE_KEYBOARD(0x2d),
	[KEYD_EQUAL]                 = HID_USAGE_KEYBOARD(0x2e),
	[KEYD_BACKSPACE]             = HID_USAGE_KEYBOARD(0x2a),
	[KEYD_TAB]                   = HID_USAGE_KEYBOARD(0x2b),
	[KEYD_Q]                     = HID_USAGE_KEYBOARD(0x14),
	[KEYD_W]                     = HID_USAGE_KEYBOARD(0x1a),
	[KEYD_E]                     = HID_USAGE_KEYBOARD(0x08),
	[KEYD_R]                     = HID_USAGE_KEYBOARD(0x15),
	[KEYD_T]                     = HID_USAGE_KEYBOARD(0x17),
	[KEYD_Y]                     = HID_USAGE_KEYBOARD(0x1c),
	[KEYD_U]                     = HID_USAGE_KEYBOARD(0x18),
	[KEYD_I]                     = HID_USAGE_KEYBOARD(0x0c),
	[KEYD_O]                     = HID_USAGE_KEYBOARD(0x12),
	[KEYD_P]                     = HID_USAGE_KEYBOARD(0x13),
	[KEYD_LEFTBRACE]             = HID_USAGE_KEYBOARD(0x2f),
	[KEYD_RIGHTBRACE]            = HID_USAGE_KEYBOARD(0x30),
	[KEYD_ENTER]                 = HID_USAGE_KEYBOARD(0x28),
	[KEYD_LEFTCTRL]              = HID_USAGE_KEYBOARD(0xe0),
	[KEYD_A]                     = HID_USAGE_KEYBOARD(0x04),
	[KEYD_S]                     = HID_USAGE_KEYBOARD(0x16),
	[KEYD_D]                     = HID_USAGE_KEYBOARD(0x07),
	[KEYD_F]                     = HID_USAGE_KEYBOARD(0x09),
	[KEYD_G]                     = HID_USAGE_KEYBOARD(0x0a),
	[KEYD_H]                     = HID_USAGE_KEYBOARD(0x0b),
	[KEYD_J]                     = HID_USAGE_KEYBOARD(0x0d),
	[KEYD_K]                     = HID_USAGE_KEYBOARD(0x0e),
	[KEYD_L]                     = HID_USAGE_KEYBOARD(0x0f),
	[KEYD_SEMICOLON]             = HID_USAGE_KEYBOARD(0x33),
	[KEYD_APOSTROPHE]            = HID_USAGE_KEYBOARD(0x34),
	[KEYD_GRAVE]                 = HID_USAGE_KEYBOARD(0x35),
	[KEYD_LEFTSHIFT]             = HID_USAGE_KEYBOARD(0xe1),
	[KEYD_BACKSLASH]             = HID_USAGE_KEYBOARD(0x31),
	[KEYD_Z]                     = HID_USAGE_KEYBOARD(0x1d),
	[KEYD_X]                     = HID_USAGE_KEYBOARD(0x1b),
	[KEYD_C]                     = HID_USAGE_KEYBOARD(0x06),
	[KEYD_V]                     = HID_USAGE_KEYBOARD(0x19),
	[KEYD_B]                     = HID_USAGE_KEYBOARD(0x05),
	[KEYD_N]                     = HID_USAGE_KEYBOARD(0x11),
	[KEYD_M]                     = HID_USAGE_KEYBOARD(0x10),
	[KEYD_COMMA]                 = HID_USAGE_KEYBOARD(0x36),
	[KEYD_DOT]                   = HID_USAGE_KEYBOARD(0x37),
	[KEYD_SLASH]                 = HID_USAGE_KEYBOARD(0x38),
	[KEYD_RIGHTSHIFT]            = HID_USAGE_KEYBOARD(0xe5),
	[KEYD_KPASTERISK]            = HID_USAGE_KEYBOARD(0x55),
	[KEYD_LEFTALT]               = HID_USAGE_KEYBOARD(0xe2),
	[KEYD_SPACE]                 = HID_USAGE_KEYBOARD(0x2c),
	[KEYD_CAPSLOCK]              = HID_USAGE_KEYBOARD(0x39),
	[KEYD_F1]                    = HID_USAGE_KEYBOARD(0x3a),
	[KEYD_F2]                    = HID_USAGE_KEYBOARD(0x3b),
	[KEYD_F3]                    = HID_USAGE_KEYBOARD(0x3c),
	[KEYD_F4]                    = HID_USAGE_KEYBOARD(0x3d),
	[KEYD_F5]                    = HID_USAGE_KEYBOARD(0x3e),
	[KEYD_F6]                    = HID_USAGE_KEYBOARD(0x3f),
	[KEYD_F7]                    = HID_USAGE_KEYBOARD(0x40),
	[KEYD_F8]                    = HID_USAGE_KEYBOARD(0x41),
	[KEYD_F9]                    = HID_USAGE_KEYBOARD(0x42),
	[KEYD_F10]                   = HID_USAGE_KEYBOARD(0x43),
	[KEYD_NUMLOCK]               = HID_USAGE_KEYBOARD(0x53),
	[KEYD_SCROLLLOCK]            = HID_USAGE_KEYBOARD(0x47),
	[KEYD_KP7]                   = HID_USAGE_KEYBOARD(0x5f),
	[KEYD_KP8]                   = HID_USAGE_KEYBOARD(0x60),
	[KEYD_KP9]                   = HID_USAGE_KEYBOARD(0x61),
	[KEYD_KPMINUS]               = HID_USAGE_KEYBOARD(0x56),
	[KEYD_KP4]                   = HID_USAGE_KEYBOARD(0x5c),
	[KEYD_KP5]                   = HID_USAGE_KEYBOARD(0x5d),
	[KEYD_KP6]                   = HID_USAGE_KEYBOARD(0x5e),
	[KEYD_KPPLUS]                = HID_USAGE_KEYBOARD(0x57),
	[KEYD_KP1]                   = HID_USAGE_KEYBOARD(0x59),
	[KEYD_KP2]                   = HID_USAGE_KEYBOARD(0x5a),
	[KEYD_KP3]                   = HID_USAGE_KEYBOARD(0x5b),
	[KEYD_KP0]                   = HID_USAGE_KEYBOARD(0x62),
	[KEYD_KPDOT]                 = HID_USAGE_KEYBOARD(0x63),
	[KEYD_IS_LEVEL3_SHIFT]       = HID_USAGE_NONE, /* XKB alias; no USB HID output usage. */
	[KEYD_ZENKAKUHANKAKU]        = HID_USAGE_KEYBOARD(0x94),
	[KEYD_102ND]                 = HID_USAGE_KEYBOARD(0x64),
	[KEYD_F11]                   = HID_USAGE_KEYBOARD(0x44),
	[KEYD_F12]                   = HID_USAGE_KEYBOARD(0x45),
	[KEYD_RO]                    = HID_USAGE_KEYBOARD(0x87),
	[KEYD_KATAKANA]              = HID_USAGE_KEYBOARD(0x92),
	[KEYD_HIRAGANA]              = HID_USAGE_KEYBOARD(0x93),
	[KEYD_HENKAN]                = HID_USAGE_KEYBOARD(0x8a),
	[KEYD_KATAKANAHIRAGANA]      = HID_USAGE_KEYBOARD(0x88),
	[KEYD_MUHENKAN]              = HID_USAGE_KEYBOARD(0x8b),
	[KEYD_KPJPCOMMA]             = HID_USAGE_KEYBOARD(0x8c),
	[KEYD_KPENTER]               = HID_USAGE_KEYBOARD(0x58),
	[KEYD_RIGHTCTRL]             = HID_USAGE_KEYBOARD(0xe4),
	[KEYD_KPSLASH]               = HID_USAGE_KEYBOARD(0x54),
	[KEYD_SYSRQ]                 = HID_USAGE_KEYBOARD(0x46),
	[KEYD_RIGHTALT]              = HID_USAGE_KEYBOARD(0xe6),
	[KEYD_LINEFEED]              = HID_USAGE_NONE, /* Legacy evdev key; no USB HID output usage. */
	[KEYD_HOME]                  = HID_USAGE_KEYBOARD(0x4a),
	[KEYD_UP]                    = HID_USAGE_KEYBOARD(0x52),
	[KEYD_PAGEUP]                = HID_USAGE_KEYBOARD(0x4b),
	[KEYD_LEFT]                  = HID_USAGE_KEYBOARD(0x50),
	[KEYD_RIGHT]                 = HID_USAGE_KEYBOARD(0x4f),
	[KEYD_END]                   = HID_USAGE_KEYBOARD(0x4d),
	[KEYD_DOWN]                  = HID_USAGE_KEYBOARD(0x51),
	[KEYD_PAGEDOWN]              = HID_USAGE_KEYBOARD(0x4e),
	[KEYD_INSERT]                = HID_USAGE_KEYBOARD(0x49),
	[KEYD_DELETE]                = HID_USAGE_KEYBOARD(0x4c),
	[KEYD_MACRO]                 = HID_USAGE_NONE, /* Internal keyd action. */
	[KEYD_MUTE]                  = HID_USAGE_KEYBOARD(0x7f),
	[KEYD_VOLUMEDOWN]            = HID_USAGE_KEYBOARD(0x81),
	[KEYD_VOLUMEUP]              = HID_USAGE_KEYBOARD(0x80),
	[KEYD_POWER]                 = HID_USAGE_KEYBOARD(0x66),
	[KEYD_KPEQUAL]               = HID_USAGE_KEYBOARD(0x67),
	[KEYD_KPPLUSMINUS]           = HID_USAGE_KEYBOARD(0xd7),
	[KEYD_PAUSE]                 = HID_USAGE_KEYBOARD(0x48),
	[KEYD_SCALE]                 = HID_USAGE_CONSUMER(0x029f),
	[KEYD_KPCOMMA]               = HID_USAGE_KEYBOARD(0x85),
	[KEYD_HANGEUL]               = HID_USAGE_KEYBOARD(0x90),
	[KEYD_HANJA]                 = HID_USAGE_KEYBOARD(0x91),
	[KEYD_YEN]                   = HID_USAGE_KEYBOARD(0x89),
	[KEYD_LEFTMETA]              = HID_USAGE_KEYBOARD(0xe3),
	[KEYD_RIGHTMETA]             = HID_USAGE_KEYBOARD(0xe7),
	[KEYD_COMPOSE]               = HID_USAGE_KEYBOARD(0x65),
	[KEYD_STOP]                  = HID_USAGE_KEYBOARD(0x78),
	[KEYD_AGAIN]                 = HID_USAGE_KEYBOARD(0x79),
	[KEYD_PROPS]                 = HID_USAGE_KEYBOARD(0x76),
	[KEYD_UNDO]                  = HID_USAGE_KEYBOARD(0x7a),
	[KEYD_FRONT]                 = HID_USAGE_KEYBOARD(0x77),
	[KEYD_COPY]                  = HID_USAGE_KEYBOARD(0x7c),
	[KEYD_OPEN]                  = HID_USAGE_KEYBOARD(0x74),
	[KEYD_PASTE]                 = HID_USAGE_KEYBOARD(0x7d),
	[KEYD_FIND]                  = HID_USAGE_KEYBOARD(0x7e),
	[KEYD_CUT]                   = HID_USAGE_KEYBOARD(0x7b),
	[KEYD_HELP]                  = HID_USAGE_KEYBOARD(0x75),
	[KEYD_MENU]                  = HID_USAGE_CONSUMER(0x0040),
	[KEYD_CALC]                  = HID_USAGE_CONSUMER(0x0192),
	[KEYD_SETUP]                 = HID_USAGE_NONE, /* No generic Linux HID mapping. */
	[KEYD_SLEEP]                 = HID_USAGE_GENERIC_DESKTOP(0x0082),
	[KEYD_WAKEUP]                = HID_USAGE_GENERIC_DESKTOP(0x0083),
	[KEYD_FILE]                  = HID_USAGE_CONSUMER(0x0194),
	[KEYD_SENDFILE]              = HID_USAGE_NONE, /* No generic Linux HID mapping. */
	[KEYD_DELETEFILE]            = HID_USAGE_NONE, /* No generic Linux HID mapping. */
	[KEYD_XFER]                  = HID_USAGE_NONE, /* No generic Linux HID mapping. */
	[KEYD_SCROLL_DOWN]           = HID_USAGE_NONE, /* Internal keyd scroll action; emitted as mouse wheel. */
	[KEYD_SCROLL_UP]             = HID_USAGE_NONE, /* Internal keyd scroll action; emitted as mouse wheel. */
	[KEYD_WWW]                   = HID_USAGE_CONSUMER(0x0196),
	[KEYD_MSDOS]                 = HID_USAGE_NONE, /* No generic Linux HID mapping. */
	[KEYD_COFFEE]                = HID_USAGE_CONSUMER(0x019e),
	[KEYD_ROTATE_DISPLAY]        = HID_USAGE_NONE, /* Platform/display control; no generic Linux HID mapping. */
	[KEYD_CYCLEWINDOWS]          = HID_USAGE_NONE, /* No generic Linux HID mapping. */
	[KEYD_MAIL]                  = HID_USAGE_CONSUMER(0x018a),
	[KEYD_BOOKMARKS]             = HID_USAGE_CONSUMER(0x022a),
	[KEYD_COMPUTER]              = HID_USAGE_NONE, /* No generic Linux HID mapping. */
	[KEYD_BACK]                  = HID_USAGE_CONSUMER(0x0224),
	[KEYD_FORWARD]               = HID_USAGE_CONSUMER(0x0225),
	[KEYD_CLOSECD]               = HID_USAGE_NONE, /* No generic Linux HID mapping. */
	[KEYD_EJECTCD]               = HID_USAGE_CONSUMER(0x00b8),
	[KEYD_EJECTCLOSECD]          = HID_USAGE_NONE, /* No generic Linux HID mapping. */
	[KEYD_NEXTSONG]              = HID_USAGE_CONSUMER(0x00b5),
	[KEYD_PLAYPAUSE]             = HID_USAGE_CONSUMER(0x00cd),
	[KEYD_PREVIOUSSONG]          = HID_USAGE_CONSUMER(0x00b6),
	[KEYD_STOPCD]                = HID_USAGE_CONSUMER(0x00b7),
	[KEYD_RECORD]                = HID_USAGE_CONSUMER(0x00b2),
	[KEYD_REWIND]                = HID_USAGE_CONSUMER(0x00b4),
	[KEYD_PHONE]                 = HID_USAGE_CONSUMER(0x008c),
	[KEYD_ISO]                   = HID_USAGE_NONE, /* No generic Linux HID mapping. */
	[KEYD_CONFIG]                = HID_USAGE_CONSUMER(0x0183),
	[KEYD_HOMEPAGE]              = HID_USAGE_CONSUMER(0x0223),
	[KEYD_REFRESH]               = HID_USAGE_CONSUMER(0x0227),
	[KEYD_EXIT]                  = HID_USAGE_CONSUMER(0x0204),
	[KEYD_MOVE]                  = HID_USAGE_NONE, /* No generic Linux HID mapping. */
	[KEYD_EDIT]                  = HID_USAGE_CONSUMER(0x023d),
	[KEYD_ZOOM]                  = HID_USAGE_NONE, /* No generic Linux HID mapping. */
	[KEYD_MOUSE_BACK]            = HID_USAGE_BUTTON(0x0004),
	[KEYD_KPLEFTPAREN]           = HID_USAGE_KEYBOARD(0xb6),
	[KEYD_KPRIGHTPAREN]          = HID_USAGE_KEYBOARD(0xb7),
	[KEYD_NEW]                   = HID_USAGE_CONSUMER(0x0201),
	[KEYD_REDO]                  = HID_USAGE_CONSUMER(0x0279),
	[KEYD_F13]                   = HID_USAGE_KEYBOARD(0x68),
	[KEYD_F14]                   = HID_USAGE_KEYBOARD(0x69),
	[KEYD_F15]                   = HID_USAGE_KEYBOARD(0x6a),
	[KEYD_F16]                   = HID_USAGE_KEYBOARD(0x6b),
	[KEYD_F17]                   = HID_USAGE_KEYBOARD(0x6c),
	[KEYD_F18]                   = HID_USAGE_KEYBOARD(0x6d),
	[KEYD_F19]                   = HID_USAGE_KEYBOARD(0x6e),
	[KEYD_F20]                   = HID_USAGE_KEYBOARD(0x6f),
	[KEYD_F21]                   = HID_USAGE_KEYBOARD(0x70),
	[KEYD_F22]                   = HID_USAGE_KEYBOARD(0x71),
	[KEYD_F23]                   = HID_USAGE_KEYBOARD(0x72),
	[KEYD_F24]                   = HID_USAGE_KEYBOARD(0x73),
	[KEYD_NOOP]                  = HID_USAGE_NONE, /* Internal keyd action. */
	[KEYD_EXTERNAL_MOUSE_BUTTON] = HID_USAGE_NONE, /* Internal keyd action. */
	[KEYD_CHORD_1]               = HID_USAGE_NONE, /* Internal keyd action. */
	[KEYD_CHORD_2]               = HID_USAGE_NONE, /* Internal keyd action. */
	[KEYD_CHORD_MAX]             = HID_USAGE_NONE, /* Internal keyd action. */
	[KEYD_PLAYCD]                = HID_USAGE_CONSUMER(0x00b0),
	[KEYD_PAUSECD]               = HID_USAGE_CONSUMER(0x00b1),
	[KEYD_SCROLL_LEFT]           = HID_USAGE_NONE, /* Internal keyd scroll action; emitted as mouse wheel. */
	[KEYD_SCROLL_RIGHT]          = HID_USAGE_NONE, /* Internal keyd scroll action; emitted as mouse wheel. */
	[KEYD_DASHBOARD]             = HID_USAGE_CONSUMER(0x02a2),
	[KEYD_SUSPEND]               = HID_USAGE_NONE, /* Linux maps sleep/wakeup, not this evdev alias. */
	[KEYD_CLOSE]                 = HID_USAGE_CONSUMER(0x0203),
	[KEYD_PLAY]                  = HID_USAGE_CONSUMER(0x00b0),
	[KEYD_FASTFORWARD]           = HID_USAGE_CONSUMER(0x00b3),
	[KEYD_BASSBOOST]             = HID_USAGE_CONSUMER(0x00e5),
	[KEYD_PRINT]                 = HID_USAGE_CONSUMER(0x0208),
	[KEYD_HP]                    = HID_USAGE_HP_VENDOR(0x0070),
	[KEYD_CAMERA]                = HID_USAGE_CAMERA(0x0021),
	[KEYD_SOUND]                 = HID_USAGE_HP_VENDOR(0x0072),
	[KEYD_QUESTION]              = HID_USAGE_HP_VENDOR(0x0073),
	[KEYD_EMAIL]                 = HID_USAGE_HP_VENDOR(0x0080),
	[KEYD_CHAT]                  = HID_USAGE_CONSUMER(0x0199),
	[KEYD_SEARCH]                = HID_USAGE_CONSUMER(0x0221),
	[KEYD_CONNECT]               = HID_USAGE_HP_VENDOR(0x0083),
	[KEYD_FINANCE]               = HID_USAGE_CONSUMER(0x0191),
	[KEYD_SPORT]                 = HID_USAGE_HP_VENDOR(0x0085),
	[KEYD_SHOP]                  = HID_USAGE_HP_VENDOR(0x0086),
	[KEYD_VOICECOMMAND]          = HID_USAGE_CONSUMER(0x00cf),
	[KEYD_CANCEL]                = HID_USAGE_CONSUMER(0x025f),
	[KEYD_BRIGHTNESSDOWN]        = HID_USAGE_CONSUMER(0x0070),
	[KEYD_BRIGHTNESSUP]          = HID_USAGE_CONSUMER(0x006f),
	[KEYD_MEDIA]                 = HID_USAGE_NONE, /* No generic Linux HID mapping. */
	[KEYD_SWITCHVIDEOMODE]       = HID_USAGE_GENERIC_DESKTOP(0x00b5),
	[KEYD_KBDILLUMTOGGLE]        = HID_USAGE_CONSUMER(0x007c),
	[KEYD_KBDILLUMDOWN]          = HID_USAGE_CONSUMER(0x007a),
	[KEYD_KBDILLUMUP]            = HID_USAGE_CONSUMER(0x0079),
	[KEYD_SEND]                  = HID_USAGE_CONSUMER(0x028c),
	[KEYD_REPLY]                 = HID_USAGE_CONSUMER(0x0289),
	[KEYD_FORWARDMAIL]           = HID_USAGE_CONSUMER(0x028b),
	[KEYD_SAVE]                  = HID_USAGE_CONSUMER(0x0207),
	[KEYD_DOCUMENTS]             = HID_USAGE_CONSUMER(0x01a7),
	[KEYD_BATTERY]               = HID_USAGE_NONE, /* Platform power status key; no generic Linux HID mapping. */
	[KEYD_BLUETOOTH]             = HID_USAGE_NONE, /* Platform radio key; no generic Linux HID mapping. */
	[KEYD_WLAN]                  = HID_USAGE_NONE, /* Platform radio key; no generic Linux HID mapping. */
	[KEYD_UWB]                   = HID_USAGE_NONE, /* Platform radio key; no generic Linux HID mapping. */
	[KEYD_UNKNOWN]               = HID_USAGE_NONE, /* Explicit unknown key. */
	[KEYD_VIDEO_NEXT]            = HID_USAGE_CONSUMER(0x0082),
	[KEYD_VIDEO_PREV]            = HID_USAGE_NONE, /* No generic Linux HID mapping. */
	[KEYD_BRIGHTNESS_CYCLE]      = HID_USAGE_NONE, /* Linux exposes an evdev key, but no generic HID usage mapping. */
	[KEYD_BRIGHTNESS_AUTO]       = HID_USAGE_CONSUMER(0x0075),
	[KEYD_DISPLAY_OFF]           = HID_USAGE_NONE, /* Platform/display control; no generic Linux HID mapping. */
	[KEYD_WWAN]                  = HID_USAGE_NONE, /* Platform radio key; no generic Linux HID mapping. */
	[KEYD_RFKILL]                = HID_USAGE_GENERIC_DESKTOP(0x00c6),
	[KEYD_MICMUTE]               = HID_USAGE_TELEPHONY(0x002f),
	[KEYD_LEFT_MOUSE]            = HID_USAGE_BUTTON(0x0001),
	[KEYD_MIDDLE_MOUSE]          = HID_USAGE_BUTTON(0x0003),
	[KEYD_RIGHT_MOUSE]           = HID_USAGE_BUTTON(0x0002),
	[KEYD_MOUSE_1]               = HID_USAGE_BUTTON(0x0004),
	[KEYD_MOUSE_2]               = HID_USAGE_BUTTON(0x0005),
	[KEYD_FN]                    = HID_USAGE_NONE, /* Internal/laptop Fn key; not a USB HID output usage. */
	[KEYD_MOUSE_FORWARD]         = HID_USAGE_BUTTON(0x0005),
};

static uint8_t mods;
static uint8_t keys[6];
static uint8_t mouse_buttons;
static uint8_t nkro_keys[HID_NKRO_KEY_BYTES];

static uint8_t get_modifier(int code)
{
	switch (code) {
	case KEYD_LEFTSHIFT:
		return HID_SHIFT;
	case KEYD_RIGHTSHIFT:
		return HID_RIGHTSHIFT;
	case KEYD_LEFTCTRL:
		return HID_CTRL;
	case KEYD_RIGHTCTRL:
		return HID_RIGHTCTRL;
	case KEYD_LEFTALT:
		return HID_ALT;
	case KEYD_RIGHTALT:
		return HID_ALT_GR;
	case KEYD_LEFTMETA:
		return HID_SUPER;
	case KEYD_RIGHTMETA:
		return HID_RIGHTSUPER;
	default:
		return 0;
	}
}

static int update_modifier_state(int code, int state)
{
	uint16_t mod = get_modifier(code);

	if (!mod)
		return -1;

	if (state)
		mods |= mod;
	else
		mods &= ~mod;

	return 0;
}

static int8_t clamp_i16_to_i8(int16_t v)
{
	/*
	 * Our current HID mouse report (and TinyUSB's `tud_hid_mouse_report()`) uses
	 * 8-bit relative X/Y deltas. Clamp int16_t deltas to avoid wrap-around when
	 * the sensor reports large movements.
	 */
	if (v > 127)
		return 127;
	if (v < -127)
		return -127;
	return (int8_t)v;
}

static int update_key_state(uint16_t code, int state, uint8_t page)
{
	hid_usage_t hid = keyd_hid_table[code];

	if (hid.page != page)
		return -1;

	int set = 0;

	for (int i = 0; i < 6; i++) {
		if (keys[i] == code) {
			set = 1;
			if (state == 0)
				keys[i] = 0;
		}
	}

	if (state && !set) {
		for (int i = 0; i < 6; i++) {
			if (keys[i] == 0) {
				keys[i] = (uint8_t)code;
				break;
			}
		}
	}

	return 0;
}

static void send_boot_keyboard_report(void)
{
	uint8_t usages[6];

	for (int i = 0; i < 6; i++) {
		if (keys[i] == 0)
			usages[i] = 0;
		else
			usages[i] = keyd_hid_table[keys[i]].usage;
	}

	while (!tud_hid_n_ready(HID_KEYBOARD_INSTANCE))
		vTaskDelay(HID_READY_WAIT_TICKS);

	tud_hid_n_keyboard_report(HID_KEYBOARD_INSTANCE, 0, mods, usages);
}

static int update_nkro_key_state(uint16_t code, int state, uint8_t page)
{
	hid_usage_t hid = keyd_hid_table[code];

	if (hid.page != page)
		return -1;

	uint8_t hid_code = hid.usage;
	uint8_t byte = hid_code / 8;
	uint8_t mask = 1u << (hid_code % 8);

	if (state)
		nkro_keys[byte] |= mask;
	else
		nkro_keys[byte] &= (uint8_t)~mask;

	return 0;
}

static void send_nkro_report(void)
{
	while (!tud_hid_n_ready(HID_KEYBOARD_INSTANCE))
		vTaskDelay(HID_READY_WAIT_TICKS);

	tud_hid_n_report(HID_KEYBOARD_INSTANCE, REPORT_ID_KEYBOARD, nkro_keys, sizeof(nkro_keys));
}

static void send_consumer_report(void)
{
	uint16_t usages[6];

	for (int i = 0; i < 6; i++) {
		if (keys[i] == 0)
			usages[i] = 0;
		else
			usages[i] = keyd_hid_table[keys[i]].usage;
	}

	while (!tud_hid_n_ready(HID_KEYBOARD_INSTANCE))
		vTaskDelay(HID_READY_WAIT_TICKS);

	tud_hid_n_report(HID_KEYBOARD_INSTANCE, REPORT_ID_CONSUMER, usages, sizeof(usages));
}

static void mouse_scroll(int x, int y)
{
	while (!tud_hid_n_ready(HID_MOUSE_INSTANCE))
		vTaskDelay(HID_READY_WAIT_TICKS);

	/* TinyUSB: wheel=vertical, pan=horizontal. */
	tud_hid_n_mouse_report(HID_MOUSE_INSTANCE, 0, mouse_buttons, 0, 0, (int8_t)y, (int8_t)x);
}

static void mouse_move(int16_t x, int16_t y)
{
	while (!tud_hid_n_ready(HID_MOUSE_INSTANCE))
		vTaskDelay(HID_READY_WAIT_TICKS);

	tud_hid_n_mouse_report(
		HID_MOUSE_INSTANCE,
		0,
		mouse_buttons,
		clamp_i16_to_i8(x),
		clamp_i16_to_i8(y),
		0,
		0
	);
}

static void mouse_button(uint8_t buttons, int state)
{
	if (state)
		mouse_buttons |= buttons;
	else
		mouse_buttons &= (uint8_t)~buttons;

	while (!tud_hid_n_ready(HID_MOUSE_INSTANCE))
		vTaskDelay(HID_READY_WAIT_TICKS);

	tud_hid_n_mouse_report(HID_MOUSE_INSTANCE, 0, mouse_buttons, 0, 0, 0, 0);
}

void vkbd_hid_nkro_task(void *pvParameters)
{
	(void)pvParameters;

	vkbd_event_t event;

	while (1) {
		if (xQueueReceive(vkbd_event_queue, &event, portMAX_DELAY) != pdPASS)
			continue;

		switch (event.type) {
		case KEY_EVENT_MOUSE_BUTTON:
			mouse_button(event.buttons, event.state);
			continue;
		case KEY_EVENT_MOUSE_SCROLL:
			mouse_scroll(clamp_i16_to_i8(event.x), clamp_i16_to_i8(event.y));
			continue;
		case KEY_EVENT_MOUSE_MOVE_ABS:
			/*
			 * TODO: absolute mouse mode backend.
			 * For now treat as relative deltas.
			 */
			/* fallthrough */
		case KEY_EVENT_MOUSE_MOVE:
			mouse_move(event.x, event.y);
			continue;
		case KEY_EVENT_KEY: {
			if (update_nkro_key_state(event.code, event.state, HID_USAGE_PAGE_KEYBOARD) == 0) {
				send_nkro_report();
				continue;
			}
			if (update_key_state(event.code, event.state, HID_USAGE_PAGE_CONSUMER) == 0) {
				send_consumer_report();
				continue;
			}
		}
		default:
			continue;
		}
	}
}

void vkbd_hid_boot_task(void *pvParameters)
{
	(void)pvParameters;

	vkbd_event_t event;

	while (1) {
		if (xQueueReceive(vkbd_event_queue, &event, portMAX_DELAY) != pdPASS)
			continue;

		switch (event.type) {
		case KEY_EVENT_MOUSE_BUTTON:
			mouse_button(event.buttons, event.state);
			continue;
		case KEY_EVENT_MOUSE_SCROLL:
			mouse_scroll(clamp_i16_to_i8(event.x), clamp_i16_to_i8(event.y));
			continue;
		case KEY_EVENT_MOUSE_MOVE_ABS:
			/*
			 * TODO: absolute mouse mode backend.
			 * For now treat as relative deltas.
			 */
			/* fallthrough */
		case KEY_EVENT_MOUSE_MOVE:
			mouse_move(event.x, event.y);
			continue;
		case KEY_EVENT_KEY: {
			if (update_modifier_state(event.code, event.state) == 0) {
				send_boot_keyboard_report();
				continue;
			}
			if (update_key_state(event.code, event.state, HID_USAGE_PAGE_KEYBOARD) == 0) {
				send_boot_keyboard_report();
				continue;
			}
		}
		default:
			continue;
		}
	}
}
