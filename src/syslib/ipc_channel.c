#include "syslib/ipc_channel.h"
#include "syslib/misc.h"
#include "syslib/shmem.h"
#include "syslib/slist.h"

struct ipc_msg_free_node
{
	unsigned int _msg_idx;
	int _next_free_idx;
};

struct ipc_msg_pool
{
};

struct ipc_msg_queue_node
{
	unsigned int _msg_size_order;
	unsigned int _msg_block_idx;
};

struct ipc_channel
{
	unsigned int _magic_tag;

	volatile unsigned int _read_ptr;
	volatile unsigned int _write_ptr;

	unsigned int _msg_queue_len;
	struct ipc_msg_queue_node _msg_queue_node[0];
};

struct ipc_local_port
{

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
	int actual_size = round_up_2power(size);

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




