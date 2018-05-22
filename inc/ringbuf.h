#ifndef __ring_buf_h__
#define __ring_buf_h__

struct ring_buf
{
	void* addr_begin;
	unsigned long size;
};

struct ring_buf* rbuf_create(void* addr, long size);
long rbuf_reset(struct ring_buf* rbuf);
long rbuf_destroy(struct ring_buf* rbuf);

long rbuf_write_block(struct ring_buf* rb, const void* data, long datalen);
long rbuf_read_block(struct ring_buf* rb, void* buf, long buflen);


#endif //__ring_buf_h__

