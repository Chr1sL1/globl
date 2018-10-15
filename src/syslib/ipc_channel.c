#include "syslib/ipc_channel.h"
#include "syslib/misc.h"
#include "syslib/shmem.h"
#include "syslib/shm_key.h"
#include "syslib/slist.h"

#include <stdlib.h>
#include <stdio.h>

#define IPC_CHANNEL_MAGIC	(0x33554455)
#define IPC_MSG_POOL_MAGIC	(0x55335544)


#pragma pack(1)
struct ipc_msg_header
{
	unsigned int _msg_tag;
	unsigned short _src_service_type;
	unsigned short _src_service_idx;
};
#pragma pack()

struct ipc_msg_queue_node
{
	unsigned int _msg_size_order;
	unsigned int _msg_block_idx;
};

struct ipc_msg_pool_key_handle
{
	int _key;
	unsigned int _next_idx;
};

struct ipc_channel
{
	unsigned int _magic_tag;
	unsigned int _msg_queue_len;

	volatile unsigned int _read_ptr_head;
	volatile unsigned int _read_ptr_tail;
	volatile unsigned int _write_ptr_head;
	volatile unsigned int _write_ptr_tail;

	struct ipc_msg_pool_key_handle _key_handle[MSG_POOL_COUNT];

	struct ipc_msg_queue_node _msg_queue_node[0];
};

struct ipc_msg_free_node
{
	unsigned int _msg_idx;
	volatile int _next_free_idx;
};

struct ipc_msg_pool
{
	unsigned int _magic_tag;
	unsigned int _msg_size;
	unsigned int _msg_offset;
	unsigned int _msg_cnt;
	struct ipc_msg_free_node _msg_free_node[0];
};

struct ipc_local_port
{
	struct ipc_channel* _channel;
	struct ipc_msg_pool* _msg_pool[MSG_POOL_COUNT];
};

struct ipc_channel_buf_impl
{
	struct ipc_channel_buf _the_buf;
};

static inline unsigned int __aligned_msg_size(unsigned int payload_size)
{
	return (unsigned int)round_up(payload_size + sizeof(struct ipc_msg_header), 8);
}

static int _msg_pool_create(int key, unsigned int pool_idx, const struct ipc_channel_cfg* cfg)
{
	int shm_size, msg_offset;
	struct shmm_blk* sb;

	err_exit(pool_idx > MSG_POOL_COUNT, "invalid pool index.");

	msg_offset = sizeof(struct ipc_msg_pool)
				+ cfg->message_count[pool_idx] * sizeof(struct ipc_msg_free_node);

	shm_size = msg_offset + cfg->message_count[pool_idx] * (1 << (pool_idx + MIN_MSG_SIZE_ORDER));


	return 0;
error_ret:
	return -1;
}

int ipc_channel_create(const struct ipc_channel_cfg* cfg)
{
	int result;
	struct shmm_blk* sb = 0;
	struct ipc_channel* ic = 0;

	int channel_key = create_ipc_channel_key(cfg->cons_service_type, cfg->cons_service_index);
	err_exit(channel_key < 0, "create ipc channel key failed.");

	sb = shmm_create(channel_key, sizeof(struct ipc_channel) + cfg->message_queue_len * sizeof(struct ipc_msg_queue_node), 0);
	err_exit(!sb, "create ipc channel shm failed.");

	ic = (struct ipc_channel*)shmm_begin_addr(sb);

	ic->_magic_tag = IPC_CHANNEL_MAGIC;

	ic->_read_ptr_head = 0;
	ic->_read_ptr_tail = 0;
	ic->_write_ptr_head = 0;
	ic->_write_ptr_tail = 0;
	ic->_msg_queue_len = cfg->message_queue_len;

	for(int i = 0; i < MSG_POOL_COUNT; ++i)
	{
		int key = create_msg_pool_key(cfg->cons_service_type, cfg->cons_service_index, i, 0);

		result = _msg_pool_create(key, i, cfg);
		err_exit(result < 0, "create ipc channel message pool[%d] failed.", i);

		ic->_key_handle[i]._key = key;
		ic->_key_handle[i]._next_idx = 1;
	}

	return 0;
error_ret:
	if(sb)
		shmm_destroy(sb);
	return -1;
}

struct ipc_local_port* ipc_open_port(int service_type, int service_index)
{
	struct ipc_local_port* ilp;
	struct shmm_blk* shm_channel;

	int channel_key = create_ipc_channel_key(service_type, service_index);
	err_exit(channel_key < 0, "create ipc channel key failed.");

	ilp = malloc(sizeof(struct ipc_local_port));
	err_exit(!ilp, "malloc local port failed.");

	shm_channel = shmm_open(channel_key, 0);
	err_exit(!shm_channel, "open ipc channel failed.");

	ilp->_channel = (struct ipc_channel*)shmm_begin_addr(shm_channel);
	err_exit(ilp->_channel != IPC_CHANNEL_MAGIC, "invalid ipc channel.");

	return ilp;
error_ret:
	if(shm_channel)
		shmm_close(shm_channel);
	if(ilp)
		free(ilp);
	return 0;
}

int ipc_close_port(struct ipc_local_port* local_port)
{

error_ret:
	return -1;
}


int ipc_read_sc(struct ipc_local_port* local_port, struct ipc_channel_buf* channel_buf)
{

error_ret:
	return -1;
}

struct ipc_channel_buf* ipc_channel_fetch_buf(struct ipc_local_port* local_port, int size)
{

error_ret:
	return 0;
}

int ipc_write_sp(struct ipc_local_port* local_port, struct ipc_channel_buf* channel_buf)
{

error_ret:
	return -1;
}

int ipc_write_mp(struct ipc_local_port* local_port, struct ipc_channel_buf* channel_buf)
{

error_ret:
	return -1;
}





