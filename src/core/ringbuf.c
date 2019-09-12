#include "common_types.h"
#include "core/ringbuf.h"
#include "core/misc.h"
#include "core/mmspace.h"
#include <string.h>

#define RING_BUF_TAG		(0x1212dbdb1212dbdb)
#define RBUF_ZONE_NAME		"sys_ringbuf_zone"

struct _ring_buf_impl
{
	struct ring_buf _the_buf;

	volatile i32 _r_offset;
	volatile i32 _w_offset;
};

static struct mmcache* __the_ring_buf_zone = 0;

static inline struct _ring_buf_impl* _conv_rb(struct ring_buf* rb)
{
	return (struct _ring_buf_impl*)((u64)rb - (u64)&(((struct _ring_buf_impl*)(0))->_the_buf));
}

static inline i32 _rbuf_try_restore_zone(void)
{
	if(!__the_ring_buf_zone)
	{
		__the_ring_buf_zone = mm_search_zone(RBUF_ZONE_NAME);
		if(!__the_ring_buf_zone)
		{
			__the_ring_buf_zone = mm_cache_create(RBUF_ZONE_NAME, sizeof(struct _ring_buf_impl), 0, 0);
			if(!__the_ring_buf_zone) goto error_ret;
		}
		else
		{
			if(__the_ring_buf_zone->obj_size != sizeof(struct _ring_buf_impl))
				goto error_ret;
		}
	}

	return 0;
error_ret:
	return -1;

}

struct ring_buf* rbuf_create(void* addr, i32 size)
{
	i32 rslt;
	struct _ring_buf_impl* rbi;

	if(!addr || size <= sizeof(struct _ring_buf_impl)) goto error_ret;
	if(((u64)addr & 0x7) != 0) goto error_ret;
	if((size & 0x7) != 0) goto error_ret;

	rslt = _rbuf_try_restore_zone();
	if(rslt < 0) goto error_ret;

	rbi = mm_cache_alloc(__the_ring_buf_zone);
	if(!rbi) goto error_ret;

	rbi->_r_offset = 0;
	rbi->_w_offset = 0;

	rbi->_the_buf.addr_begin = addr;
	rbi->_the_buf.size = size;

	return &rbi->_the_buf;
error_ret:
	return 0;
}

inline i32 rbuf_reset(struct ring_buf* rbuf)
{
	struct _ring_buf_impl* rbi;

	if(!rbuf) goto error_ret;
	rbi = _conv_rb(rbuf);

	rbi->_r_offset = 0;
	rbi->_w_offset = 0;

	return 0;
error_ret:
	return -1;
}

i32 rbuf_destroy(struct ring_buf* rbuf)
{
	struct _ring_buf_impl* rbi;
	if(!rbuf) goto error_ret;

	rbi = _conv_rb(rbuf);

	return mm_cache_free(__the_ring_buf_zone, rbi);
error_ret:
	return -1;
}

i32 rbuf_write_block(struct ring_buf* rbuf, const void* data, i32 datalen)
{
	i32 r_offset, w_offset;
	i32 remain;
	struct _ring_buf_impl* rbi;

	if(!rbuf) goto error_ret;

	rbi = _conv_rb(rbuf);
	if(rbi == 0 || rbi->_the_buf.addr_begin == 0) goto error_ret;

	r_offset = rbi->_r_offset;
	w_offset = rbi->_w_offset;

	if(w_offset < r_offset)
	{
		remain = r_offset - w_offset;
		if(remain < datalen) goto error_ret;

		memcpy(rbi->_the_buf.addr_begin + w_offset, data, datalen);
		w_offset += datalen;

		goto succ_ret;
	}
	else if(w_offset + datalen < r_offset + rbi->_the_buf.size)
	{
		remain = rbi->_the_buf.size - w_offset;
		if(remain >= datalen)
		{
			memcpy(rbi->_the_buf.addr_begin + w_offset, data, datalen);
			w_offset += datalen;
		}
		else
		{
			i32 remain2 = datalen - remain;
			memcpy(rbi->_the_buf.addr_begin + w_offset, data, remain);
			memcpy(rbi->_the_buf.addr_begin, data + remain, remain2);
			w_offset = remain2;
		}

		goto succ_ret;
	}

	goto error_ret;

succ_ret:
	rbi->_w_offset = w_offset;
	return 0;
error_ret:
	return -1;
}

i32 rbuf_read_block(struct ring_buf* rbuf, void* buf, i32 readlen)
{
	i32 r_offset, w_offset;
	i32 remain;

	struct _ring_buf_impl* rbi = _conv_rb(rbuf);
	if(rbi == 0 || rbi->_the_buf.addr_begin == 0) goto error_ret;

	r_offset = rbi->_r_offset;
	w_offset = rbi->_w_offset;

	if(r_offset <= w_offset)
	{
		remain = w_offset - r_offset;
		if(remain < readlen) goto error_ret;

		memcpy(buf, rbi->_the_buf.addr_begin + r_offset, readlen);
		r_offset += readlen;

		goto succ_ret;
	}
	else if(r_offset + readlen < w_offset + rbi->_the_buf.size)
	{
		remain = rbi->_the_buf.size - r_offset;
		if(remain >= readlen)
		{
			memcpy(buf, rbi->_the_buf.addr_begin + r_offset, readlen);
			r_offset += readlen;
		}
		else
		{
			i32 remain2 = readlen - remain;
			memcpy(buf, rbi->_the_buf.addr_begin + r_offset, remain);
			memcpy(buf + remain, rbi->_the_buf.addr_begin, remain2);
			r_offset = remain2;
		}
		goto succ_ret;
	}

	goto error_ret;

succ_ret:
	rbi->_r_offset = r_offset;
	return 0;
error_ret:
	return -1;
}

