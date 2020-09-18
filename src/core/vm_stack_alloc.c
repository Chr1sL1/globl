#include "common_types.h"
#include "core/vm_stack_alloc.h"
#include "core/dlist.h"
#include "core/misc.h"
#include "core/asm.h"
#include "core/vm_space.h"

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

struct vm_stack_allocator
{
	u64 _stkp_tag;
	void* addr_begin;
	struct dlist _free_list;
	void* _payload_trunk_addr;
	u64 _stk_frm_size;
	u64 _req_stk_frm_size;
	u64 _stk_frm_cnt;
	u64 _sys_pg_size;
	const char* _name;
	struct _stkp_node _node_pool[];
};

static inline void _set_payload(struct _stkp_node* node, void* payload)
{
	node->_payload_addr = (void*)((u64)payload | node->using);
}

static inline void* _get_payload(struct _stkp_node* node)
{
	return (void*)(((u64)(node->_payload_addr)) & (~3));
}

static inline struct _stkp_node* _conv_dln(struct dlnode* dln)
{
	return (struct _stkp_node*)((void*)dln- (u64)(&((struct _stkp_node*)(0))->_dln));
}

static inline struct _stkp_node* _get_dln_by_payload(struct vm_stack_allocator* stkpi, void* payload)
{
	return &stkpi->_node_pool[(payload - stkpi->_payload_trunk_addr) / stkpi->_stk_frm_size];
}

struct vm_stack_allocator* _stkp_init(void* addr, u64 cocurrent_stack_cnt, u64 stack_frm_size)
{
	i32 rslt = 0;
	void* payload_ptr;
	u64 frm_cnt = 0;
	struct vm_stack_allocator* stkpi;

	stkpi = (struct vm_stack_allocator*)addr;

	stkpi->_sys_pg_size = sysconf(_SC_PAGESIZE);
	stkpi->_stk_frm_size = round_up(stkpi->_sys_pg_size + stack_frm_size, stkpi->_sys_pg_size);
	stkpi->_req_stk_frm_size = stack_frm_size;

	stkpi->_stkp_tag = STKP_TAG;
	stkpi->addr_begin = addr;
	stkpi->_stk_frm_cnt = cocurrent_stack_cnt;

	lst_new(&stkpi->_free_list);

	stkpi->_payload_trunk_addr = vm_alloc_page(stkpi->_stk_frm_size * cocurrent_stack_cnt);
	err_exit(!stkpi->_payload_trunk_addr, "alloc stack payload failed.");

	for(i32 i = 0; i < stkpi->_stk_frm_cnt; ++i)
	{
		lst_clr(&stkpi->_node_pool[i]._dln);

		payload_ptr = stkpi->_payload_trunk_addr + i * stkpi->_stk_frm_size;

		stkpi->_node_pool[i]._payload_addr = (char*)payload_ptr + stkpi->_sys_pg_size;
		lst_push_back(&stkpi->_free_list, &stkpi->_node_pool[i]._dln);

		rslt = mprotect(payload_ptr, stkpi->_sys_pg_size, PROT_READ);
		err_exit(rslt < 0, "mprotect failed with errorcode: %d.", errno);
	}

	return stkpi;
error_ret:
	return 0;
}

struct vm_stack_allocator* stack_allocator_create(const char* allocator_name, u64 cocurrent_stack_cnt, u64 stack_frm_size)
{
	void* addr;
	struct vm_stack_allocator* stkpi;

	u64 allocator_size = sizeof(struct vm_stack_allocator) + cocurrent_stack_cnt * sizeof(struct _stkp_node);

	addr = vm_new_chunk(allocator_name, allocator_size);
	err_exit(!addr, "new chunk error.");

	stkpi = _stkp_init(addr, cocurrent_stack_cnt, stack_frm_size);
	err_exit(!stkpi, "stkp init failed @ 0x%p.", addr);

	stkpi->_name = allocator_name;

	return stkpi;
error_ret:
	return 0;
}

struct vm_stack_allocator* stack_allocator_load(const char* allocator_name)
{
	struct vm_stack_allocator* vsa = (struct vm_stack_allocator*)vm_find_chunk(allocator_name);
	err_exit(!vsa, "can not find chunk");
	err_exit(vsa->_stkp_tag != STKP_TAG, "invalid address, stkpi: 0x%p.", vsa);

	return vsa;
error_ret:
	return 0;
}

void stack_allocator_destroy(struct vm_stack_allocator* stkp)
{
	i32 rslt;
	char* p;

	err_exit(!stkp, "invalid argument.");

	p = stkp->_payload_trunk_addr;

	// restore mprotected pages.
	for(i32 i = 0; i < stkp->_stk_frm_cnt; ++i)
	{
		rslt = mprotect(p, stkp->_sys_pg_size, PROT_READ | PROT_WRITE);
		if(rslt < 0)
			fprintf(stderr, "mprotect failed @ 0x%p.\n", p);

		p += stkp->_stk_frm_size;
	}

	vm_free_page(stkp->_payload_trunk_addr);

	stkp->_stkp_tag = 0;

	vm_del_chunk(stkp->_name);

	return;
error_ret:
	return;
}

//static int __alloc_ref = 0;

void* stack_allocator_alloc(struct vm_stack_allocator* stkp, u64* stack_frame_size)
{
	struct dlnode* dln;
	struct _stkp_node* nd;

	err_exit(!stkp, "invalid argument.");

	dln = lst_pop_front(&stkp->_free_list);
	err_exit(!dln, "no free object to alloc.");

	nd = _conv_dln(dln);

	nd->using = 1;

	*stack_frame_size = stkp->_req_stk_frm_size;

//	++__alloc_ref;

	return _get_payload(nd);
error_ret:
//	printf("stack_allocator_alloc failed, ref: %d.\n", __alloc_ref);
	return 0;
}

i32 stack_allocator_free(struct vm_stack_allocator* stkp, void* p)
{
	i32 idx;

	err_exit(!stkp, "invalid argument.");
	err_exit(!p, "invalid argument.");

	err_exit((u64)p & (stkp->_sys_pg_size - 1), "invalid ptr.");

	p -= stkp->_sys_pg_size;

	err_exit(p < stkp->_payload_trunk_addr, "error ptr.");

	idx = (p - stkp->_payload_trunk_addr) / stkp->_stk_frm_size;
	err_exit(!stkp->_node_pool[idx].using, "error: freed twice.");

	stkp->_node_pool[idx].using = 0;

	lst_push_front(&stkp->_free_list, &stkp->_node_pool[idx]._dln);

	--__alloc_ref;

	return 0;
error_ret:
	return -1;
}

