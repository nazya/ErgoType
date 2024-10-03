#include <pico/stdlib.h> 

extern QueueHandle_t eventQueue;

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
	int fd;
};

// daemon.c part
static uint8_t keystate[256];
void keyb_init();
void keys_task(void* pvParameters);
void keyd_task(void* pvParameters); 



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