#include "syslib/ipc_channel.h"
#include "syslib/misc.h"
#include "syslib/shmem.h"
#include "syslib/shm_key.h"
#include "syslib/slist.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define IPC_CHANNEL_MAGIC		(0x33554455)
#define IPC_MSG_POOL_MAGIC		(0x55335544)
#define IPC_MSG_HEADER_MAGIC	(0xfeeffeef)

#pragma pack(1)
struct ipc_msg_header
{
	unsigned int _msg_tag;
	unsigned int _msg_size;
	unsigned int _msg_idx;

	unsigned _prod_service_type : SHM_SERVICE_TYPE_BITS;
	unsigned _prod_service_index : SHM_SERVICE_INDEX_BITS;
	unsigned _cons_service_type : SHM_SERVICE_TYPE_BITS;
	unsigned _cons_service_index : SHM_SERVICE_INDEX_BITS;
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
	unsigned int _msg_order;
	unsigned int _msg_cnt;
	volatile unsigned int _free_msg_head;
	volatile unsigned int _free_msg_tail;
	unsigned int _reserved;
	void* _guard_msg_hdr;
	struct ipc_msg_free_node _free_node_list[0];
};

struct ipc_local_port
{
	unsigned short _service_type;
	unsigned short _service_idx;
	unsigned int _reserved;
	struct shmm_blk* _shm_channel;
	struct shmm_blk* _shm_msg_pool[MSG_POOL_COUNT];
	struct ipc_msg_pool* _msg_pool[MSG_POOL_COUNT];
};

static struct ipc_local_port* __the_cons_port = 0;
static ipc_read_func_t __cons_read_func;

static inline unsigned int __aligned_msg_size(unsigned int payload_size)
{
	return (unsigned int)round_up(payload_size + sizeof(struct ipc_msg_header), 8);
}

static inline unsigned int __aligned_msg_size_order(unsigned int payload_size)
{
	return log_2(__aligned_msg_size(payload_size)) + 1;
}

static inline int __check_valid_channel(struct ipc_channel* channel)
{
	if(channel && channel->_magic_tag == IPC_CHANNEL_MAGIC)
		return 0;

	return -1;
}

static inline int __cas32(volatile unsigned int* dst, unsigned int expected, unsigned int src)
{
	char result;
	asm volatile("lock; cmpxchg %3, %1; sete %0" : "=q"(result), "+m"(*dst), "+a"(expected) : "r"(src) : "memory", "cc");
	return result;
}

static inline int __cas64(volatile unsigned long* dst, unsigned long expected, unsigned long src)
{
	char result;
	asm volatile("lock; cmpxchg %3, %1; sete %0" : "=q"(result), "+m"(*dst), "+a"(expected) : "r"(src) : "memory", "cc");
	return result;
}

static inline struct ipc_msg_pool* __get_msg_pool(struct ipc_local_port* port, unsigned int msg_size_order)
{
	struct ipc_msg_pool* imp;
	err_exit(msg_size_order >= MSG_POOL_COUNT, "");

	imp = (struct ipc_msg_pool*)shmm_begin_addr(port->_shm_msg_pool[msg_size_order - MIN_MSG_SIZE_ORDER]);
	err_exit(imp->_magic_tag != IPC_MSG_POOL_MAGIC, "invalid size order [%d].", msg_size_order);

	return imp;
error_ret:
	return 0;
}

static inline struct ipc_msg_header* __get_msg(struct ipc_msg_pool* msg_pool, unsigned int msg_idx)
{
	struct ipc_msg_header* msg_hdr;

	msg_hdr = (struct ipc_msg_header*)(msg_pool->_guard_msg_hdr + (1 << msg_pool->_msg_order) * msg_idx);

	err_exit(msg_hdr->_msg_tag != IPC_MSG_HEADER_MAGIC, "invalid addr.");
	err_exit(msg_hdr->_msg_idx != msg_idx, "invalid idx.");

	return msg_hdr;
error_ret:
	return 0;
}

static inline struct ipc_msg_header* __read_msg(struct ipc_msg_queue_node* msg_node)
{
	struct ipc_msg_header* msg_hdr;
	struct ipc_msg_pool* msg_pool;

	err_exit(msg_node->_msg_size_order < MIN_MSG_SIZE_ORDER || msg_node->_msg_size_order >= MAX_MSG_SIZE_ORDER, "invalid message size.");

	msg_pool = __get_msg_pool(__the_cons_port, msg_node->_msg_size_order);
	err_exit(msg_pool == 0, "invalid message pool.");
	err_exit(msg_node->_msg_block_idx >= msg_pool->_msg_cnt, "invalid message index.");

	msg_hdr = __get_msg(msg_pool, msg_node->_msg_block_idx);
	err_exit(msg_hdr == 0, "invalid msg_hdr.");

	if(__cons_read_func)
		(*__cons_read_func)((char*)(msg_hdr + 1), msg_hdr->_msg_size, msg_hdr->_prod_service_type, msg_hdr->_prod_service_index);

	return msg_hdr;
error_ret:
	return 0;
}



static int __msg_pool_create(int key, unsigned int pool_idx, const struct ipc_channel_cfg* cfg)
{
	unsigned int shm_size, msg_size;
	struct shmm_blk* sb;
	struct ipc_msg_pool* imp;
	struct ipc_msg_header* msg_hdr;
	char* p;

	err_exit(pool_idx > MSG_POOL_COUNT, "invalid pool index.");

	msg_size = (1 << (pool_idx + MIN_MSG_SIZE_ORDER));
	shm_size = sizeof(struct ipc_msg_pool) + cfg->message_count[pool_idx] * sizeof(struct ipc_msg_free_node) + cfg->message_count[pool_idx] * msg_size;

	sb = shmm_create(key, shm_size, 0);
	err_exit(!sb, "create msg pool shm failed.");

	imp = (struct ipc_msg_pool*)shmm_begin_addr(sb);

	imp->_magic_tag = IPC_MSG_POOL_MAGIC; 
	imp->_msg_cnt = cfg->message_count[pool_idx];
	imp->_msg_order = pool_idx + MIN_MSG_SIZE_ORDER;
	imp->_free_msg_head = 0;
	imp->_free_msg_tail = imp->_msg_cnt - 1;

	p = (char*)imp + sizeof(struct ipc_msg_pool) + imp->_msg_cnt * sizeof(struct ipc_msg_free_node);
	imp->_guard_msg_hdr = p;

	for(int i = 0; i < imp->_msg_cnt; ++i)
	{
		imp->_free_node_list[i]._msg_idx = i;
		imp->_free_node_list[i]._next_free_idx = (i + 1) % imp->_msg_cnt;

		msg_hdr = (struct ipc_msg_header*)(p + msg_size * i);

		msg_hdr->_msg_tag = IPC_MSG_HEADER_MAGIC;
		msg_hdr->_msg_idx = i;

	}

	imp->_free_node_list[imp->_free_msg_tail]._next_free_idx = -1;

	return 0;
error_ret:
	return -1;
}

static int __msg_pool_load(int key, unsigned int pool_idx, int cons_service_type, int cons_service_index)
{
	unsigned int shm_size, msg_size;
	struct shmm_blk* sb;
	struct ipc_msg_pool* imp;
	struct ipc_msg_header* msg_hdr;
	char* p;

	err_exit(pool_idx > MSG_POOL_COUNT, "invalid pool index.");

	sb = shmm_open(key, 0);
	err_exit(!sb, "load msg pool shm failed.");

	imp = (struct ipc_msg_pool*)shmm_begin_addr(sb);
	err_exit(imp->_magic_tag != IPC_MSG_POOL_MAGIC, "invalid msg pool.");

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

	ilp->_service_type = service_type;
	ilp->_service_idx = service_index;

	for(int i = 0; i < MSG_POOL_COUNT; ++i)
	{
		struct ipc_msg_pool* imp;
		struct shmm_blk* sb = shmm_open(channel->_key_handle[i]._key, 0);
		err_exit(!sb, "open msg pool [%d] failed.", i);

		imp = (struct ipc_msg_pool*)shmm_begin_addr(sb);
		err_exit(imp->_magic_tag != IPC_MSG_POOL_MAGIC, "invalid msg pool [%d].", i);

		ilp->_shm_msg_pool[i] = sb;
		ilp->_msg_pool[i] = imp;
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
	unsigned long prod_head = channel->_prod_ptr_head;
	unsigned long cons_tail = channel->_cons_ptr_tail;

	err_exit(prod_head < cons_tail, "message queue full.");

	return 0;
error_ret:
	return -1;
}

static inline int __check_free_list(struct ipc_msg_pool* msg_pool)
{
	

	return 0;
error_ret:
	return -1;
}

static inline int __free_msg_sc(struct ipc_msg_pool* pool, struct ipc_msg_header* msg_hdr)
{
	unsigned int msg_hdr_idx = 0;
	struct ipc_msg_header* first_hdr;
	struct ipc_msg_header* tail_hdr;
	unsigned int msg_size = (1 << pool->_msg_order);

	err_exit(msg_hdr->_msg_idx >= pool->_msg_cnt, "__free_msg_sc: invalid msg_hdr.");
	err_exit(pool->_free_node_list[msg_hdr->_msg_idx]._next_free_idx >= 0, "__free_msg_sc: invalid msg_hdr.");

	pool->_free_node_list[pool->_free_msg_tail]._next_free_idx = msg_hdr->_msg_idx;
	pool->_free_msg_tail = msg_hdr->_msg_idx;
	pool->_free_node_list[pool->_free_msg_tail]._next_free_idx = -1;

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

int ipc_channel_load(int cons_service_type, int cons_service_index)
{
	int rslt;
	struct shmm_blk* sb = 0;
	struct ipc_channel* ic = 0;

	int channel_key = create_ipc_channel_key(cons_service_type, cons_service_index);
	err_exit(channel_key < 0, "create ipc channel key failed.");

	sb = shmm_open(channel_key, 0);
	err_exit(!sb, "open ipc channel failed.");

	ic = (struct ipc_channel*)shmm_begin_addr(sb);
	err_exit(ic->_magic_tag != IPC_CHANNEL_MAGIC, "invalid channel.");

	for(int i = 0; i < MSG_POOL_COUNT; ++i)
	{
		rslt = __msg_pool_load(ic->_key_handle[i]._key, i, cons_service_type, cons_service_index);
		err_exit(rslt < 0, "load ipc channel message pool[%d] failed.", i);
	}

	return 0;
error_ret:
	if(sb)
		shmm_close(sb);
	return -1;
}

int ipc_open_cons_port(int service_type, int service_index, ipc_read_func_t read_func)
{
	if(!__the_cons_port)
		__the_cons_port = __open_local_port(service_type, service_index);

	err_exit(__the_cons_port == 0, "open cons port failed.");

	__cons_read_func = read_func;

	return 0;
error_ret:
	return -1;
}

struct ipc_local_port* ipc_open_prod_port(int cons_service_type, int cons_service_index)
{
	return __open_local_port(cons_service_type, cons_service_index);
}

static inline int __ipc_close_port(struct ipc_local_port* local_port)
{
	err_exit(!local_port, "");

	for(int i = 0; i < MSG_POOL_COUNT; ++i)
	{
		struct shmm_blk* sb = local_port->_shm_msg_pool[i];
		shmm_close(sb);
	}

	free(local_port);

	if(local_port == __the_cons_port)
		__the_cons_port = 0;

	return 0;
error_ret:
	return -1;
}

int ipc_close_cons_port(void)
{
	return __ipc_close_port(__the_cons_port);
}

int ipc_close_prod_port(struct ipc_local_port* local_port)
{
	return __ipc_close_port(local_port);
}

int ipc_read_sc(void)
{
	struct ipc_channel* channel;
	struct ipc_msg_header* msg_hdr;
	struct ipc_msg_queue_node* msg_node;
	struct ipc_msg_pool* msg_pool;
	unsigned long cons_head, cons_next;

	err_exit(!__the_cons_port, "ipc_read_sc: invalid local port.");

	channel = (struct ipc_channel*)shmm_begin_addr(__the_cons_port->_shm_channel);
	err_exit(__check_valid_channel(channel) < 0, "ipc_read_sc: invalid ipc channel.");
	err_exit(__check_read(channel) < 0, "ipc_read_sc: can not read.");

	cons_head = channel->_cons_ptr_head;
	cons_next = cons_head + 1;

	msg_node = &channel->_msg_queue_node[cons_head % channel->_msg_queue_len];

	msg_pool = __get_msg_pool(__the_cons_port, msg_node->_msg_size_order);
	err_exit(msg_pool == 0, "invalid msg_pool.");

	channel->_cons_ptr_head = cons_next;
	channel->_cons_ptr_tail = cons_next;

	msg_hdr = __read_msg(msg_node);
	err_exit(!msg_hdr, "ipc_read_sc: read msg failed");

	__free_msg_sc(msg_pool, msg_hdr);

	return 0;
error_ret:
	return -1;
}

char* ipc_alloc_write_buf_mp(struct ipc_local_port* local_port, unsigned int size, int from_service_type, int from_service_index)
{
	unsigned int pool_idx, free_node, new_header, tail_node;

	struct ipc_msg_header* msg_hdr;
	struct ipc_msg_pool* msg_pool;

	err_exit(local_port == 0, "invalid local port.");

	msg_pool = __get_msg_pool(local_port, __aligned_msg_size_order(size));
	err_exit(msg_pool == 0, "invalid pool.");

	do {
		free_node = msg_pool->_free_msg_head;
		tail_node = msg_pool->_free_msg_tail;

		err_exit(free_node == tail_node, "invalid header node.");

		new_header = msg_pool->_free_node_list[free_node]._next_free_idx;
		err_exit(new_header < 0, "invalid header node.");

		spin_wait;
	} while(!__cas32(&msg_pool->_free_msg_head, free_node, new_header));

	msg_pool->_free_node_list[free_node]._next_free_idx = -1;
	msg_hdr = (struct ipc_msg_header*)(msg_pool->_guard_msg_hdr + (1 << msg_pool->_msg_order) * free_node);

	err_exit(msg_hdr->_msg_tag != IPC_MSG_HEADER_MAGIC, "not an allocated buf.");
	err_exit(msg_hdr->_msg_idx != free_node, "invalid free message.");

	msg_hdr->_prod_service_type = from_service_type;
	msg_hdr->_prod_service_index = from_service_index;
	msg_hdr->_cons_service_type = local_port->_service_type;
	msg_hdr->_cons_service_index = local_port->_service_idx;
	msg_hdr->_msg_size = size;

	return (char*)(msg_hdr + 1);
error_ret:
	return 0;
}

int ipc_write_mp(struct ipc_local_port* local_port, const char* buf)
{
	struct ipc_msg_header* msg_header;
	struct ipc_channel* channel;
	struct ipc_msg_queue_node* mqn;
	unsigned long prod_head, prod_next, cons_tail;

	err_exit(local_port == 0, "invalid port.");
	err_exit(buf == 0, "invalid buf.");

	msg_header = (struct ipc_msg_header*)(buf - sizeof(struct ipc_msg_header));

	err_exit(msg_header->_msg_tag != IPC_MSG_HEADER_MAGIC, "not an allocated buf.");

	channel = (struct ipc_channel*)shmm_begin_addr(local_port->_shm_channel);
	err_exit(__check_valid_channel(channel) < 0, "invalid ipc channel.");
//	err_exit(__check_write(channel) < 0, "channel can not write.");

	do {
		prod_head = channel->_prod_ptr_head;
		prod_next = prod_head + 1;
		cons_tail = channel->_cons_ptr_tail;

		err_exit(cons_tail > prod_head, "channel full.");

		spin_wait;

	} while(!__cas64(&channel->_prod_ptr_head, prod_head, prod_next));

	while(prod_head > channel->_prod_ptr_tail)
		spin_wait;

	err_exit(prod_head < channel->_prod_ptr_tail, "unknown error: prod_ptr_tail skipped me!");
	channel->_prod_ptr_tail = prod_next;

	mqn = &channel->_msg_queue_node[prod_head % channel->_msg_queue_len];

	mqn->_msg_block_idx = msg_header->_msg_idx;
	mqn->_msg_size_order = __aligned_msg_size_order(msg_header->_msg_size);

	return 0;
error_ret:
	return -1;
}


