#include "../../include/linux/hid.h"

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
		 * active hooks are report_fixup/usage_table/input_mapping/input_mapped/
		 * input_configured/event/simple probe, async-converted Vivaldi
		 * feature_mapping, and Kye's async SET_REPORT probe path. Drivers
		 * needing raw_event/workqueue/wait/sync GET_REPORT remain out of CMake.
		 */
		ret = hid_register_driver(*driver);
		if (ret)
			return ret;
	}

	return 0;
}
