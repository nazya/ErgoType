#include "../../include/linux/hid.h"

/*
 * Upstream Linux has no single hid-drivers.c registry file: HID drivers are
 * registered by module/initcall machinery. This port has no loadable modules,
 * so linked driver descriptors are collected through linker sections here.
 */

extern const struct hid_driver * const __start_hid_drivers[];
extern const struct hid_driver * const __stop_hid_drivers[];
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
	const struct hid_driver * const *driver;
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
		ret = hid_register_driver(*driver);
		if (ret)
			return ret;
	}

	return 0;
}
