#ifndef USB_HOST_LINUX_TYPES_H
#define USB_HOST_LINUX_TYPES_H

/* Port UAPI types must not pull hid_compat.h and its Linux struct device. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef int8_t __s8;
typedef int16_t __s16;
typedef int32_t __s32;
typedef int64_t __s64;
typedef uint16_t __le16;
typedef unsigned long __kernel_ulong_t;
typedef unsigned long kernel_ulong_t;
typedef unsigned int umode_t;

#define __BITS_PER_LONG (__SIZEOF_LONG__ * 8)
#define __user

#endif
