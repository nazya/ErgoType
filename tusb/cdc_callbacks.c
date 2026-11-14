#include "tusb.h"

#include "ui/ui.h"

// Invoked when there is data available on CDC interface
void tud_cdc_rx_cb(uint8_t itf)
{
    // Drain and ignore incoming bytes by default.
    while (tud_cdc_n_available(itf)) {
        (void) tud_cdc_n_read_char(itf);
    }
}

// Invoked when DTR/RTS is changed
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void) itf;
    (void) rts;
    if (dtr)
        ui_notify_cdc_connected();
    else
        ui_notify_cdc_disconnected();
}
