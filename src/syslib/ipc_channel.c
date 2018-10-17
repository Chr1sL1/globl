#include "syslib/ipc_channel.h"
#include "syslib/misc.h"
#include "syslib/shmem.h"
#include "syslib/shm_key.h"
#include "syslib/slist.h"

#include <stdlib.h>
#include <stdio.h>

#define IPC_CHANNEL_MAGIC		(0x33554455)
#define IPC_MSG_POOL_MAGIC		(0x55335544)
#define IPC_MSG_HEADER_MAGIC	(0xfeeffeef)


#pragma pack(1)
struct ipc_msg_header
{
	unsigned int _msg_tag;
	unsigned short _prod_service_type;
	unsigned short _prod_service_idx;
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
//	unsigned int _next_idx;
};

struct ipc_channel
{
	unsigned int _magic_tag;
	unsigned int _msg_queue_len;

	volatile unsigned long _cons_ptr_head;
	volatile unsigned long _cons_ptr_tail;
	volatile unsigned long _prod_ptr_head;
	volatile unsigned long _prod_ptr_tail;

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
	unsigned int _msg_offset;
	unsigned int _msg_cnt;
	struct shmm_blk* _shm_addr;
	struct ipc_msg_free_node _msg_free_node[0];
};

struct ipc_local_port
{
	struct shmm_blk* _shm_channel;
	struct ipc_msg_pool* _msg_pool[MSG_POOL_COUNT];
};

struct ipc_channel_buf_impl
{
	struct ipc_channel_buf _the_buf;
};

static struct ipc_local_port* __the_cons_port = 0;

static inline unsigned int __aligned_msg_size(unsigned int payload_size)
{
	return (unsigned int)round_up(payload_size + sizeof(struct ipc_msg_header), 8);
}

static inline int __check_valid_channel(struct ipc_channel* channel)
{
	if(channel && channel->_magic_tag == IPC_CHANNEL_MAGIC)
		return 0;

	return -1;
}

static inline int __read_msg(struct ipc_msg_queue_node* msg_node)
{
	struct ipc_msg_header* msg_hdr;
	struct ipc_msg_pool* msg_pool;
	err_exit(msg_node->_msg_size_order >= MSG_POOL_COUNT, "invalid message size.");

	msg_pool = __the_cons_port->_msg_pool[msg_node->_msg_size_order];
	err_exit(msg_pool == 0, "invalid message pool.");
	err_exit(msg_node->_msg_block_idx >= msg_pool->_msg_cnt, "invalid message index.");

//	msg_hdr = (struct ipc_msg_header*)((char*)msg_pool + msg_pool->_msg_offset );
//	err_exit(msg_hdr->_msg_tag != IPC_MSG_HEADER_MAGIC, "invalid message");

	return 0;
error_ret:
	return -1;
}

static int __msg_pool_create(int key, unsigned int pool_idx, const struct ipc_channel_cfg* cfg)
{
	int shm_size, msg_offset;
	struct shmm_blk* sb;
	struct ipc_msg_pool* imp;

	err_exit(pool_idx > MSG_POOL_COUNT, "invalid pool index.");

	msg_offset = sizeof(struct ipc_msg_pool)
				+ cfg->message_count[pool_idx] * sizeof(struct ipc_msg_free_node);

	shm_size = msg_offset + cfg->message_count[pool_idx] * round_up(1 << (pool_idx + MIN_MSG_SIZE_ORDER), 8);

	sb = shmm_create(key, shm_size, 0);
	err_exit(!sb, "create msg pool shm failed.");

	imp = (struct ipc_msg_pool*)shmm_begin_addr(sb);

	imp->_magic_tag = IPC_MSG_POOL_MAGIC; 
	imp->_msg_cnt = cfg->message_count[pool_idx];
	imp->_msg_offset = msg_offset;

	for(int i = 0; i < imp->_msg_cnt; ++i)
	{
		imp->_msg_free_node[i]._msg_idx = i;
		imp->_msg_free_node[i]._next_free_idx = (i + 1) % imp->_msg_cnt;
	}

	return 0;
error_ret:
	return -1;
}

static struct ipc_local_port* __open_local_port(int service_type, int service_index)
{
	struct ipc_local_port* ilp;
	struct ipc_channel* channel;

	int channel_key = create_ipc_channel_key(service_type, service_index);
	err_exit(channel_key < 0, "create ipc channel key failed.");

	ilp = malloc(sizeof(struct ipc_local_port));
	err_exit(!ilp, "malloc local port failed.");

	ilp->_shm_channel = shmm_open(channel_key, 0);
	err_exit(!ilp->_shm_channel, "open ipc channel failed.");

	channel = (struct ipc_channel*)shmm_begin_addr(ilp->_shm_channel);
	err_exit(channel->_magic_tag != IPC_CHANNEL_MAGIC, "invalid ipc channel.");

	for(int i = 0; i < MSG_POOL_COUNT; ++i)
	{
		struct ipc_msg_pool* imp;
		struct shmm_blk* sb = shmm_open(channel->_key_handle[i]._key, 0);
		err_exit(!sb, "open msg pool [%d] failed.", i);

		imp = (struct ipc_msg_pool*)shmm_begin_addr(sb);
		err_exit(imp->_magic_tag != IPC_MSG_POOL_MAGIC, "invalid msg pool [%d].", i);

		imp->_shm_addr = sb;
	}

	return ilp;
error_ret:
	if(ilp->_shm_channel)
		shmm_close(ilp->_shm_channel);
	if(ilp)
		free(ilp);
	return 0;
}


static inline int __check_read(struct ipc_channel* channel)
{
	unsigned long cons_head = channel->_cons_ptr_head;
	unsigned long prod_tail = channel->_prod_ptr_tail;

	err_exit(cons_head >= prod_tail, "message queue empty.");

	return 0;
error_ret:
	return -1;
}

static inline int __check_write(struct ipc_channel* channel)
{
	unsigned long prod_head = channel->_prod_ptr_head % channel->_msg_queue_len;
	unsigned long cons_tail = channel->_cons_ptr_tail % channel->_msg_queue_len;

	err_exit(prod_head >= cons_tail, "message queue full.");

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

	ic->_cons_ptr_head = 0;
	ic->_cons_ptr_tail = 0;
	ic->_prod_ptr_head = 0;
	ic->_prod_ptr_tail = 0;
	ic->_msg_queue_len = cfg->message_queue_len;

	for(int i = 0; i < MSG_POOL_COUNT; ++i)
	{
		int key = create_msg_pool_key(cfg->cons_service_type, cfg->cons_service_index, i, 0);

		result = __msg_pool_create(key, i, cfg);
		err_exit(result < 0, "create ipc channel message pool[%d] failed.", i);

		ic->_key_handle[i]._key = key;
	}

	return 0;
error_ret:
	if(sb)
		shmm_destroy(sb);
	return -1;
}

int ipc_open_cons_port(int service_type, int service_index)
{
	if(!__the_cons_port)
		__the_cons_port = __open_local_port(service_type, service_index);

	err_exit(__the_cons_port == 0, "open cons port failed.");

	return 0;
error_ret:
	return -1;
}

struct ipc_local_port* ipc_open_prod_port(int service_type, int service_index)
{
	return __open_local_port(service_type, service_index);
}

int ipc_close_port(struct ipc_local_port* local_port)
{
	err_exit(!local_port, "");

	for(int i = 0; i < MSG_POOL_COUNT; ++i)
	{
		struct shmm_blk* sb = local_port->_msg_pool[i]->_shm_addr;
		shmm_close(sb);
	}

	free(local_port);

	if(local_port == __the_cons_port)
		__the_cons_port = 0;

	return 0;
error_ret:
	return -1;
}


int ipc_read_sc(struct ipc_channel_buf* channel_buf)
{
	struct ipc_channel* channel;
	unsigned long cons_head, cons_next;

	err_exit(!__the_cons_port, "ipc_read_sc: invalid local port.");
	err_exit(!channel_buf, "ipc_read_sc: invalid channel buf.");

	channel = (struct ipc_channel*)shmm_begin_addr(__the_cons_port->_shm_channel);
	err_exit(__check_valid_channel(channel) < 0, "ipc_read_sc: invalid ipc channel.");
	err_exit(__check_read(channel) < 0, "ipc_read_sc: can not read.");

	cons_head = channel->_cons_ptr_head;
	cons_next = cons_head + 1;

	return 0;
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


