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
	int ret;

	for (initcall = __start_linux_initcalls; initcall < __stop_linux_initcalls; initcall++) {
		if (!*initcall)
			continue;
		ret = (*initcall)();
		if (ret)
			return ret;
	}

	return 0;
}

int hid_builtin_drivers_init(void)
{
	const struct hid_builtin_driver *driver;
	int ret;

	for (driver = __start_hid_drivers; driver < __stop_hid_drivers; driver++) {
#if 0
		/*
		 * Earlier WIP tried to make hid-drivers.c decide which imported
		 * drivers are safe. Keep selection in CMake instead, so this loop
		 * stays Linux-shaped and registers the linked allowlist only.
		 */
		ret = hid_register_driver(*driver);
		if (ret)
			return ret;
#endif
		/*
		 * This source slice only links hid-generic plus vendor drivers whose
		 * active hooks are covered by the current port layers selected in
		 * CMake: input mapping/report fixups, callback-safe raw_event handlers,
		 * report GET/SET async paths, hiddev proxy users, and timer/workqueue
		 * users that do not need deferred Linux subsystem proxies.
		 */
		// ret = hid_register_driver(*driver);
		// The linker entry now pairs the descriptor with runtime storage looked up by hid-core.
		ret = hid_register_driver(driver->hid_driver);
		if (ret)
			return ret;
	}

	return 0;
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
