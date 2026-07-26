#ifndef USB_HOST_LINUX_STRING_H
#define USB_HOST_LINUX_STRING_H

#include "hid_compat.h"

/**
 * strstarts - does @str start with @prefix?
 * @str: string to examine
 * @prefix: prefix to look for.
 *
 * Returns:
 * True if @str begins with @prefix. False in all other cases.
 */
static inline bool strstarts(const char *str, const char *prefix)
{
	return strncmp(str, prefix, strlen(prefix)) == 0;
}

#endif
