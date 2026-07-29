#include <linux/kfifo.h>
#include <linux/slab.h>

#define HID_KFIFO_RECORD_SIZE 2u
#define HID_KFIFO_RECORD_MAX UINT16_MAX

static unsigned int hid_kfifo_roundup_pow_of_two(unsigned int size)
{
	unsigned int rounded = 1;

	while (rounded < size)
		rounded <<= 1;
	return rounded;
}

static unsigned int hid_kfifo_len(const struct hid_kfifo_storage *fifo)
{
	return fifo->in - fifo->out;
}

static void hid_kfifo_copy_in(struct hid_kfifo_storage *fifo,
			      const void *src, unsigned int len,
			      unsigned int offset)
{
	unsigned int position = (fifo->in + offset) & (fifo->size - 1);
	unsigned int first = min(len, fifo->size - position);

	memcpy(fifo->buffer + position, src, first);
	memcpy(fifo->buffer, (const u8 *)src + first, len - first);
}

static void hid_kfifo_copy_out(const struct hid_kfifo_storage *fifo,
			       void *dst, unsigned int len,
			       unsigned int offset)
{
	unsigned int position = (fifo->out + offset) & (fifo->size - 1);
	unsigned int first = min(len, fifo->size - position);

	memcpy(dst, fifo->buffer + position, first);
	memcpy((u8 *)dst + first, fifo->buffer, len - first);
}

static unsigned int hid_kfifo_record_len(const struct hid_kfifo_storage *fifo)
{
	u8 length[HID_KFIFO_RECORD_SIZE];

	hid_kfifo_copy_out(fifo, length, sizeof(length), 0);
	return (unsigned int)length[0] | ((unsigned int)length[1] << 8);
}

int hid_kfifo_alloc(struct hid_kfifo_storage *fifo, unsigned int size,
		    gfp_t flags)
{
	unsigned int rounded = hid_kfifo_roundup_pow_of_two(size);
	u8 *buffer;

	(void)flags;
	buffer = kmalloc(rounded, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	fifo->buffer = buffer;
	fifo->size = rounded;
	fifo->in = 0;
	fifo->out = 0;
	return 0;
}

void hid_kfifo_free(struct hid_kfifo_storage *fifo)
{
	kfree(fifo->buffer);
	fifo->buffer = NULL;
	fifo->size = 0;
	fifo->in = 0;
	fifo->out = 0;
}

bool hid_kfifo_is_empty(const struct hid_kfifo_storage *fifo)
{
	return fifo->in == fifo->out;
}

unsigned int hid_kfifo_byte_in(struct hid_kfifo_storage *fifo,
			       const void *buf, unsigned int len)
{
	unsigned int avail = fifo->size - hid_kfifo_len(fifo);
	unsigned int count = min(len, avail);

	hid_kfifo_copy_in(fifo, buf, count, 0);
	fifo->in += count;
	return count;
}

unsigned int hid_kfifo_byte_out(struct hid_kfifo_storage *fifo,
				void *buf, unsigned int len)
{
	unsigned int count = min(len, hid_kfifo_len(fifo));

	hid_kfifo_copy_out(fifo, buf, count, 0);
	fifo->out += count;
	return count;
}

unsigned int hid_kfifo_record_avail(const struct hid_kfifo_storage *fifo)
{
	unsigned int unused = fifo->size - hid_kfifo_len(fifo);

	if (unused <= HID_KFIFO_RECORD_SIZE)
		return 0;
	return min(unused - HID_KFIFO_RECORD_SIZE, HID_KFIFO_RECORD_MAX);
}

unsigned int hid_kfifo_record_in(struct hid_kfifo_storage *fifo,
				 const void *buf, unsigned int len)
{
	u8 length[HID_KFIFO_RECORD_SIZE];

	if (len > hid_kfifo_record_avail(fifo))
		return 0;

	length[0] = len;
	length[1] = len >> 8;
	hid_kfifo_copy_in(fifo, length, sizeof(length), 0);
	hid_kfifo_copy_in(fifo, buf, len, sizeof(length));
	fifo->in += sizeof(length) + len;
	return len;
}

unsigned int hid_kfifo_record_out(struct hid_kfifo_storage *fifo,
				  void *buf, unsigned int len)
{
	unsigned int record_len = hid_kfifo_record_len(fifo);
	unsigned int count = min(len, record_len);

	hid_kfifo_copy_out(fifo, buf, count, HID_KFIFO_RECORD_SIZE);
	fifo->out += HID_KFIFO_RECORD_SIZE + record_len;
	return count;
}

unsigned int hid_kfifo_record_peek_len(const struct hid_kfifo_storage *fifo)
{
	return hid_kfifo_record_len(fifo);
}

void hid_kfifo_record_skip(struct hid_kfifo_storage *fifo)
{
	fifo->out += HID_KFIFO_RECORD_SIZE + hid_kfifo_record_len(fifo);
}
