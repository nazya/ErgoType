#include "../../include/linux/hid.h"

/*
 * Upstream Linux has no single hid-drivers.c registry file: HID drivers are
 * registered by module/initcall machinery. This port has no loadable modules,
 * so linked driver descriptors are collected through linker sections here.
 */

// Each linker entry pairs a flash-resident descriptor with mutable .bss runtime
// storage, avoiding one FreeRTOS heap allocation per builtin HID driver.
extern const struct hid_builtin_driver __start_hid_drivers[];
extern const struct hid_builtin_driver __stop_hid_drivers[];
extern linux_initcall_t const __start_linux_initcalls[];
extern linux_initcall_t const __stop_linux_initcalls[];

static linux_initcall_t const linux_initcall_sentinel
__attribute__((used, section("linux_initcalls"))) = NULL;

int linux_module_initcalls_init(void)
{
	linux_initcall_t const *initcall;
	int first_error = 0;
	int ret;

	for (initcall = __start_linux_initcalls; initcall < __stop_linux_initcalls; initcall++) {
		if (!*initcall)
			continue;
		ret = (*initcall)();
		/* Linux records a failed initcall and continues booting the others. */
		if (ret && !first_error)
			first_error = ret;
	}

	return first_error;
}

int hid_builtin_drivers_init(void)
{
	const struct hid_builtin_driver *driver;
	int first_error = 0;
	int ret;

	for (driver = __start_hid_drivers; driver < __stop_hid_drivers; driver++) {
		/*
		 * This source slice only links hid-generic plus vendor drivers whose
		 * active hooks are covered by the current port layers selected in
		 * CMake: input mapping/report fixups, task-owned raw_event handlers,
		 * report GET/SET async paths, and bounded timer/workqueue users. Hiddev
		 * proxy users and drivers needing deferred subsystem proxies stay unlinked.
		 */
		// ret = hid_register_driver(*driver);
		// The linker entry now pairs the descriptor with runtime storage looked up by hid-core.
		ret = hid_register_driver(driver->hid_driver);
		/* One failed Linux module must not suppress unrelated HID drivers. */
		if (ret && !first_error)
			first_error = ret;
	}

	return first_error;
}

// Upstream Linux: no equivalent; registration and unregister use this port
// boundary to find linker-owned runtime storage without changing their API.
struct hid_driver_runtime *hid_builtin_driver_runtime(const struct hid_driver *hid_driver)
{
	const struct hid_builtin_driver *driver;

	for (driver = __start_hid_drivers; driver < __stop_hid_drivers; driver++)
		if (driver->hid_driver == hid_driver)
			return driver->runtime;

	return NULL;
}
