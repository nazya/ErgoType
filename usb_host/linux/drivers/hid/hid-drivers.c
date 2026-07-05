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
		 * Old port path registered every imported vendor driver. Temporarily
		 * disabled while callback-driven HID host has no async driver
		 * GET_REPORT/workqueue continuation model.
		 */
		ret = hid_register_driver(*driver);
		if (ret)
			return ret;
#endif
		if (strcmp((*driver)->name, "hid-generic"))
			continue;

		ret = hid_register_driver(*driver);
		if (ret)
			return ret;
	}

	return 0;
}
