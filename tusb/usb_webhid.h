#ifndef USB_WEBHID_H_
#define USB_WEBHID_H_

#include <stdbool.h>
#include <stdint.h>

#include "tusb.h"

bool webhid_is_instance(uint8_t instance);
uint16_t webhid_get_report(uint8_t report_id, hid_report_type_t report_type,
                           uint8_t* buffer, uint16_t reqlen);
void webhid_set_report(uint8_t report_id, hid_report_type_t report_type,
                       uint8_t const* buffer, uint16_t bufsize);

#endif /* USB_WEBHID_H_ */
