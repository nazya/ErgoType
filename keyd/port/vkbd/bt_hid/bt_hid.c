#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "keyd.h"
#include "vkbd_event.h"

#if ERGOTYPE_BT_OUTPUT

#include "pico/cyw43_arch.h"

#include "btstack.h"
#include "log.h"
#include "usb_descriptors.h"

#define HID_CTRL 0x1
#define HID_RIGHTCTRL 0x10
#define HID_SHIFT 0x2
#define HID_RIGHTSHIFT 0x20
#define HID_ALT 0x4
#define HID_ALT_GR 0x40
#define HID_RIGHTSUPER 0x80
#define HID_SUPER 0x8

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

static const uint8_t hid_descriptor[] = {
	0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, REPORT_ID_KEYBOARD,
	0x75, 0x01, 0x95, 0x08, 0x05, 0x07, 0x19, 0xe0,
	0x29, 0xe7, 0x15, 0x00, 0x25, 0x01, 0x81, 0x02,
	0x75, 0x01, 0x95, 0x08, 0x81, 0x03, 0x95, 0x05,
	0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05,
	0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x03,
	0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0xff,
	0x05, 0x07, 0x19, 0x00, 0x29, 0xff, 0x81, 0x00,
	0xc0,
	0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, REPORT_ID_MOUSE,
	0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01,
	0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05,
	0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03,
	0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
	0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08,
	0x95, 0x03, 0x81, 0x06, 0x05, 0x0c, 0x0a, 0x38,
	0x02, 0x95, 0x01, 0x81, 0x06, 0xc0, 0xc0,
};

static uint8_t hid_service_buffer[500];
static uint8_t device_id_sdp_service_buffer[100];
static btstack_packet_callback_registration_t hci_event_callback_registration;

#define BT_HID_EVENT_QUEUE_LEN 16
#define BT_HID_QUEUE_SET_LEN (256 + BT_HID_EVENT_QUEUE_LEN)

enum bt_hid_event_type {
	BT_HID_EVT_READY,
	BT_HID_EVT_CONNECTED,
	BT_HID_EVT_DISCONNECTED,
	BT_HID_EVT_CAN_SEND,
};

struct bt_hid_event {
	uint8_t type;
	uint16_t cid;
};

struct bt_hid_state {
	uint16_t hid_cid;
	uint8_t send_requested;
	uint8_t pending_keyboard;
	uint8_t pending_mouse;
	uint8_t mods;
	uint8_t keys[6];
	uint8_t mouse_buttons;
	int16_t mouse_x;
	int16_t mouse_y;
	int16_t mouse_wheel;
	int16_t mouse_pan;
};

static QueueHandle_t bt_event_queue;
static QueueSetHandle_t bt_queue_set;

static int8_t clamp_i16_to_i8(int16_t v)
{
	if (v > 127)
		return 127;
	if (v < -127)
		return -127;
	return (int8_t)v;
}

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

static void update_key_state(struct bt_hid_state *state, uint16_t code, int pressed)
{
	int set = 0;
	uint8_t hid_code = hid_table[code];

	for (int i = 0; i < 6; i++) {
		if (state->keys[i] == hid_code) {
			set = 1;
			if (pressed == 0)
				state->keys[i] = 0;
		}
	}

	if (pressed && !set) {
		for (int i = 0; i < 6; i++) {
			if (state->keys[i] == 0) {
				state->keys[i] = hid_code;
				break;
			}
		}
	}
}

static int update_modifier_state(struct bt_hid_state *state, int code, int pressed)
{
	uint16_t mod = get_modifier(code);

	if (!mod)
		return -1;

	if (pressed)
		state->mods |= mod;
	else
		state->mods &= ~mod;

	return 0;
}

static void request_send(struct bt_hid_state *state)
{
	if (!state->hid_cid || state->send_requested || (!state->pending_keyboard && !state->pending_mouse))
		return;

	state->send_requested = 1;
	hid_device_request_can_send_now_event(state->hid_cid);
}

static void send_ready_report(struct bt_hid_state *state)
{
	uint8_t message[10];
	uint16_t len = 0;

	state->send_requested = 0;

	if (state->pending_keyboard) {
		message[0] = 0xa1;
		message[1] = REPORT_ID_KEYBOARD;
		message[2] = state->mods;
		message[3] = 0;
		memcpy(&message[4], state->keys, sizeof(state->keys));
		state->pending_keyboard = 0;
		len = 10;
	} else if (state->pending_mouse) {
		int8_t x = clamp_i16_to_i8(state->mouse_x);
		int8_t y = clamp_i16_to_i8(state->mouse_y);
		int8_t wheel = clamp_i16_to_i8(state->mouse_wheel);
		int8_t pan = clamp_i16_to_i8(state->mouse_pan);

		state->mouse_x = (int16_t)(state->mouse_x - x);
		state->mouse_y = (int16_t)(state->mouse_y - y);
		state->mouse_wheel = (int16_t)(state->mouse_wheel - wheel);
		state->mouse_pan = (int16_t)(state->mouse_pan - pan);

		message[0] = 0xa1;
		message[1] = REPORT_ID_MOUSE;
		message[2] = state->mouse_buttons;
		message[3] = (uint8_t)x;
		message[4] = (uint8_t)y;
		message[5] = (uint8_t)wheel;
		message[6] = (uint8_t)pan;
		state->pending_mouse = state->mouse_x || state->mouse_y || state->mouse_wheel || state->mouse_pan;
		len = 7;
	}

	if (len)
		hid_device_send_interrupt_message(state->hid_cid, message, len);

	request_send(state);
}

static void queue_bt_event(uint8_t type, uint16_t cid)
{
	struct bt_hid_event event = {
		.type = type,
		.cid = cid,
	};
	xQueueSendToBack(bt_event_queue, &event, 0);
}

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t packet_size)
{
	(void)channel;
	(void)packet_size;

	if (packet_type != HCI_EVENT_PACKET)
		return;

	switch (hci_event_packet_get_type(packet)) {
	case BTSTACK_EVENT_STATE:
		if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING)
			queue_bt_event(BT_HID_EVT_READY, 0);
		return;
	case HCI_EVENT_PIN_CODE_REQUEST: {
		bd_addr_t addr;
		hci_event_pin_code_request_get_bd_addr(packet, addr);
		gap_pin_code_response(addr, "0000");
		return;
	}
	case HCI_EVENT_USER_CONFIRMATION_REQUEST: {
		bd_addr_t addr;
		hci_event_user_confirmation_request_get_bd_addr(packet, addr);
		gap_ssp_confirmation_response(addr);
		return;
	}
	case HCI_EVENT_HID_META:
		switch (hci_event_hid_meta_get_subevent_code(packet)) {
		case HID_SUBEVENT_CONNECTION_OPENED:
			if (hid_subevent_connection_opened_get_status(packet) == ERROR_CODE_SUCCESS)
				queue_bt_event(BT_HID_EVT_CONNECTED, hid_subevent_connection_opened_get_hid_cid(packet));
			return;
		case HID_SUBEVENT_CONNECTION_CLOSED:
			queue_bt_event(BT_HID_EVT_DISCONNECTED, 0);
			return;
		case HID_SUBEVENT_CAN_SEND_NOW:
			queue_bt_event(BT_HID_EVT_CAN_SEND, 0);
			return;
		default:
			return;
		}
	default:
		return;
	}
}

bool vkbd_bt_hid_init(void)
{
	if (cyw43_arch_init() != 0)
		return false;

	bt_event_queue = xQueueCreate(BT_HID_EVENT_QUEUE_LEN, sizeof(struct bt_hid_event));
	bt_queue_set = xQueueCreateSet(BT_HID_QUEUE_SET_LEN);
	if (!bt_event_queue || !bt_queue_set)
		return false;

	configASSERT(xQueueAddToSet(vkbd_event_queue, bt_queue_set) == pdPASS);
	configASSERT(xQueueAddToSet(bt_event_queue, bt_queue_set) == pdPASS);

	gap_discoverable_control(1);
	gap_set_class_of_device(0x25c0);
	gap_set_local_name("ErgoType BT 00:00:00:00:00:00");
	gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE);
	gap_set_allow_role_switch(true);
	gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);

	l2cap_init();
	sdp_init();

	hid_sdp_record_t hid_params = {
		0x25c0,
		33,
		0,
		1,
		1,
		1,
		0,
		1600,
		3200,
		3200,
		hid_descriptor,
		sizeof(hid_descriptor),
		"ErgoType BT"
	};

	memset(hid_service_buffer, 0, sizeof(hid_service_buffer));
	hid_create_sdp_record(hid_service_buffer, sdp_create_service_record_handle(), &hid_params);
	btstack_assert(de_get_len(hid_service_buffer) <= sizeof(hid_service_buffer));
	sdp_register_service(hid_service_buffer);

	device_id_create_sdp_record(
		device_id_sdp_service_buffer,
		sdp_create_service_record_handle(),
		DEVICE_ID_VENDOR_ID_SOURCE_USB,
		USB_VID,
		USB_PID_HID,
		USB_DEVICE_BCD
	);
	btstack_assert(de_get_len(device_id_sdp_service_buffer) <= sizeof(device_id_sdp_service_buffer));
	sdp_register_service(device_id_sdp_service_buffer);

	hid_device_init(0, sizeof(hid_descriptor), hid_descriptor);

	hci_event_callback_registration.callback = &packet_handler;
	hci_add_event_handler(&hci_event_callback_registration);
	hid_device_register_packet_handler(&packet_handler);

	hci_power_control(HCI_POWER_ON);
	return true;
}

void vkbd_bt_hid_task(void *pvParameters)
{
	(void)pvParameters;

	struct bt_hid_state state = {0};

	while (1) {
		QueueSetMemberHandle_t ready = xQueueSelectFromSet(bt_queue_set, portMAX_DELAY);

		if (ready == bt_event_queue) {
			struct bt_hid_event event;

			(void)xQueueReceive(bt_event_queue, &event, 0);
			switch (event.type) {
			case BT_HID_EVT_READY:
				msg("bluetooth HID ready");
				break;
			case BT_HID_EVT_CONNECTED:
				state.hid_cid = event.cid;
				state.send_requested = 0;
				msg("bluetooth HID connected");
				request_send(&state);
				break;
			case BT_HID_EVT_DISCONNECTED:
				state.hid_cid = 0;
				state.send_requested = 0;
				msg("bluetooth HID disconnected");
				break;
			case BT_HID_EVT_CAN_SEND:
				if (state.hid_cid)
					send_ready_report(&state);
				else
					state.send_requested = 0;
				break;
			default:
				break;
			}
			continue;
		}

		key_event_t event;

		(void)xQueueReceive(vkbd_event_queue, &event, 0);
		switch (event.type) {
		case KEY_EVENT_MOUSE_BUTTON:
			if (event.state)
				state.mouse_buttons |= event.buttons;
			else
				state.mouse_buttons &= (uint8_t)~event.buttons;
			state.pending_mouse = 1;
			break;
		case KEY_EVENT_MOUSE_SCROLL:
			state.mouse_pan = (int16_t)(state.mouse_pan + event.x);
			state.mouse_wheel = (int16_t)(state.mouse_wheel + event.y);
			state.pending_mouse = 1;
			break;
		case KEY_EVENT_MOUSE_MOVE_ABS:
		case KEY_EVENT_MOUSE_MOVE:
			state.mouse_x = (int16_t)(state.mouse_x + event.x);
			state.mouse_y = (int16_t)(state.mouse_y + event.y);
			state.pending_mouse = 1;
			break;
		case KEY_EVENT_KEY:
			if (update_modifier_state(&state, event.code, event.state) < 0)
				update_key_state(&state, event.code, event.state);
			state.pending_keyboard = 1;
			break;
		default:
			break;
		}

		request_send(&state);
	}
}

#else

bool vkbd_bt_hid_init(void)
{
	return false;
}

void vkbd_bt_hid_task(void *pvParameters)
{
	(void)pvParameters;
	vTaskDelete(NULL);
}

#endif
