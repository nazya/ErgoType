#include "tusb.h"
#include "device/dcd.h"
#include "hardware/structs/usb.h"

/*
 * TinyUSB RP2040 driver (0.18; still in master 1c03f04): dcd_remote_wakeup()
 * enables the SOF interrupt to detect the end of the wakeup, but the SOF flag
 * latched before suspend is still set. The interrupt fires at once and TinyUSB
 * reports resume before the host wakes. Reading SOF_RD clears the stale flag.
 *
 * Linked in with --wrap=dcd_remote_wakeup (see CMakeLists.txt).
 * Remove both when pico-sdk ships a TinyUSB with the fix.
 */
void __real_dcd_remote_wakeup(uint8_t rhport);

void __wrap_dcd_remote_wakeup(uint8_t rhport)
{
    (void)usb_hw->sof_rd;
    __real_dcd_remote_wakeup(rhport);
}
