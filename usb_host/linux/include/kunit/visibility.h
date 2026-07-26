#ifndef USB_HOST_KUNIT_VISIBILITY_H
#define USB_HOST_KUNIT_VISIBILITY_H

/*
 * KUnit is not linked in firmware. Keep the upstream export annotation
 * syntactically present without adding a test/module export subsystem.
 */
#define EXPORT_SYMBOL_IF_KUNIT(symbol)

#endif
