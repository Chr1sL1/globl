#include "syslib/ipc.h"
#include "syslib/shmem.h"
#include "syslib/ringbuf.h"
#include "syslib/mmspace.h"

#include <stdlib.h>

#define IPC_ZONE_NAME "sys_ipc_zone"

struct _ipc_peer_impl
{
	struct ipc_peer _the_peer;
	struct shmm_blk* _shb;
	struct ring_buf* _rb;
};

static struct mmcache* __the_ipc_zone = 0;

static inline struct _ipc_peer_impl* _conv_peer(struct ipc_peer* pr)
{
	return (struct _ipc_peer_impl*)((unsigned long)pr - (unsigned long)&(((struct _ipc_peer_impl*)(0))->_the_peer));
}

static inline long _ipc_try_restore_zone(void)
{
	if(!__the_ipc_zone)
	{
		__the_ipc_zone = mm_search_zone(IPC_ZONE_NAME);
		if(!__the_ipc_zone)
		{
			__the_ipc_zone = mm_cache_create(IPC_ZONE_NAME, sizeof(struct _ipc_peer_impl), 0, 0);
			if(!__the_ipc_zone) goto error_ret;
		}
		else
		{
			if(__the_ipc_zone->obj_size != sizeof(struct _ipc_peer_impl))
				goto error_ret;
		}
	}

	return 0;
error_ret:
	return -1;

}

struct ipc_peer* ipc_create(int channel_id, unsigned long buffer_size, int use_huge_tlb)
{
	long rslt;
	int shmm_key = -1;
	void* addr_begin;
	void* addr_end;
	struct _ipc_peer_impl* ipi = 0;
//	union shmm_sub_key sub_key;

	if(channel_id < 0 || buffer_size == 0)
		goto error_ret;

	rslt = _ipc_try_restore_zone();
	if(rslt < 0) goto error_ret;

	ipi = mm_cache_alloc(__the_ipc_zone);
	if(!ipi) goto error_ret;

//	sub_key.ipc_channel = channel_id;

//	shmm_key = mm_create_shm_key(MM_SHM_IPC, 0, &sub_key);
	if(shmm_key < 0) goto error_ret;

	ipi->_shb = shmm_create(shmm_key, buffer_size, use_huge_tlb);
	if(!ipi->_shb) goto error_ret;

	addr_begin = shmm_begin_addr(ipi->_shb);
	addr_end = shmm_end_addr(ipi->_shb);

	ipi->_rb = rbuf_create(addr_begin, addr_end - addr_begin);
	if(!ipi->_rb) goto error_ret;

	ipi->_the_peer.channel_id = channel_id;
	ipi->_the_peer.buffer_size = addr_end - addr_begin;

	return &ipi->_the_peer;
error_ret:
	if(ipi)
	{
		if(ipi->_rb)
			rbuf_destroy(ipi->_rb);
		if(ipi->_shb)
			shmm_destroy(ipi->_shb);

		mm_cache_free(__the_ipc_zone, ipi);
	}
	return 0;
}

struct ipc_peer* ipc_link(int channel_id)
{
	long rslt;
	int shmm_key = -1;
	void* addr_begin;
	void* addr_end;
	struct _ipc_peer_impl* ipi = 0;

	if(channel_id < 0)
		goto error_ret;

	rslt = _ipc_try_restore_zone();
	if(rslt < 0) goto error_ret;

	ipi = mm_cache_alloc(__the_ipc_zone);
	if(!ipi) goto error_ret;

//	sub_key.ipc_channel = channel_id;

//	shmm_key = mm_create_shm_key(MM_SHM_IPC, 0, &sub_key);
	if(shmm_key < 0) goto error_ret;

	ipi->_shb = shmm_open(shmm_key, 0);
	if(!ipi->_shb) goto error_ret;

	addr_begin = shmm_begin_addr(ipi->_shb);
	addr_end = shmm_end_addr(ipi->_shb);

	ipi->_rb = rbuf_create(addr_begin, addr_end - addr_begin);
	if(!ipi->_rb) goto error_ret;

	ipi->_the_peer.channel_id = channel_id;
	ipi->_the_peer.buffer_size = addr_end - addr_begin;

	return &ipi->_the_peer;
error_ret:
	if(ipi)
	{
		if(ipi->_rb)
			rbuf_destroy(ipi->_rb);
		if(ipi->_shb)
			shmm_close(ipi->_shb);

		mm_cache_free(__the_ipc_zone, ipi);
	}
	return 0;
}

long ipc_unlink(struct ipc_peer* pr)
{
	struct _ipc_peer_impl* ipi = _conv_peer(pr);
//	if(!ipi) goto error_ret;

//	if(!ipi->_rb || !ipi->_shb)
//		goto error_ret;
//
//	rbuf_close(&ipi->_rb);
//	shmm_close(ipi->_shb);
//
//	free(*pr);
//	*pr = 0;

	return 0;
error_ret:
//	if(*pr)
//		free(*pr);
//	*pr = 0;
	return -1;
}


long ipc_destroy(struct ipc_peer* pr)
{
	struct _ipc_peer_impl* ipi = _conv_peer(pr);
	if(!ipi) goto error_ret;

//	if(!ipi->_rb || !ipi->_shb)
//		goto error_ret;
//
//	rbuf_del(&ipi->_rb);
//	shmm_destroy(ipi->_shb);
//
//	free(*pr);
//	*pr = 0;

	return 0;
error_ret:
//	if(*pr)
//		free(*pr);
//	*pr = 0;
	return -1;
}

long ipc_write(struct ipc_peer* pr, const void* buff, long size)
{
	struct _ipc_peer_impl* ipi = _conv_peer(pr);
	if(!ipi) goto error_ret;

	if(!ipi->_rb || !ipi->_shb)
		goto error_ret;

	return rbuf_write_block(ipi->_rb, buff, size);
error_ret:
	return -1;
}

long ipc_read(struct ipc_peer* pr, void* buff, long size)
{
	struct _ipc_peer_impl* ipi = _conv_peer(pr);
	if(!ipi) goto error_ret;

	if(!ipi->_rb || !ipi->_shb)
		goto error_ret;

	return rbuf_read_block(ipi->_rb, buff, size);
error_ret:
	return -1;
}


