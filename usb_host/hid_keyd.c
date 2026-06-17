#include <stdint.h>

#include "tusb.h"

#include "keys.h"
#include "hid_keyd.h"

#define HOST_HID_CONSUMER_PLAY 0x00b0
#define HOST_HID_CONSUMER_PAUSE 0x00b1
#define HOST_HID_CONSUMER_RECORD 0x00b2
#define HOST_HID_CONSUMER_FAST_FORWARD 0x00b3
#define HOST_HID_CONSUMER_REWIND 0x00b4
#define HOST_HID_CONSUMER_EJECT 0x00b8
#define HOST_HID_CONSUMER_AC_NEW 0x0201
#define HOST_HID_CONSUMER_AC_OPEN 0x0202
#define HOST_HID_CONSUMER_AC_CLOSE 0x0203
#define HOST_HID_CONSUMER_AC_EXIT 0x0204
#define HOST_HID_CONSUMER_AC_SAVE 0x0207
#define HOST_HID_CONSUMER_AC_PRINT 0x0208
#define HOST_HID_CONSUMER_AC_PROPERTIES 0x0209
#define HOST_HID_CONSUMER_AC_UNDO 0x021a
#define HOST_HID_CONSUMER_AC_COPY 0x021b
#define HOST_HID_CONSUMER_AC_CUT 0x021c
#define HOST_HID_CONSUMER_AC_PASTE 0x021d
#define HOST_HID_CONSUMER_AC_FIND 0x021f

static const uint8_t hid_to_keyd[256] = {
	[0x04] = KEYD_A,
	[0x05] = KEYD_B,
	[0x06] = KEYD_C,
	[0x07] = KEYD_D,
	[0x08] = KEYD_E,
	[0x09] = KEYD_F,
	[0x0a] = KEYD_G,
	[0x0b] = KEYD_H,
	[0x0c] = KEYD_I,
	[0x0d] = KEYD_J,
	[0x0e] = KEYD_K,
	[0x0f] = KEYD_L,
	[0x10] = KEYD_M,
	[0x11] = KEYD_N,
	[0x12] = KEYD_O,
	[0x13] = KEYD_P,
	[0x14] = KEYD_Q,
	[0x15] = KEYD_R,
	[0x16] = KEYD_S,
	[0x17] = KEYD_T,
	[0x18] = KEYD_U,
	[0x19] = KEYD_V,
	[0x1a] = KEYD_W,
	[0x1b] = KEYD_X,
	[0x1c] = KEYD_Y,
	[0x1d] = KEYD_Z,
	[0x1e] = KEYD_1,
	[0x1f] = KEYD_2,
	[0x20] = KEYD_3,
	[0x21] = KEYD_4,
	[0x22] = KEYD_5,
	[0x23] = KEYD_6,
	[0x24] = KEYD_7,
	[0x25] = KEYD_8,
	[0x26] = KEYD_9,
	[0x27] = KEYD_0,
	[0x28] = KEYD_ENTER,
	[0x29] = KEYD_ESC,
	[0x2a] = KEYD_BACKSPACE,
	[0x2b] = KEYD_TAB,
	[0x2c] = KEYD_SPACE,
	[0x2d] = KEYD_MINUS,
	[0x2e] = KEYD_EQUAL,
	[0x2f] = KEYD_LEFTBRACE,
	[0x30] = KEYD_RIGHTBRACE,
	[0x31] = KEYD_BACKSLASH,
	[0x33] = KEYD_SEMICOLON,
	[0x34] = KEYD_APOSTROPHE,
	[0x35] = KEYD_GRAVE,
	[0x36] = KEYD_COMMA,
	[0x37] = KEYD_DOT,
	[0x38] = KEYD_SLASH,
	[0x39] = KEYD_CAPSLOCK,
	[0x3a] = KEYD_F1,
	[0x3b] = KEYD_F2,
	[0x3c] = KEYD_F3,
	[0x3d] = KEYD_F4,
	[0x3e] = KEYD_F5,
	[0x3f] = KEYD_F6,
	[0x40] = KEYD_F7,
	[0x41] = KEYD_F8,
	[0x42] = KEYD_F9,
	[0x43] = KEYD_F10,
	[0x44] = KEYD_F11,
	[0x45] = KEYD_F12,
	[0x46] = KEYD_SYSRQ,
	[0x47] = KEYD_SCROLLLOCK,
	[0x48] = KEYD_PAUSE,
	[0x49] = KEYD_INSERT,
	[0x4a] = KEYD_HOME,
	[0x4b] = KEYD_PAGEUP,
	[0x4c] = KEYD_DELETE,
	[0x4d] = KEYD_END,
	[0x4e] = KEYD_PAGEDOWN,
	[0x4f] = KEYD_RIGHT,
	[0x50] = KEYD_LEFT,
	[0x51] = KEYD_DOWN,
	[0x52] = KEYD_UP,
	[0x53] = KEYD_NUMLOCK,
	[0x54] = KEYD_KPSLASH,
	[0x55] = KEYD_KPASTERISK,
	[0x56] = KEYD_KPMINUS,
	[0x57] = KEYD_KPPLUS,
	[0x58] = KEYD_KPENTER,
	[0x59] = KEYD_KP1,
	[0x5a] = KEYD_KP2,
	[0x5b] = KEYD_KP3,
	[0x5c] = KEYD_KP4,
	[0x5d] = KEYD_KP5,
	[0x5e] = KEYD_KP6,
	[0x5f] = KEYD_KP7,
	[0x60] = KEYD_KP8,
	[0x61] = KEYD_KP9,
	[0x62] = KEYD_KP0,
	[0x63] = KEYD_KPDOT,
	[0x64] = KEYD_102ND,
	[0x65] = KEYD_COMPOSE,
	[0x66] = KEYD_POWER,
	[0x67] = KEYD_KPEQUAL,
	[0x68] = KEYD_F13,
	[0x69] = KEYD_F14,
	[0x6a] = KEYD_F15,
	[0x6b] = KEYD_F16,
	[0x6c] = KEYD_F17,
	[0x6d] = KEYD_F18,
	[0x6e] = KEYD_F19,
	[0x6f] = KEYD_F20,
	[0x70] = KEYD_F21,
	[0x71] = KEYD_F22,
	[0x72] = KEYD_F23,
	[0x73] = KEYD_F24,
	[0x74] = KEYD_OPEN,
	[0x75] = KEYD_HELP,
	[0x77] = KEYD_FRONT,
	[0x79] = KEYD_AGAIN,
	[0x7a] = KEYD_UNDO,
	[0x7b] = KEYD_CUT,
	[0x7c] = KEYD_COPY,
	[0x7d] = KEYD_PASTE,
	[0x7e] = KEYD_FIND,
	[0x7f] = KEYD_MUTE,
	[0x80] = KEYD_VOLUMEUP,
	[0x81] = KEYD_VOLUMEDOWN,
	[0x85] = KEYD_KPCOMMA,
	[0x87] = KEYD_RO,
	[0x88] = KEYD_KATAKANAHIRAGANA,
	[0x89] = KEYD_YEN,
	[0x8a] = KEYD_HENKAN,
	[0x8b] = KEYD_MUHENKAN,
	[0x90] = KEYD_HANGEUL,
	[0x91] = KEYD_HANJA,
	[0x92] = KEYD_KATAKANA,
	[0x93] = KEYD_HIRAGANA,
	[0x94] = KEYD_ZENKAKUHANKAKU,
	[0xb6] = KEYD_KPLEFTPAREN,
	[0xb7] = KEYD_KPRIGHTPAREN,
	[0xe0] = KEYD_LEFTCTRL,
	[0xe1] = KEYD_LEFTSHIFT,
	[0xe2] = KEYD_LEFTALT,
	[0xe3] = KEYD_LEFTMETA,
	[0xe4] = KEYD_RIGHTCTRL,
	[0xe5] = KEYD_RIGHTSHIFT,
	[0xe6] = KEYD_RIGHTALT,
	[0xe7] = KEYD_RIGHTMETA,
};

static const uint8_t hid_mod_to_keyd[8] = {
	KEYD_LEFTCTRL,
	KEYD_LEFTSHIFT,
	KEYD_LEFTALT,
	KEYD_LEFTMETA,
	KEYD_RIGHTCTRL,
	KEYD_RIGHTSHIFT,
	KEYD_RIGHTALT,
	KEYD_RIGHTMETA,
};

uint8_t hid_keyboard_to_keyd(uint16_t usage)
{
	return usage < 256 ? hid_to_keyd[usage] : 0;
}

uint8_t hid_modifier_to_keyd(uint8_t bit)
{
	return hid_mod_to_keyd[bit];
}

static uint8_t consumer_to_keyd(uint16_t usage)
{
	switch (usage) {
	case HID_USAGE_CONSUMER_POWER:
		return KEYD_POWER;
	case HID_USAGE_CONSUMER_SLEEP:
		return KEYD_SLEEP;
	case HID_USAGE_CONSUMER_BRIGHTNESS_INCREMENT:
		return KEYD_BRIGHTNESSUP;
	case HID_USAGE_CONSUMER_BRIGHTNESS_DECREMENT:
		return KEYD_BRIGHTNESSDOWN;
	case HOST_HID_CONSUMER_PLAY:
		return KEYD_PLAY;
	case HOST_HID_CONSUMER_PAUSE:
		return KEYD_PAUSECD;
	case HOST_HID_CONSUMER_RECORD:
		return KEYD_RECORD;
	case HOST_HID_CONSUMER_FAST_FORWARD:
		return KEYD_FASTFORWARD;
	case HOST_HID_CONSUMER_REWIND:
		return KEYD_REWIND;
	case HID_USAGE_CONSUMER_PLAY_PAUSE:
		return KEYD_PLAYPAUSE;
	case HID_USAGE_CONSUMER_SCAN_NEXT:
		return KEYD_NEXTSONG;
	case HID_USAGE_CONSUMER_SCAN_PREVIOUS:
		return KEYD_PREVIOUSSONG;
	case HID_USAGE_CONSUMER_STOP:
		return KEYD_STOPCD;
	case HOST_HID_CONSUMER_EJECT:
		return KEYD_EJECTCD;
	case HID_USAGE_CONSUMER_MUTE:
		return KEYD_MUTE;
	case HID_USAGE_CONSUMER_VOLUME_INCREMENT:
		return KEYD_VOLUMEUP;
	case HID_USAGE_CONSUMER_VOLUME_DECREMENT:
		return KEYD_VOLUMEDOWN;
	case HID_USAGE_CONSUMER_BASS_BOOST:
		return KEYD_BASSBOOST;
	case HID_USAGE_CONSUMER_AL_CONSUMER_CONTROL_CONFIGURATION:
		return KEYD_CONFIG;
	case HID_USAGE_CONSUMER_AL_EMAIL_READER:
		return KEYD_MAIL;
	case HID_USAGE_CONSUMER_AL_CALCULATOR:
		return KEYD_CALC;
	case HID_USAGE_CONSUMER_AL_LOCAL_BROWSER:
		return KEYD_WWW;
	case HOST_HID_CONSUMER_AC_NEW:
		return KEYD_NEW;
	case HOST_HID_CONSUMER_AC_OPEN:
		return KEYD_OPEN;
	case HOST_HID_CONSUMER_AC_CLOSE:
		return KEYD_CLOSE;
	case HOST_HID_CONSUMER_AC_EXIT:
		return KEYD_EXIT;
	case HOST_HID_CONSUMER_AC_SAVE:
		return KEYD_SAVE;
	case HOST_HID_CONSUMER_AC_PRINT:
		return KEYD_PRINT;
	case HOST_HID_CONSUMER_AC_PROPERTIES:
		return KEYD_PROPS;
	case HOST_HID_CONSUMER_AC_UNDO:
		return KEYD_UNDO;
	case HOST_HID_CONSUMER_AC_COPY:
		return KEYD_COPY;
	case HOST_HID_CONSUMER_AC_CUT:
		return KEYD_CUT;
	case HOST_HID_CONSUMER_AC_PASTE:
		return KEYD_PASTE;
	case HOST_HID_CONSUMER_AC_FIND:
		return KEYD_FIND;
	case HID_USAGE_CONSUMER_AC_SEARCH:
		return KEYD_SEARCH;
	case HID_USAGE_CONSUMER_AC_HOME:
		return KEYD_HOMEPAGE;
	case HID_USAGE_CONSUMER_AC_BACK:
		return KEYD_BACK;
	case HID_USAGE_CONSUMER_AC_FORWARD:
		return KEYD_FORWARD;
	case HID_USAGE_CONSUMER_AC_STOP:
		return KEYD_STOP;
	case HID_USAGE_CONSUMER_AC_REFRESH:
		return KEYD_REFRESH;
	case HID_USAGE_CONSUMER_AC_BOOKMARKS:
		return KEYD_BOOKMARKS;
	default:
		return 0;
	}
}

static uint8_t desktop_system_to_keyd(uint16_t usage)
{
	switch (usage) {
	case HID_USAGE_DESKTOP_SYSTEM_POWER_DOWN:
		return KEYD_POWER;
	case HID_USAGE_DESKTOP_SYSTEM_SLEEP:
		return KEYD_SLEEP;
	case HID_USAGE_DESKTOP_SYSTEM_HIBERNATE:
		/* Not implemented: keyd has no dedicated hibernate key. */
		return 0;
	case HID_USAGE_DESKTOP_SYSTEM_WAKE_UP:
		return KEYD_WAKEUP;
	case HID_USAGE_DESKTOP_SYSTEM_SPEAKER_MUTE:
		return KEYD_MUTE;
	case HID_USAGE_DESKTOP_SYSTEM_DISPLAY_TOGGLE_INT_EXT:
		return KEYD_SWITCHVIDEOMODE;
	default:
		return 0;
	}
}

static uint8_t mouse_button_to_keyd(uint16_t usage)
{
	switch (usage) {
	case 1:
		return KEYD_LEFT_MOUSE;
	case 2:
		return KEYD_RIGHT_MOUSE;
	case 3:
		return KEYD_MIDDLE_MOUSE;
	case 4:
		return KEYD_MOUSE_BACK;
	case 5:
		return KEYD_MOUSE_FORWARD;
	case 6:
		return KEYD_MOUSE_1;
	case 7:
		return KEYD_MOUSE_2;
	default:
		return 0;
	}
}

uint8_t hid_usage_to_keyd(uint16_t usage_page, uint16_t usage)
{
	if ((usage_page & 0xff00u) == HID_USAGE_PAGE_VENDOR) {
		/* Parsed and diffed, but vendor pages need per-device KEYD mapping. */
		(void)usage;
		return 0;
	}

	switch (usage_page) {
	case HID_USAGE_PAGE_KEYBOARD:
		return hid_keyboard_to_keyd(usage);
	case HID_USAGE_PAGE_BUTTON:
		return mouse_button_to_keyd(usage);
	case HID_USAGE_PAGE_CONSUMER:
		return consumer_to_keyd(usage);
	case HID_USAGE_PAGE_DESKTOP:
		return desktop_system_to_keyd(usage);
	case HID_USAGE_PAGE_SIMULATE:
	case HID_USAGE_PAGE_VIRTUAL_REALITY:
	case HID_USAGE_PAGE_SPORT:
	case HID_USAGE_PAGE_GAME:
	case HID_USAGE_PAGE_GENERIC_DEVICE:
	case HID_USAGE_PAGE_LED:
	case HID_USAGE_PAGE_ORDINAL:
	case HID_USAGE_PAGE_TELEPHONY:
	case HID_USAGE_PAGE_DIGITIZER:
	case HID_USAGE_PAGE_PID:
	case HID_USAGE_PAGE_UNICODE:
	case HID_USAGE_PAGE_ALPHA_DISPLAY:
	case HID_USAGE_PAGE_MEDICAL:
	case HID_USAGE_PAGE_LIGHTING_AND_ILLUMINATION:
	case HID_USAGE_PAGE_MONITOR:
	case HID_USAGE_PAGE_POWER:
	case HID_USAGE_PAGE_BARCODE_SCANNER:
	case HID_USAGE_PAGE_SCALE:
	case HID_USAGE_PAGE_MSR:
	case HID_USAGE_PAGE_CAMERA:
	case HID_USAGE_PAGE_ARCADE:
	case HID_USAGE_PAGE_FIDO:
		/* Parsed and diffed, but no KEYD semantic mapping for these HID pages yet. */
		(void)usage;
		return 0;
	default:
		return 0;
	}
}
