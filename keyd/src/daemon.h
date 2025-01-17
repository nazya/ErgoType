/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
 
/*
 * ErgoType - Keyboard Solutions
 *
 * © 2024 Nazarii Tupitsa (see also: LICENSE-ErgoType).
*/

#include <pico/stdlib.h> 
#include "keys.h" 

#define HID_CTRL 0x1
#define HID_RIGHTCTRL 0x10
#define HID_SHIFT 0x2
#define HID_RIGHTSHIFT 0x20
#define HID_ALT 0x4
#define HID_ALT_GR 0x40
#define HID_RIGHTSUPER 0x80
#define HID_SUPER 0x8

// extern QueueHandle_t keyd_queue;
extern QueueHandle_t keyscan_queue;

typedef struct {
    uint8_t code;  // Key code
    uint8_t state; // Key state: 1 for down, 0 for up
} key_event_t;

enum event_type {
	EV_DEV_ADD,
	EV_DEV_REMOVE,
	EV_DEV_EVENT,
	EV_FD_ACTIVITY,
	// EV_FD_ERR,
	EV_TIMEOUT,
};

struct device_event {
	enum {
		DEV_KEY,
		DEV_LED,

		DEV_MOUSE_MOVE,
		/* All absolute values are relative to a resolution of 1024x1024. */
		DEV_MOUSE_MOVE_ABS,
		DEV_MOUSE_SCROLL,

		// DEV_REMOVED,
	} type;

	uint8_t code;
	uint8_t pressed;
	uint32_t x;
	uint32_t y;
};

struct event {
	enum event_type type;
	// struct device *dev;
	struct device_event *devev;
	int timestamp;
	// int fd;
};

// daemon.c part
static uint8_t keystate[256];
void keyd_task(void* pvParameters); 
void key_event_hid_task(void *pvParameters);



// struct device {
// 	/*
// 	 * A file descriptor that can be used to monitor events subsequently read with
// 	 * device_read_event().
// 	 */
// 	int fd;

// 	uint8_t grabbed;
// 	uint8_t capabilities;
// 	uint8_t is_virtual;

// 	char id[64];
// 	char name[64];
// 	char path[256];

// 	/* Internal. */
// 	uint32_t _maxx;
// 	uint32_t _maxy;
// 	uint32_t _minx;
// 	uint32_t _miny;

// 	/* Reserved for the user. */
// 	void *data;
// };

static const uint8_t hid_table[256] = {
	[KEYD_ESC] = 0x29,
	[KEYD_1] = 0x1e,
	[KEYD_2] = 0x1f,
	[KEYD_3] = 0x20,
	[KEYD_4] = 0x21,
	[KEYD_5] = 0x22,
	[KEYD_6] = 0x23,
	[KEYD_7] = 0x24,
	[KEYD_8] = 0x25,
	[KEYD_9] = 0x26,
	[KEYD_0] = 0x27,
	[KEYD_MINUS] = 0x2d,
	[KEYD_EQUAL] = 0x2e,
	[KEYD_BACKSPACE] = 0x2a,
	[KEYD_TAB] = 0x2b,
	[KEYD_Q] = 0x14,
	[KEYD_W] = 0x1a,
	[KEYD_E] = 0x08,
	[KEYD_R] = 0x15,
	[KEYD_T] = 0x17,
	[KEYD_Y] = 0x1c,
	[KEYD_U] = 0x18,
	[KEYD_I] = 0x0c,
	[KEYD_O] = 0x12,
	[KEYD_P] = 0x13,
	[KEYD_LEFTBRACE] = 0x2f,
	[KEYD_RIGHTBRACE] = 0x30,
	[KEYD_ENTER] = 0x28,
	[KEYD_LEFTCTRL] = 0xe0,
	[KEYD_A] = 0x04,
	[KEYD_S] = 0x16,
	[KEYD_D] = 0x07,
	[KEYD_F] = 0x09,
	[KEYD_G] = 0x0a,
	[KEYD_H] = 0x0b,
	[KEYD_J] = 0x0d,
	[KEYD_K] = 0x0e,
	[KEYD_L] = 0x0f,
	[KEYD_SEMICOLON] = 0x33,
	[KEYD_APOSTROPHE] = 0x34,
	[KEYD_GRAVE] = 0x35,
	[KEYD_LEFTSHIFT] = 0xe1,
	[KEYD_BACKSLASH] = 0x31,
	[KEYD_Z] = 0x1d,
	[KEYD_X] = 0x1b,
	[KEYD_C] = 0x06,
	[KEYD_V] = 0x19,
	[KEYD_B] = 0x05,
	[KEYD_N] = 0x11,
	[KEYD_M] = 0x10,
	[KEYD_COMMA] = 0x36,
	[KEYD_DOT] = 0x37,
	[KEYD_SLASH] = 0x38,
	[KEYD_RIGHTSHIFT] = 0xe5,
	[KEYD_KPASTERISK] = 0x55,
	[KEYD_LEFTALT] = 0xe2,
	[KEYD_SPACE] = 0x2c,
	[KEYD_CAPSLOCK] = 0x39,
	[KEYD_F1] = 0x3a,
	[KEYD_F2] = 0x3b,
	[KEYD_F3] = 0x3c,
	[KEYD_F4] = 0x3d,
	[KEYD_F5] = 0x3e,
	[KEYD_F6] = 0x3f,
	[KEYD_F7] = 0x40,
	[KEYD_F8] = 0x41,
	[KEYD_F9] = 0x42,
	[KEYD_F10] = 0x43,
	[KEYD_NUMLOCK] = 0x53,
	[KEYD_SCROLLLOCK] = 0x47,
	[KEYD_KP7] = 0x5f,
	[KEYD_KP8] = 0x60,
	[KEYD_KP9] = 0x61,
	[KEYD_KPMINUS] = 0x56,
	[KEYD_KP4] = 0x5c,
	[KEYD_KP5] = 0x5d,
	[KEYD_KP6] = 0x5e,
	[KEYD_KPPLUS] = 0x57,
	[KEYD_KP1] = 0x59,
	[KEYD_KP2] = 0x5a,
	[KEYD_KP3] = 0x5b,
	[KEYD_KP0] = 0x62,
	[KEYD_KPDOT] = 0x63,
	[KEYD_ZENKAKUHANKAKU] = 0x94,
	[KEYD_102ND] = 0x64,
	[KEYD_F11] = 0x44,
	[KEYD_F12] = 0x45,
	[KEYD_RO] = 0x87,
	[KEYD_KATAKANA] = 0x92,
	[KEYD_HIRAGANA] = 0x93,
	[KEYD_HENKAN] = 0x8a,
	[KEYD_KATAKANAHIRAGANA] = 0x88,
	[KEYD_MUHENKAN] = 0x8b,
	[KEYD_KPENTER] = 0x58,
	[KEYD_RIGHTCTRL] = 0xe4,
	[KEYD_KPSLASH] = 0x54,
	[KEYD_SYSRQ] = 0x46,
	[KEYD_RIGHTALT] = 0xe6,
	[KEYD_HOME] = 0x4a,
	[KEYD_UP] = 0x52,
	[KEYD_PAGEUP] = 0x4b,
	[KEYD_LEFT] = 0x50,
	[KEYD_RIGHT] = 0x4f,
	[KEYD_END] = 0x4d,
	[KEYD_DOWN] = 0x51,
	[KEYD_PAGEDOWN] = 0x4e,
	[KEYD_INSERT] = 0x49,
	[KEYD_DELETE] = 0x4c,
	[KEYD_MUTE] = 0x7f,
	[KEYD_VOLUMEDOWN] = 0x81,
	[KEYD_VOLUMEUP] = 0x80,
	[KEYD_POWER] = 0x66,
	[KEYD_KPEQUAL] = 0x67,
	[KEYD_KPPLUSMINUS] = 0xd7,
	[KEYD_PAUSE] = 0x48,
	[KEYD_KPCOMMA] = 0x85,
	[KEYD_HANGEUL] = 0x90,
	[KEYD_HANJA] = 0x91,
	[KEYD_YEN] = 0x89,
	[KEYD_LEFTMETA] = 0xe3,
	[KEYD_RIGHTMETA] = 0xe7,
	[KEYD_COMPOSE] = 0x65,
	[KEYD_AGAIN] = 0x79,
	[KEYD_UNDO] = 0x7a,
	[KEYD_FRONT] = 0x77,
	[KEYD_COPY] = 0x7c,
	[KEYD_OPEN] = 0x74,
	[KEYD_PASTE] = 0x7d,
	[KEYD_FIND] = 0x7e,
	[KEYD_CUT] = 0x7b,
	[KEYD_HELP] = 0x75,
	[KEYD_KPLEFTPAREN] = 0xb6,
	[KEYD_KPRIGHTPAREN] = 0xb7,
	[KEYD_F13] = 0x68,
	[KEYD_F14] = 0x69,
	[KEYD_F15] = 0x6a,
	[KEYD_F16] = 0x6b,
	[KEYD_F17] = 0x6c,
	[KEYD_F18] = 0x6d,
	[KEYD_F19] = 0x6e,
	[KEYD_F20] = 0x6f,
	[KEYD_F21] = 0x70,
	[KEYD_F22] = 0x71,
	[KEYD_F23] = 0x72,
	[KEYD_F24] = 0x73,
};