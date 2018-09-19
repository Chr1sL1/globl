#include "syslib/ipc_channel.h"
#include "syslib/misc.h"
#include "syslib/shmem.h"

struct ipc_channel
{
	struct shmm_blk*  _shm;

};

struct ipc_channel_buf_impl
{
	struct ipc_channel_buf _the_buf;
};

int ipc_channel_create(const struct ipc_channel_cfg* cfg)
{

error_ret:
	return -1;
}

struct ipc_channel* ipc_channel_open(int channel_id)
{

error_ret:
	return 0;
}

int ipc_channel_close(struct ipc_channel* channel)
{
	
error_ret:
	return -1;
}

int ipc_channel_read_sc(struct ipc_channel* channel, char** buf, int* size)
{

error_ret:
	return -1;
}

struct ipc_channel_buf* ipc_channel_fetch_buf(struct ipc_channel* channel, int size)
{

error_ret:
	return 0;
}

int ipc_channel_write_sp(struct ipc_channel* channel, struct ipc_channel_buf* channel_buf)
{

error_ret:
	return -1;
}

int ipc_channel_write_mp(struct ipc_channel* channel, struct ipc_channel_buf* channel_buf)
{

error_ret:
	return -1;
}




