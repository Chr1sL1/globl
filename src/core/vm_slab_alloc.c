#include "common_types.h"
#include "core/vm_slab_alloc.h"
#include "core/dlist.h"
#include "core/vm_space.h"
#include "core/vm_page_pool.h"
#include "core/asm.h"
#include "core/misc.h"

#include <stdio.h>

#define VM_SLAB_MAGIC		(0xABCDEFAB)
#define VM_SLAB_ALLOC_MAGIC	(0x12312312ABCABCAB)

#define VM_SLAB_MAX_OBJ		(1024)
#define VM_BITMAP_PER_CNT	(64)

struct vm_slab;

struct vm_slab_cookie
{
	struct vm_slab* _slab;
};

struct vm_slab
{
	u32 _magic;
	u16 _obj_count;
	u8 _obj_size_order;
	u8 _bit_map_cnt;
	u64* _alloc_bit_map;
	void* _obj_ptr;
	void* _end_addr;
	struct dlnode _list_node;
};

struct vm_slab_list
{
	struct dlist _full_list;
	struct dlist _partial_list;
	struct dlist _empty_list;
	i32 _in_empty_cnt;
	i32 _out_empty_cnt;
};

struct vm_slab_allocator
{
	u64 _magic;
	u16 _min_obj_size_order;
	u16 _max_obj_size_order;
	u32 _init_obj_cnt;
	u32 _slab_cnt;
	u32 _slab_list_cnt;
	u32 _alignment_order;
	struct vm_page_pool* _page_pool;

	struct vm_slab_list* _slab_list;
};

static inline struct vm_slab* _convert_list_node(struct dlnode* list_node)
{
	return (struct vm_slab*)((u64)list_node - (u64)&((struct vm_slab*)(0))->_list_node);
}

static inline i32 _is_slab_full(struct vm_slab* slab)
{
	for (i32 i = 0; i < slab->_bit_map_cnt; ++i)
	{
		if (slab->_alloc_bit_map[i] != (u64)-1)
			return 0;
	}
	return 0;
}

static inline i32 _is_slab_empty(struct vm_slab* slab)
{
	for (i32 i = 0; i < slab->_bit_map_cnt; ++i)
	{
		if (slab->_alloc_bit_map[i] != 0)
			return 0;
	}
	return 1;
}

static inline i32 _check_aligment(void* ptr, u32 _alignment_order)
{
	return (((u64)ptr) & ((1 << _alignment_order) - 1)) == 0;
}

static struct vm_slab_list* _get_slab_list(struct vm_slab_allocator* alloc, u32 size_order)
{
	err_exit(size_order > alloc->_max_obj_size_order, "");
	if (size_order < alloc->_min_obj_size_order)
		return &alloc->_slab_list[0];

	return &alloc->_slab_list[size_order - alloc->_min_obj_size_order];
error_ret:
	return 0;
}


struct vm_slab_allocator* vsa_create(const char* allocator_name, struct vm_page_pool* page_pool, u32 min_obj_size, u32 max_obj_size, u32 init_obj_cnt, u32 alignment_order)
{
	u64 allocator_size;
	i32 slab_list_cnt, min_size_order, max_size_order;

	struct vm_slab_allocator* vsa = 0;
	err_exit(!page_pool, "");
	err_exit(alignment_order >= 12, "");

	min_size_order = log_2(round_up_2power(min_obj_size + sizeof(struct vm_slab_cookie)));
	max_size_order = log_2(round_up_2power(max_obj_size + sizeof(struct vm_slab_cookie)));

	slab_list_cnt = max_size_order - min_size_order + 1;
	allocator_size = sizeof(struct vm_slab_allocator) + slab_list_cnt * sizeof(struct vm_slab_list);

	vsa = (struct vm_slab_allocator*)vm_new_chunk(allocator_name, allocator_size);
	err_exit(!vsa, "");

	vsa->_magic = VM_SLAB_ALLOC_MAGIC;
	vsa->_min_obj_size_order = min_size_order;
	vsa->_max_obj_size_order = max_size_order;
	vsa->_init_obj_cnt = init_obj_cnt;
	vsa->_page_pool = page_pool;
	vsa->_alignment_order = alignment_order;
	vsa->_slab_list_cnt = slab_list_cnt;
	vsa->_slab_list = (struct vm_slab_list*)(vsa + 1);

	for (i32 i = 0; i < slab_list_cnt; ++i)
	{
		lst_new(&vsa->_slab_list[i]._full_list);
		lst_new(&vsa->_slab_list[i]._partial_list);
		lst_new(&vsa->_slab_list[i]._empty_list);
	}

	return vsa;
error_ret:
	return 0;
}

struct vm_slab_allocator* vsa_load(const char* allocator_name)
{
	struct vm_slab_allocator* vsa = (struct vm_slab_allocator*)vm_find_chunk(allocator_name);
	err_exit(!vsa, "");
	err_exit(vsa->_magic != VM_SLAB_ALLOC_MAGIC, "");

	return vsa;
error_ret:
	return 0;
}

void vsa_destroy(struct vm_slab_allocator* allocator)
{

}

static i32 _recycle_empty_slab(struct vm_slab_allocator* allocator)
{
	u32 target_idx = -1;
	i32 cnt = 0;

	struct dlnode* node;

	for(u32 i = 0; i < allocator->_slab_list_cnt; ++i)
	{
		if(allocator->_slab_list[i]._out_empty_cnt > cnt)
		{
			cnt = allocator->_slab_list[i]._out_empty_cnt;
			target_idx = i;
		}
	}

	cnt = 0;
	node = lst_first(&allocator->_slab_list[target_idx]._empty_list);
	while(node != lst_last(&allocator->_slab_list[target_idx]._empty_list))
	{
		struct vm_slab* slab = _convert_list_node(node);
		vpp_free(allocator->_page_pool, slab);

		++cnt;

		node = node->next;
	}


	return cnt;
error_ret:
	return 0;
}

static inline struct vm_slab* _new_slab(struct vm_slab_allocator* allocator, i32 size_order, i32 obj_count)
{
	i32 bit_map_cnt;
	i32 header_size;
	u64 slab_size;
	struct vm_slab* slab;

	size_order = get_max(size_order, allocator->_alignment_order);
	err_exit(size_order < allocator->_min_obj_size_order || size_order > allocator->_max_obj_size_order, "");

	bit_map_cnt = (obj_count + (VM_BITMAP_PER_CNT - 1)) / VM_BITMAP_PER_CNT;
	header_size = roundup_order(sizeof(struct vm_slab) + bit_map_cnt * sizeof(u64), allocator->_alignment_order);
	slab_size = header_size + (1 << size_order) * (obj_count + 1);

	slab = (struct vm_slab*)vpp_alloc(allocator->_page_pool, slab_size);
	err_exit(!slab || !_check_aligment(slab, allocator->_alignment_order), "");

	slab->_magic = VM_SLAB_MAGIC;
	slab->_obj_count = obj_count;
	slab->_obj_size_order = size_order;
	slab->_bit_map_cnt = bit_map_cnt;
	slab->_alloc_bit_map = (u64*)(slab + 1);
	slab->_obj_ptr = (void*)((char*)slab + header_size + (1 << size_order) - sizeof(struct vm_slab_cookie));
	slab->_end_addr = (void*)((char*)slab + slab_size);
	lst_clr(&slab->_list_node);

	for (i32 i = 0; i < bit_map_cnt; ++i)
		slab->_alloc_bit_map[i] = 0ULL;

	++allocator->_slab_cnt;

	return slab;
error_ret:
	return 0;
}

static inline struct vm_slab_cookie* _slab_alloc(struct vm_slab* slab)
{
	i32 bitmap_idx = 0;
	i64 obj_idx = -1;
	struct vm_slab_cookie* cookie;

	for (; bitmap_idx < slab->_bit_map_cnt; ++bitmap_idx)
	{
		obj_idx = bsf(~(slab->_alloc_bit_map[bitmap_idx]));
		if (obj_idx >= 0)
		{
			obj_idx += bitmap_idx * VM_BITMAP_PER_CNT;
			break;
		}
	}

	err_exit_silent(obj_idx < 0 || obj_idx < slab->_obj_count);

	slab->_alloc_bit_map[bitmap_idx] |= (1ULL << obj_idx);

	cookie = (struct vm_slab_cookie*)((char*)slab->_obj_ptr + (obj_idx << slab->_obj_size_order));
	cookie->_slab = slab;

	return cookie;
error_ret:
	return 0;
}

static inline void _slab_free(struct vm_slab_cookie* cookie)
{
	i32 bitmap_idx;
	i64 obj_idx;
	struct vm_slab* slab = cookie->_slab;

	obj_idx = (((i64)cookie - (i64)slab->_obj_ptr) >> slab->_obj_size_order);
	err_exit(obj_idx < 0 || obj_idx >= slab->_obj_count, "");

	bitmap_idx = obj_idx / 64;
	obj_idx = obj_idx % 64;

	err_exit(bitmap_idx >= slab->_bit_map_cnt, "");
	err_exit((slab->_alloc_bit_map[bitmap_idx] & (1ULL << obj_idx)) == 0, "");

	slab->_alloc_bit_map[bitmap_idx] &= ~(1ULL << obj_idx);

	return;
error_ret:
	return;
}


void* vsa_alloc(struct vm_slab_allocator* allocator, u64 size)
{
	struct vm_slab_list* slab_list;
	struct vm_slab* slab;
	struct vm_slab_cookie* cookie;

	i32 size_order = log_2(round_up_2power(size + sizeof(struct vm_slab_cookie)));

	slab_list = _get_slab_list(allocator, size_order);
	err_exit(!slab_list, "");

__retry_alloc:
	if (!lst_empty(&slab_list->_partial_list))
	{
		slab = _convert_list_node(lst_first(&slab_list->_partial_list));
		cookie = _slab_alloc(slab);

		if (_is_slab_full(slab))
		{
			lst_remove(&slab_list->_partial_list, &slab->_list_node);
			lst_push_front(&slab_list->_full_list, &slab->_list_node);
		}
	}
	else if (!lst_empty(&slab_list->_empty_list))
	{
		slab = _convert_list_node(lst_first(&slab_list->_empty_list));
		cookie = _slab_alloc(slab);

		lst_remove(&slab_list->_empty_list, &slab->_list_node);
		lst_push_front(&slab_list->_partial_list, &slab->_list_node);
		++slab_list->_out_empty_cnt;
	}
	else
	{
		slab = _new_slab(allocator, size_order, allocator->_init_obj_cnt);
		err_exit(!slab, "");

		cookie = _slab_alloc(slab);
		err_exit(!cookie, "");

		++slab_list->_out_empty_cnt;

		lst_push_front(&slab_list->_partial_list, &slab->_list_node);
	}

	err_exit(!cookie, "");

	return cookie + 1;
error_ret:
	return 0;
}

void vsa_free(struct vm_slab_allocator* allocator, void* p)
{
	struct vm_slab_cookie* cookie;
	struct vm_slab* slab;
	struct vm_slab_list* slab_list;

	err_exit(!allocator, "");
	err_exit(!p, "");

	cookie = (struct vm_slab_cookie*)((char*)p - sizeof(struct vm_slab_cookie));
	slab = cookie->_slab;
	err_exit(!slab || slab->_magic != VM_SLAB_MAGIC, "");
	err_exit(p < slab->_obj_ptr || p >= slab->_end_addr, "");
	err_exit(slab->_obj_size_order < allocator->_min_obj_size_order || slab->_obj_size_order > allocator->_max_obj_size_order, "");

	slab_list = _get_slab_list(allocator, slab->_obj_size_order);
	err_exit(!slab_list, "");

	_slab_free(cookie);

	lst_remove_node(&slab->_list_node);

	if (_is_slab_empty(slab))
	{
		lst_push_front(&slab_list->_empty_list, &slab->_list_node);
		++slab_list->_in_empty_cnt;
	}
	else
		lst_push_front(&slab_list->_partial_list, &slab->_list_node);

error_ret:
	return;
}

void vsa_debug_info(struct vm_slab_allocator* allocator)
{
//	LINKED_LIST_NODE* node;
//	LOG_PROCESS_ERROR(allocator);
//
//	INF("total slab cnt: %u", allocator->_slab_cnt);
//
//	for(i32 i = 0; i < allocator->_slab_list_cnt; ++i)
//	{
//		struct vm_slab_list* slab_list = &allocator->_slab_list[i];
//
//		INF("slab [%d], in_empty %d, out_empty %d", i, slab_list->_in_empty_cnt, slab_list->_out_empty_cnt);
//		INF("empty list:");
//
//		node = slab_list->_empty_list.head().pNext;
//		while(node != &slab_list->_empty_list.rear())
//		{
//			struct vm_slab* slab = _convert_list_node(node);
//			INF("\t obj size order: %u", slab->_obj_size_order);
//			INF("\t bitmap value:");
//
//			for(i32 j = 0; j < slab->_bit_map_cnt; ++j)
//			{
//				INF("%llx", slab->_alloc_bit_map[j]);
//			}
//			INF("-------------------------");
//
//			node = node->pNext;
//		}
//
//		INF("partial list:");
//
//		node = slab_list->_partial_list.head().pNext;
//		while(node != &slab_list->_partial_list.rear())
//		{
//			struct vm_slab* slab = _convert_list_node(node);
//			INF("\t obj size order: %u", slab->_obj_size_order);
//			INF("\t bitmap value:");
//
//			for(i32 j = 0; j < slab->_bit_map_cnt; ++j)
//			{
//				INF("%llx", slab->_alloc_bit_map[j]);
//			}
//			INF("-------------------------");
//
//			node = node->pNext;
//		}
//
//		INF("full list:");
//
//		node = slab_list->_full_list.head().pNext;
//		while(node != &slab_list->_full_list.rear())
//		{
//			struct vm_slab* slab = _convert_list_node(node);
//			INF("\t obj size order: %u", slab->_obj_size_order);
//			INF("\t bitmap value:");
//
//			for(i32 j = 0; j < slab->_bit_map_cnt; ++j)
//			{
//				INF("%llx", slab->_alloc_bit_map[j]);
//			}
//			INF("-------------------------");
//
//			node = node->pNext;
//		}
//	}

error_ret:
	return;
}

