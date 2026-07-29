#ifndef USB_HOST_LINUX_KFIFO_H
#define USB_HOST_LINUX_KFIFO_H

#include "hid_compat.h"

/*
 * Reduced Linux kfifo contract for linked HID callers:
 *
 * - struct kfifo is a dynamically allocated byte stream used by Logitech DJ;
 * - struct kfifo_rec_ptr_2 is a dynamically allocated record FIFO with the
 *   upstream two-byte length prefix used by Wacom;
 * - allocation rounds the requested capacity to a power of two, and current
 *   callers serialize access with existing locks or task/lifecycle ordering;
 * - record peek, skip, and out are called only after the caller established
 *   that the FIFO is non-empty.
 *
 * Other Linux kfifo declaration forms and operations are intentionally not
 * provided; newly linked callers must extend and re-audit this contract.
 */
struct hid_kfifo_storage {
	u8 *buffer;
	unsigned int size;
	unsigned int in;
	unsigned int out;
};

struct kfifo {
	struct hid_kfifo_storage storage;
};

struct kfifo_rec_ptr_2 {
	struct hid_kfifo_storage storage;
};

int hid_kfifo_alloc(struct hid_kfifo_storage *fifo, unsigned int size,
		    gfp_t flags);
void hid_kfifo_free(struct hid_kfifo_storage *fifo);
bool hid_kfifo_is_empty(const struct hid_kfifo_storage *fifo);
unsigned int hid_kfifo_byte_in(struct hid_kfifo_storage *fifo,
			       const void *buf, unsigned int len);
unsigned int hid_kfifo_byte_out(struct hid_kfifo_storage *fifo,
				void *buf, unsigned int len);
unsigned int hid_kfifo_record_avail(const struct hid_kfifo_storage *fifo);
unsigned int hid_kfifo_record_in(struct hid_kfifo_storage *fifo,
				 const void *buf, unsigned int len);
unsigned int hid_kfifo_record_out(struct hid_kfifo_storage *fifo,
				  void *buf, unsigned int len);
unsigned int hid_kfifo_record_peek_len(const struct hid_kfifo_storage *fifo);
void hid_kfifo_record_skip(struct hid_kfifo_storage *fifo);

#define kfifo_alloc(fifo, size, flags) \
	hid_kfifo_alloc(&(fifo)->storage, (size), (flags))
#define kfifo_free(fifo) hid_kfifo_free(&(fifo)->storage)
#define kfifo_is_empty(fifo) hid_kfifo_is_empty(&(fifo)->storage)
#define kfifo_len(fifo) ((fifo)->storage.in - (fifo)->storage.out)
#define kfifo_avail(fifo) hid_kfifo_record_avail(&(fifo)->storage)
#define kfifo_peek_len(fifo) hid_kfifo_record_peek_len(&(fifo)->storage)
#define kfifo_skip(fifo) hid_kfifo_record_skip(&(fifo)->storage)

#define kfifo_in(fifo, buf, len) \
	_Generic((fifo), \
		struct kfifo *: hid_kfifo_byte_in, \
		struct kfifo_rec_ptr_2 *: hid_kfifo_record_in \
	)(&(fifo)->storage, (buf), (len))

#define kfifo_out(fifo, buf, len) \
	_Generic((fifo), \
		struct kfifo *: hid_kfifo_byte_out, \
		struct kfifo_rec_ptr_2 *: hid_kfifo_record_out \
	)(&(fifo)->storage, (buf), (len))

#endif
