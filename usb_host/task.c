#include "bsp/board.h"
#include "pio_usb.h"
#include "tusb.h"

#include "FreeRTOS.h"
#include "task.h"

void tusb_host_task(void *pvParameters)
{
	(void)pvParameters;

	tusb_rhport_init_t host_init = {
		.role = TUSB_ROLE_HOST,
		.speed = TUSB_SPEED_AUTO,
	};
	pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;

	pio_cfg.pin_dp = PICO_DEFAULT_PIO_USB_DP_PIN;
	tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
	tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT);
	(void)tusb_init(BOARD_TUH_RHPORT, &host_init);

	while (1)
		tuh_task();
}
