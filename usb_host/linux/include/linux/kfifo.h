#ifndef USB_HOST_LINUX_KFIFO_H
#define USB_HOST_LINUX_KFIFO_H

#include "hid_compat.h"

/*
 * PORTING DEBT: no linked source uses this reduced byte FIFO. It omits
 * Linux kfifo's locking variants, power-of-two sizing contract, and broader
 * typed API; audit the prospective caller before this header becomes active.
 */
struct kfifo {
	u8 *buffer;
	unsigned int size;
	unsigned int in;
	unsigned int out;
};

static inline int kfifo_alloc(struct kfifo *fifo, unsigned int size, gfp_t flags)
{
	(void)flags;
	fifo->buffer = kmalloc(size, GFP_KERNEL);
	if (!fifo->buffer)
		return -ENOMEM;

	fifo->size = size;
	fifo->in = 0;
	fifo->out = 0;
	return 0;
}

static inline void kfifo_free(struct kfifo *fifo)
{
	kfree(fifo->buffer);
	fifo->buffer = NULL;
	fifo->size = 0;
	fifo->in = 0;
	fifo->out = 0;
}

static inline unsigned int kfifo_len(struct kfifo *fifo)
{
	return fifo->in - fifo->out;
}

static inline bool kfifo_is_empty(struct kfifo *fifo)
{
	return kfifo_len(fifo) == 0;
}

static inline unsigned int kfifo_in(struct kfifo *fifo, const void *buf, unsigned int len)
{
	const u8 *src = buf;
	unsigned int written = 0;

	while (written < len && kfifo_len(fifo) < fifo->size) {
		fifo->buffer[fifo->in % fifo->size] = src[written++];
		fifo->in++;
	}

	return written;
}

static inline unsigned int kfifo_out(struct kfifo *fifo, void *buf, unsigned int len)
{
	u8 *dst = buf;
	unsigned int read = 0;

	while (read < len && !kfifo_is_empty(fifo)) {
		dst[read++] = fifo->buffer[fifo->out % fifo->size];
		fifo->out++;
	}

	return read;
}

#endif
