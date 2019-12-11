#include "common_types.h"
#include "core/ipc_channel.h"
#include "core/misc.h"
#include "core/asm.h"
#include "core/shmem.h"
#include "core/shm_key.h"
#include "core/slist.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define IPC_CHANNEL_MAGIC		(0x33554455)
#define IPC_MSG_POOL_MAGIC		(0x55335544)
#define IPC_MSG_HEADER_MAGIC	(0xfeeffeef)

#pragma pack(1)
struct ipc_msg_header
{
	u32 _msg_tag;
	u32 _msg_size;
	u32 _msg_idx;

	unsigned _prod_service_type : SHM_SERVICE_TYPE_BITS;
	unsigned _prod_service_index : SHM_SERVICE_INDEX_BITS;
	unsigned _cons_service_type : SHM_SERVICE_TYPE_BITS;
	unsigned _cons_service_index : SHM_SERVICE_INDEX_BITS;
};
#pragma pack()

struct ipc_msg_queue_node
{
	u32 _msg_size_order;
	u32 _msg_block_idx;
};

struct ipc_msg_pool_key_handle
{
	i32 _key;
};

struct ipc_channel
{
	u32 _magic_tag;
	u32 _msg_queue_len;

	struct ipc_cons_port* _cons_port;

	volatile u64 _cons_ptr_head;
	volatile u64 _cons_ptr_tail;
	volatile u64 _prod_ptr_head;
	volatile u64 _prod_ptr_tail;

	struct ipc_msg_pool_key_handle _key_handle[MSG_POOL_COUNT];

	struct ipc_msg_queue_node _msg_queue_node[0];
};

struct ipc_msg_free_node
{
	u32 _msg_idx;
	volatile i32 _next_free_idx;
};

struct ipc_msg_pool
{
	u32 _magic_tag;
	u32 _msg_order;
	volatile u32 _free_msg_head;
	volatile u32 _free_msg_tail;
	u32 _msg_cnt;
	u32 _msg_hdr_offset;
	struct ipc_msg_free_node _free_node_list[0];
};

struct ipc_channel_port
{
	struct ipc_service_key _cons_key;
	struct shmm_blk* _shm_channel;
	struct shmm_blk* _shm_msg_pool[MSG_POOL_COUNT];
	struct ipc_msg_pool* _msg_pool[MSG_POOL_COUNT];
};

struct ipc_prod_port
{
	struct ipc_channel_port _local_port;
	struct ipc_service_key _local_service_key;
};

struct ipc_cons_port
{
	struct ipc_channel_port _local_port;
	ipc_read_func_t _read_func;
};

static u64 __rdtsc_read = 0;
static u64 __read_count = 0;

static u64 __rdtsc_write = 0;
static u64 __write_count = 0;

static inline u32 __aligned_msg_size(u32 payload_size)
{
	return (u32)round_up(payload_size + sizeof(struct ipc_msg_header), 8);
}

static inline u32 __aligned_msg_size_order(u32 payload_size)
{
	return log_2(__aligned_msg_size(payload_size)) + 1;
}

static inline i32 __check_valid_channel(struct ipc_channel* channel)
{
	if(channel && channel->_magic_tag == IPC_CHANNEL_MAGIC)
		return 0;

	return -1;
}

static inline i32 __cas32(volatile u32* dst, u32 expected, u32 src)
{
	char result;
	asm volatile("lock; cmpxchg %3, %1; sete %0" : "=q"(result), "+m"(*dst), "+a"(expected) : "r"(src) : "memory", "cc");
	return result;
}

static inline i32 __cas64(volatile u64* dst, u64 expected, u64 src)
{
	char result;
	asm volatile("lock; cmpxchg %3, %1; sete %0" : "=q"(result), "+m"(*dst), "+a"(expected) : "r"(src) : "memory", "cc");
	return result;
}

static inline struct ipc_msg_pool* __get_msg_pool(struct ipc_channel_port* port, i32 msg_size_order)
{
	struct ipc_msg_pool* imp;
	err_exit(msg_size_order < MIN_MSG_SIZE_ORDER || msg_size_order >= MAX_MSG_SIZE_ORDER, "invalid msg size order");

	imp = (struct ipc_msg_pool*)shmm_begin_addr(port->_shm_msg_pool[msg_size_order - MIN_MSG_SIZE_ORDER]);
	err_exit(imp->_magic_tag != IPC_MSG_POOL_MAGIC, "invalid size order [%d].", msg_size_order);

	return imp;
error_ret:
	return 0;
}

static inline struct ipc_msg_header* __get_msg(struct ipc_msg_pool* msg_pool, u32 msg_idx)
{
	struct ipc_msg_header* msg_hdr;

	msg_hdr = (struct ipc_msg_header*)(((char*)msg_pool + msg_pool->_msg_hdr_offset) + (1 << msg_pool->_msg_order) * msg_idx);

	err_exit(msg_hdr->_msg_tag != IPC_MSG_HEADER_MAGIC, "invalid addr.");
	err_exit(msg_hdr->_msg_idx != msg_idx, "invalid idx.");

	return msg_hdr;
error_ret:
	return 0;
}

static inline struct ipc_msg_header* __read_msg(struct ipc_cons_port* cons_port, struct ipc_msg_queue_node* msg_node)
{
	struct ipc_msg_header* msg_hdr;
	struct ipc_msg_pool* msg_pool;

	err_exit(msg_node->_msg_size_order < MIN_MSG_SIZE_ORDER || msg_node->_msg_size_order >= MAX_MSG_SIZE_ORDER, "invalid message size.");

	msg_pool = __get_msg_pool(&cons_port->_local_port, msg_node->_msg_size_order);
	err_exit(msg_pool == 0, "invalid message pool.");
	err_exit(msg_node->_msg_block_idx >= msg_pool->_msg_cnt, "invalid message index.");

	msg_hdr = __get_msg(msg_pool, msg_node->_msg_block_idx);
	err_exit(msg_hdr == 0, "invalid msg_hdr.");

	if(cons_port->_read_func)
		(*cons_port->_read_func)((char*)(msg_hdr + 1), msg_hdr->_msg_size, msg_hdr->_prod_service_type, msg_hdr->_prod_service_index);

	return msg_hdr;
error_ret:
	return 0;
}

static i32 __msg_pool_create(i32 key, u32 pool_idx, const struct ipc_channel_cfg* cfg)
{
	u32 shm_size, msg_size;
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
	imp->_msg_hdr_offset = (u32)((u64)p - (u64)imp);

	for(i32 i = 0; i < imp->_msg_cnt; ++i)
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

static i32 __msg_pool_load(i32 key)
{
	u32 shm_size, msg_size;
	struct shmm_blk* sb;
	struct ipc_msg_pool* imp;
	struct ipc_msg_header* msg_hdr;
	char* p;

	sb = shmm_open(key, 0);
	err_exit(!sb, "load msg pool shm failed.");

	imp = (struct ipc_msg_pool*)shmm_begin_addr(sb);
	err_exit(imp->_magic_tag != IPC_MSG_POOL_MAGIC, "invalid msg pool.");

	return 0;
error_ret:
	return -1;
}

static i32 __msg_pool_destroy(i32 key)
{
	u32 shm_size, msg_size;
	struct shmm_blk* sb;
	struct ipc_msg_pool* imp;
	struct ipc_msg_header* msg_hdr;
	char* p;

	sb = shmm_open(key, 0);
	err_exit(!sb, "load msg pool shm failed.");

	shmm_destroy(sb);

	return 0;
error_ret:
	return -1;
}


static inline i32 __check_read(struct ipc_channel* channel)
{
	u64 cons_head = channel->_cons_ptr_head;
	u64 prod_tail = channel->_prod_ptr_tail;

	err_exit_silent(cons_head >= prod_tail);

	return 0;
error_ret:
	return -1;
}

static inline i32 __check_write(struct ipc_channel* channel)
{
	u64 prod_head = channel->_prod_ptr_head;
	u64 cons_tail = channel->_cons_ptr_tail;

	err_exit_silent(prod_head < cons_tail);

	return 0;
error_ret:
	return -1;
}

static inline i32 __check_free_list(struct ipc_msg_pool* msg_pool)
{
	

	return 0;
error_ret:
	return -1;
}

static inline i32 __free_msg_sc(struct ipc_msg_pool* pool, struct ipc_msg_header* msg_hdr)
{
	u32 msg_hdr_idx = 0;
	struct ipc_msg_header* first_hdr;
	struct ipc_msg_header* tail_hdr;
	u32 msg_size = (1 << pool->_msg_order);

	err_exit(msg_hdr->_msg_idx >= pool->_msg_cnt, "__free_msg_sc: invalid msg_hdr.");
	err_exit(pool->_free_node_list[msg_hdr->_msg_idx]._next_free_idx >= 0, "__free_msg_sc: invalid msg_hdr.");

	pool->_free_node_list[pool->_free_msg_tail]._next_free_idx = msg_hdr->_msg_idx;
	pool->_free_msg_tail = msg_hdr->_msg_idx;
	pool->_free_node_list[pool->_free_msg_tail]._next_free_idx = -1;

	return 0;
error_ret:
	return -1;
}

static inline i32 __ipc_close_port(struct ipc_channel_port* local_port)
{
	err_exit_silent(!local_port);

	for(i32 i = 0; i < MSG_POOL_COUNT; ++i)
	{
		struct shmm_blk* sb = local_port->_shm_msg_pool[i];
		if(sb)
			shmm_close(sb);
	}

	return 0;
error_ret:
	return -1;
}

i32 ipc_channel_create(const struct ipc_channel_cfg* cfg)
{
	i32 result;
	struct shmm_blk* sb = 0;
	struct ipc_channel* ic = 0;

	i32 channel_key = create_ipc_channel_key(cfg->cons_service_key.service_type, cfg->cons_service_key.service_index);
	err_exit(channel_key < 0, "create ipc channel key failed.");

	sb = shmm_create(channel_key, sizeof(struct ipc_channel) + cfg->message_queue_len * sizeof(struct ipc_msg_queue_node), 0);
	err_exit(!sb, "create ipc channel shm failed.");

	ic = (struct ipc_channel*)shmm_begin_addr(sb);

	ic->_magic_tag = IPC_CHANNEL_MAGIC;
	ic->_cons_port = 0;

	ic->_cons_ptr_head = 0;
	ic->_cons_ptr_tail = 0;
	ic->_prod_ptr_head = 0;
	ic->_prod_ptr_tail = 0;
	ic->_msg_queue_len = cfg->message_queue_len;

	for(i32 i = 0; i < MSG_POOL_COUNT; ++i)
	{
		i32 key = create_msg_pool_key(cfg->cons_service_key.service_type, cfg->cons_service_key.service_index, i, 0);

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

i32 ipc_channel_load(struct ipc_service_key* cons_service_key)
{
	i32 rslt;
	struct shmm_blk* sb = 0;
	struct ipc_channel* ic = 0;

	i32 channel_key = create_ipc_channel_key(cons_service_key->service_type, cons_service_key->service_index);
	err_exit(channel_key < 0, "create ipc channel key failed.");

	sb = shmm_open(channel_key, 0);
	err_exit(!sb, "open ipc channel failed.");

	ic = (struct ipc_channel*)shmm_begin_addr(sb);
	err_exit(ic->_magic_tag != IPC_CHANNEL_MAGIC, "invalid channel.");

	ic->_cons_port = 0;

	for(i32 i = 0; i < MSG_POOL_COUNT; ++i)
	{
		rslt = __msg_pool_load(ic->_key_handle[i]._key);
		err_exit(rslt < 0, "load ipc channel message pool[%d] failed.", i);
	}

	return 0;
error_ret:
	if(sb)
		shmm_close(sb);
	return -1;
}

i32 ipc_channel_destroy(struct ipc_service_key* cons_service_key)
{
	i32 rslt;
	struct shmm_blk* sb = 0;
	struct ipc_channel* ic = 0;

	i32 channel_key = create_ipc_channel_key(cons_service_key->service_type, cons_service_key->service_index);
	err_exit(channel_key < 0, "create ipc channel key failed.");

	sb = shmm_open(channel_key, 0);
	err_exit(!sb, "open ipc channel failed.");

	ic = (struct ipc_channel*)shmm_begin_addr(sb);
	err_exit(ic->_magic_tag != IPC_CHANNEL_MAGIC, "invalid channel.");

	for(i32 i = 0; i < MSG_POOL_COUNT; ++i)
	{
		rslt = __msg_pool_destroy(ic->_key_handle[i]._key);
		err_exit(rslt < 0, "destroy ipc channel message pool[%d] failed.", i);
	}

	shmm_destroy(sb);

	return 0;
error_ret:
	if(sb)
		shmm_destroy(sb);
	return -1;
}

static i32 __open_local_port(struct ipc_channel_port* ilp, struct ipc_service_key* cons_key, struct ipc_channel** ret_channel)
{
	struct ipc_channel* channel;

	i32 channel_key = create_ipc_channel_key(cons_key->service_type, cons_key->service_index);
	err_exit(channel_key < 0, "create ipc channel key failed.");

	ilp->_shm_channel = shmm_open(channel_key, 0);
	err_exit(!ilp->_shm_channel, "open ipc channel failed.");

	channel = (struct ipc_channel*)shmm_begin_addr(ilp->_shm_channel);
	err_exit(channel->_magic_tag != IPC_CHANNEL_MAGIC, "invalid ipc channel.");

	ilp->_cons_key = *cons_key;

	for(i32 i = 0; i < MSG_POOL_COUNT; ++i)
	{
		struct ipc_msg_pool* imp;
		struct shmm_blk* sb = shmm_open(channel->_key_handle[i]._key, 0);
		err_exit(!sb, "open msg pool [%d] failed.", i);

		imp = (struct ipc_msg_pool*)shmm_begin_addr(sb);
		err_exit(imp->_magic_tag != IPC_MSG_POOL_MAGIC, "invalid msg pool [%d].", i);

		ilp->_shm_msg_pool[i] = sb;
		ilp->_msg_pool[i] = imp;
	}

	*ret_channel = channel;

	return 0;
error_ret:
	__ipc_close_port(ilp);
	return -1;
}

struct ipc_cons_port* ipc_open_cons_port(struct ipc_service_key* cons_service_key, ipc_read_func_t read_func)
{
	i32 rslt;
	struct ipc_cons_port* icp;
	struct ipc_channel* channel;

	err_exit_silent(!cons_service_key);

	icp = malloc(sizeof(struct ipc_cons_port));
	err_exit(!icp, "malloc port failed.");

	rslt = __open_local_port(&icp->_local_port, cons_service_key, &channel);
	err_exit(rslt < 0, "open local port failed.");

	err_exit(channel->_cons_port, "cons port has already been opened.");

	rslt = __cas64((u64*)(&channel->_cons_port), 0LL, (u64)icp);
	err_exit(!rslt, "cons port has already been opened.");

	icp->_read_func = read_func;

	return icp;
error_ret:
	__ipc_close_port(&icp->_local_port);

	if(icp)
		free(icp);
	return 0;
}


struct ipc_prod_port* ipc_open_prod_port(struct ipc_service_key* cons_service_key, struct ipc_service_key* prod_service_key)
{
	i32 rslt;
	struct ipc_prod_port* ipp;
	struct ipc_channel* channel;

	err_exit_silent(!cons_service_key);

	ipp = malloc(sizeof(struct ipc_prod_port));
	err_exit(!ipp, "malloc port failed.");

	rslt = __open_local_port(&ipp->_local_port, cons_service_key, &channel);
	err_exit(rslt < 0, "open local port failed.");

	ipp->_local_service_key = *prod_service_key;

	return ipp;
error_ret:
	__ipc_close_port(&ipp->_local_port);

	if(ipp)
		free(ipp);
	return 0;
}

i32 ipc_close_cons_port(struct ipc_cons_port* cons_port)
{
	i32 rslt;

	struct ipc_channel* channel = (struct ipc_channel*)shmm_begin_addr(cons_port->_local_port._shm_channel);
	err_exit(channel->_magic_tag != IPC_CHANNEL_MAGIC, "invalid ipc channel.");

	rslt = __ipc_close_port(&cons_port->_local_port);
	err_exit(rslt < 0, "close port failed.");

	free(cons_port);

	return 0;
error_ret:
	return -1;
}

i32 ipc_close_prod_port(struct ipc_prod_port* prod_port)
{
	i32 rslt = __ipc_close_port(&prod_port->_local_port);
	err_exit(rslt < 0, "close port failed.");

	free(prod_port);

	return 0;
error_ret:
	return -1;
}

i32 ipc_read_sc(struct ipc_cons_port* cons_port)
{
	struct ipc_channel* channel;
	struct ipc_msg_header* msg_hdr;
	struct ipc_msg_queue_node* msg_node;
	struct ipc_msg_pool* msg_pool;
	u64 cons_head, cons_next;

	u64 rdtsc1 = rdtsc();

	err_exit(!cons_port, "ipc_read_sc: invalid local port.");

	channel = (struct ipc_channel*)shmm_begin_addr(cons_port->_local_port._shm_channel);
	err_exit(__check_valid_channel(channel) < 0, "ipc_read_sc: invalid ipc channel.");
	err_exit_silent(__check_read(channel) < 0);

	cons_head = channel->_cons_ptr_head;
	cons_next = cons_head + 1;

	msg_node = &channel->_msg_queue_node[cons_head % channel->_msg_queue_len];

	msg_pool = __get_msg_pool(&cons_port->_local_port, msg_node->_msg_size_order);
	err_exit(msg_pool == 0, "invalid msg_pool.");

	channel->_cons_ptr_head = cons_next;
	channel->_cons_ptr_tail = cons_next;

	msg_hdr = __read_msg(cons_port, msg_node);
	err_exit(!msg_hdr, "ipc_read_sc: read msg failed");

	__free_msg_sc(msg_pool, msg_hdr);

	__rdtsc_read += (rdtsc() - rdtsc1);

	++__read_count;

	return 0;
error_ret:
	return -1;
}

char* ipc_alloc_write_buf_mp(struct ipc_prod_port* prod_port, u32 size)
{
	u32 pool_idx, free_node, new_header, tail_node;

	struct ipc_msg_header* msg_hdr;
	struct ipc_msg_pool* msg_pool;

	err_exit(prod_port == 0, "invalid prod port.");

	msg_pool = __get_msg_pool(&prod_port->_local_port, __aligned_msg_size_order(size));
	err_exit(msg_pool == 0, "invalid pool.");

	do {
		free_node = msg_pool->_free_msg_head;
		tail_node = msg_pool->_free_msg_tail;

		err_exit_silent(free_node == tail_node);

		new_header = msg_pool->_free_node_list[free_node]._next_free_idx;
		err_exit(new_header < 0, "invalid header node.");

		spin_wait;
	} while(!__cas32(&msg_pool->_free_msg_head, free_node, new_header));

	msg_pool->_free_node_list[free_node]._next_free_idx = -1;
	msg_hdr = (struct ipc_msg_header*)((char*)msg_pool + msg_pool->_msg_hdr_offset + (1 << msg_pool->_msg_order) * free_node);

	err_exit(msg_hdr->_msg_tag != IPC_MSG_HEADER_MAGIC, "not an allocated buf.");
	err_exit(msg_hdr->_msg_idx != free_node, "invalid free message.");

	msg_hdr->_prod_service_type = prod_port->_local_service_key.service_type;
	msg_hdr->_prod_service_index = prod_port->_local_service_key.service_index;
	msg_hdr->_cons_service_type = prod_port->_local_port._cons_key.service_type;
	msg_hdr->_cons_service_index = prod_port->_local_port._cons_key.service_index;
	msg_hdr->_msg_size = size;

	return (char*)(msg_hdr + 1);
error_ret:
	return 0;
}

i32 ipc_write_mp(struct ipc_prod_port* prod_port, const char* buf)
{
	struct ipc_msg_header* msg_header;
	struct ipc_channel* channel;
	struct ipc_msg_queue_node* mqn;
	u64 prod_head, prod_next, cons_tail;

	err_exit(!prod_port, "invalid port.");
	err_exit(!buf, "invalid buf.");

	msg_header = (struct ipc_msg_header*)(buf - sizeof(struct ipc_msg_header));

	err_exit(msg_header->_msg_tag != IPC_MSG_HEADER_MAGIC, "not an allocated buf.");

	channel = (struct ipc_channel*)shmm_begin_addr(prod_port->_local_port._shm_channel);
	err_exit(__check_valid_channel(channel) < 0, "invalid ipc channel.");

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

static i32 __ipc_channel_check_state(struct ipc_channel_port* local_port)
{
	struct ipc_channel* channel;

	err_exit(!local_port, "error local port");
	err_exit(!local_port->_shm_channel, "error channel shm.");

	channel = (struct ipc_channel*)shmm_begin_addr(local_port->_shm_channel);
	err_exit(channel->_magic_tag != IPC_CHANNEL_MAGIC, "invalid ipc channel.");

	printf("cons_ptr_head: %lu\n", channel->_cons_ptr_head);
	printf("cons_ptr_tail: %lu\n", channel->_cons_ptr_tail);
	printf("prod_ptr_head: %lu\n", channel->_prod_ptr_head);
	printf("prod_ptr_tail: %lu\n", channel->_prod_ptr_tail);

	for(i32 i = 0; i < MSG_POOL_COUNT; ++i) 
	{
		i32 free_node_count = 0;

		struct ipc_msg_pool* imp = __get_msg_pool(local_port, MIN_MSG_SIZE_ORDER + i);
		err_exit(!imp, "error message pool: %d", i);

		err_exit(imp->_free_node_list[imp->_free_msg_tail]._next_free_idx >= 0, "msgpool [%d]: failed to check tail node.", i);

		for(i32 j = imp->_free_msg_head; j >= 0; )
		{
			++free_node_count;
			j = imp->_free_node_list[j]._next_free_idx;
		}

		printf("msgpool [%d] free node count: %d, total count: %d\n", i, free_node_count, imp->_msg_cnt);
	}

	if(__read_count > 0)
		printf("avg read rdtsc: %lu\n", __rdtsc_read / __read_count);

	return 0;
error_ret:
	return -1;
}

i32 ipc_channel_check_state_cons(struct ipc_cons_port* cons_port)
{
	return __ipc_channel_check_state(&cons_port->_local_port);
}

i32 ipc_channel_check_state_prod(struct ipc_prod_port* prod_port)
{
	return __ipc_channel_check_state(&prod_port->_local_port);
}


