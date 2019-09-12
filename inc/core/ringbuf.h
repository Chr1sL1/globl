#ifndef __ring_buf_h__
#define __ring_buf_h__

struct ring_buf
{
	void* addr_begin;
	u64 size;
};

struct ring_buf* rbuf_create(void* addr, i32 size);
i32 rbuf_reset(struct ring_buf* rbuf);
i32 rbuf_destroy(struct ring_buf* rbuf);

i32 rbuf_write_block(struct ring_buf* rb, const void* data, i32 datalen);
i32 rbuf_read_block(struct ring_buf* rb, void* buf, i32 buflen);


#endif //__ring_buf_h__

