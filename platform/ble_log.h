#ifndef ERGOTYPE_PLATFORM_BLE_LOG_H
#define ERGOTYPE_PLATFORM_BLE_LOG_H

#include <stddef.h>

int platform_ble_log_init(void);
void platform_ble_log_write(const char *data, size_t len);

#endif
