#include "syslib/stkpool.h"
#include "syslib/mmops.h"
#include "syslib/misc.h"
#include "syslib/dlist.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

#define STKP_TAG	(0xAAFFBBEE00995577)
#define MIN_STK_SIZE	(0x2000UL)

struct _stkp_node
{
	struct dlnode _dln;
	union
	{
		void* _payload_addr;
		unsigned using : 2;
	};
};

struct _stkp_impl
{
	unsigned long _stkp_tag;
	struct stkpool _the_pool;
	struct dlist _free_list;
	void* _payload_trunk_addr;
	unsigned long _stk_frm_size;
	unsigned long _stk_frm_cnt;
	unsigned long _sys_pg_size;
	struct _stkp_node _node_pool[0];
};


static void* __stkp_create_agent(void* addr, struct mm_config* cfg)
{
	return stkp_create(addr, cfg);
}

static void* __stkp_load_agent(void* addr)
{
	return stkp_load(addr);
}

static void __stkp_destroy_agent(void* alloc)
{
	stkp_destroy((struct stkpool*)alloc);
}

static void* __stkp_alloc_agent(void* alloc, unsigned long size)
{
	return stkp_alloc((struct stkpool*)alloc);
}

static long __stkp_free_agent(void* alloc, void* p)
{
	return stkp_free((struct stkpool*)alloc, p);
}

static inline void _set_payload(struct _stkp_node* node, void* payload)
{
	node->_payload_addr = (void*)((unsigned long)payload | node->using);
}

static inline void* _get_payload(struct _stkp_node* node)
{
	return (void*)(((unsigned long)(node->_payload_addr)) & (~3));
}

struct mm_ops __stkp_ops =
{
	.create_func = __stkp_create_agent,
	.load_func = __stkp_load_agent,
	.destroy_func = __stkp_destroy_agent,

	.alloc_func = __stkp_alloc_agent,
	.free_func = __stkp_free_agent,
};

static inline struct _stkp_impl* _conv_impl(struct stkpool* stkp)
{
	return (struct _stkp_impl*)((void*)stkp - (unsigned long)(&((struct _stkp_impl*)(0))->_the_pool));
}

static inline struct _stkp_node* _conv_dln(struct dlnode* dln)
{
	return (struct _stkp_node*)((void*)dln- (unsigned long)(&((struct _stkp_node*)(0))->_dln));
}

static inline struct _stkp_node* _get_dln_by_payload(struct _stkp_impl* stkpi, void* payload)
{
	return &stkpi->_node_pool[(payload - stkpi->_payload_trunk_addr) / stkpi->_stk_frm_size];
}

struct _stkp_impl* _stkp_init(void* addr, unsigned long total_size, unsigned int stk_frm_size)
{
	long rslt = 0;
	void* cur_ptr;
	void* payload_ptr;
	unsigned long frm_cnt = 0;
	struct _stkp_impl* stkpi;

	stkpi = (struct _stkp_impl*)addr;
	cur_ptr = move_ptr_align8(addr, sizeof(struct _stkp_impl));

	stkpi->_sys_pg_size = sysconf(_SC_PAGESIZE);
	stkpi->_stk_frm_size = round_up(stkpi->_sys_pg_size + stk_frm_size, stkpi->_sys_pg_size);

	stkpi->_stkp_tag = STKP_TAG;
	stkpi->_the_pool.addr_begin = addr;
	stkpi->_the_pool.addr_end = (void*)round_down((unsigned long)addr + total_size, stkpi->_stk_frm_size);

	lst_new(&stkpi->_free_list);

	while(1)
	{
		if(cur_ptr + stkpi->_stk_frm_cnt * sizeof(struct _stkp_node) > stkpi->_the_pool.addr_end - stkpi->_stk_frm_size * stkpi->_stk_frm_cnt)
		{
			--stkpi->_stk_frm_cnt;
			break;
		}

		++stkpi->_stk_frm_cnt;
	}

	err_exit(stkpi->_stk_frm_cnt == 0, "not enough space for allocate.");
	stkpi->_payload_trunk_addr = stkpi->_the_pool.addr_end - stkpi->_stk_frm_size * stkpi->_stk_frm_cnt;

	for(int i = 0; i < stkpi->_stk_frm_cnt; ++i)
	{
		lst_clr(&stkpi->_node_pool[i]._dln);

		payload_ptr = stkpi->_payload_trunk_addr + i * stkpi->_stk_frm_size;

		stkpi->_node_pool[i]._payload_addr = payload_ptr + stkpi->_sys_pg_size;
		lst_push_back(&stkpi->_free_list, &stkpi->_node_pool[i]._dln);

//		rslt = mprotect(payload_ptr, stkpi->_sys_pg_size, PROT_READ);
//		err_exit(rslt < 0, "mprotect failed with errorcode: %d.", errno);
	}

	return stkpi;
error_ret:
	return 0;
}

struct stkpool* stkp_create(void* addr, struct mm_config* cfg)
{
	struct _stkp_impl* stkpi;

	err_exit((!addr || ((unsigned long)addr & 7) != 0), "address must be 8-byte aligned.");
	err_exit(cfg->stk_frm_size < MIN_STK_SIZE, "stack frame size must be >= %lu", MIN_STK_SIZE);

	stkpi = _stkp_init(addr, cfg->total_size, cfg->stk_frm_size);
	err_exit(!stkpi, "stkp init failed @ 0x%p.", addr);

	return &stkpi->_the_pool;
error_ret:
	return 0;
}


struct stkpool* stkp_load(void* addr)
{
	struct _stkp_impl* stkpi;
	err_exit((!addr || ((unsigned long)addr & 7) != 0), "invalid argument: address must be 8-byte aligned.");

	stkpi = _conv_impl((struct stkpool*)addr);

	err_exit(stkpi->_stkp_tag != STKP_TAG, "invalid address, stkpi: %p.", stkpi);

	return &stkpi->_the_pool;
error_ret:
	return 0;
}

void stkp_destroy(struct stkpool* stkp)
{
	long rslt;
	void* p;
	struct _stkp_impl* stkpi;

	err_exit(!stkp, "invalid argument.");

	stkpi = _conv_impl(stkp);
	p = stkpi->_payload_trunk_addr;


	// restore mprotected pages.
//	for(int i = 0; i < stkpi->_stk_frm_cnt; ++i)
//	{
//		rslt = mprotect(p, stkpi->_sys_pg_size, PROT_READ | PROT_WRITE);
//		if(rslt < 0)
//			fprintf(stderr, "mprotect failed @ 0x%p.\n", p);
//	}

	stkpi->_stkp_tag = 0;

	return;
error_ret:
	return;
}

void* stkp_alloc(struct stkpool* stkp)
{
	struct dlnode* dln;
	struct _stkp_node* nd;
	struct _stkp_impl* stkpi;

	err_exit(!stkp, "invalid argument.");

	stkpi = _conv_impl(stkp);

	dln = lst_pop_front(&stkpi->_free_list);
	err_exit(!dln, "no free object to alloc.");

	nd = _conv_dln(dln);

	nd->using = 1;

	return _get_payload(nd);
error_ret:
	return 0;
}

long stkp_free(struct stkpool* stkp, void* p)
{
	long idx;
	struct _stkp_impl* stkpi;

	err_exit(!stkp, "invalid argument.");
	err_exit(!p, "invalid argument.");

	stkpi = _conv_impl(stkp);

	err_exit((unsigned long)p & (stkpi->_sys_pg_size - 1), "invalid ptr.");

	p -= stkpi->_sys_pg_size;

	err_exit(p < stkpi->_payload_trunk_addr, "error ptr.");

	idx = (p - stkpi->_payload_trunk_addr) / stkpi->_stk_frm_size;
	err_exit(!stkpi->_node_pool[idx].using, "error: freed twice.");

	stkpi->_node_pool[idx].using = 0;

	lst_push_front(&stkpi->_free_list, &stkpi->_node_pool[idx]._dln);

	return 0;
error_ret:
	return -1;
}

