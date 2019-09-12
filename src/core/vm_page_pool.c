#include "common_types.h"
#include "core/dlist.h"
#include "core/rbtree.h"
#include "core/vm_page_pool.h"
#include "core/misc.h"
#include "core/asm.h"
#include <stdio.h>


#define PG_SIZE_SHIFT	(12)

#define PGP_CHUNK_LABEL (0xbaba2121fdfd9696UL)

struct _pg_node
{
	union
	{
		struct dlnode _free_pgn_node;
		struct dlnode _fln_node;
	};

	struct rbnode _rb_node;

	union
	{
		void* _payload_addr;
		unsigned _using : 2;
	};

	u64 _pg_count;
};

struct _pgp_cfg
{
	u64 max_page_count_alloc;
	u64 freelist_count;
	u64 page_size_order;
};

struct vm_page_pool
{
	u64 _chunck_label;
	struct _pgp_cfg _cfg;

	void* addr_begin;
	void* addr_end;
	void* _chunk_addr;
	u64 _chunk_pgcount;

	struct rbtree _pgn_tree;
	struct dlist* _free_list;

	struct dlist _free_pgn_list;
	struct _pg_node* _pgn_pool;

	u64 _alloc_count;
	u64 _free_count;

};

static inline struct _pg_node* _conv_rbn(struct rbnode* rbn)
{
	return (struct _pg_node*)((u64)rbn - (u64)(&((struct _pg_node*)(0))->_rb_node));
}

static inline struct _pg_node* _conv_fln(struct dlnode* fln)
{
	return (struct _pg_node*)((u64)fln - (u64)(&((struct _pg_node*)(0))->_fln_node));
}

static inline struct _pg_node* _conv_free_pgn(struct dlnode* fln)
{
	return (struct _pg_node*)((u64)fln - (u64)(&((struct _pg_node*)(0))->_free_pgn_node));
}

static inline void _set_payload(struct _pg_node* node, void* payload)
{
	node->_payload_addr = (void*)((u64)payload | node->_using);
}

static inline void* _get_payload(struct _pg_node* node)
{
	return (void*)(((u64)(node->_payload_addr)) & (~3));
}

static inline u64 _get_page_size(u64 page_size_order)
{
	return 1ULL << page_size_order;
}

static inline struct _pg_node* _fetch_free_pgn(struct vm_page_pool* pool)
{
	struct dlnode* dln = lst_pop_front(&pool->_free_pgn_list);
	struct _pg_node* pgn = _conv_free_pgn(dln);

	return pgn;
}

static inline i32 _return_free_pgn(struct vm_page_pool* pool, struct _pg_node* pgn)
{
	return lst_push_front(&pool->_free_pgn_list, &pgn->_free_pgn_node);
}

static inline struct _pg_node* _fetch_fln(struct vm_page_pool* pool, u64 flh_idx)
{
	struct _pg_node* pgn;
	struct dlnode* fln = lst_pop_front(&pool->_free_list[flh_idx]);
	pgn = _conv_fln(fln);

	return pgn;
}

static inline i32 _link_fln(struct vm_page_pool* pool, struct _pg_node* pgn)
{
	u64 flh_idx = log_2(pgn->_pg_count);

	lst_clr(&pgn->_fln_node);

	return lst_push_front(&pool->_free_list[flh_idx], &pgn->_fln_node);
}

static inline i32 _unlink_fln(struct vm_page_pool* pool, struct _pg_node* pgn)
{
	u64 flh_idx = log_2(pgn->_pg_count);

	return lst_remove(&pool->_free_list[flh_idx], &pgn->_fln_node);
}

static inline i32 _link_rbn(struct vm_page_pool* pool, struct _pg_node* pgn)
{
	pgn->_rb_node.key = _get_payload(pgn);
	return rb_insert(&pool->_pgn_tree, &pgn->_rb_node);
}

static inline void _unlink_rbn(struct vm_page_pool* pool, struct _pg_node* pgn)
{
	rb_remove_node(&pool->_pgn_tree, &pgn->_rb_node);
}

static inline struct _pg_node* _pgn_from_payload(struct vm_page_pool* pool, void* payload)
{
	struct rbnode* rbn;
	struct rbnode* hot;

	rbn = rb_search(&pool->_pgn_tree, payload, &hot);
	err_exit(!rbn, "");

	return _conv_rbn(rbn);
error_ret:
	return NULL;
}

static i32 _take_free_node(struct vm_page_pool* pool, u64 pg_count, struct _pg_node** pgn)
{
	u64 flh_idx = log_2(pg_count);
	u64 idx = flh_idx;

	struct dlnode* dln;
	struct _pg_node* candi_pgn = 0;

	*pgn = 0;

	if(is_2power(pg_count) && !lst_empty(&pool->_free_list[flh_idx]))
	{
		*pgn = _fetch_fln(pool, idx);
		err_exit(!(*pgn) || (*pgn)->_using, "");
		goto Exit1;
	}

	for(idx = flh_idx; idx < pool->_cfg.freelist_count; ++idx)
	{
		if(lst_empty(&pool->_free_list[idx]))
			continue;

		dln = lst_first(&pool->_free_list[idx]);

		while(dln != lst_last(&pool->_free_list[idx]))
		{
			candi_pgn = _conv_fln(dln);
			if(candi_pgn->_pg_count >= pg_count && !candi_pgn->_using)
			{
				_unlink_fln(pool, candi_pgn);
				goto candi_found;
			}
			candi_pgn = 0;

			dln = dln->next;
		}
	}

candi_found:	

	err_exit(!candi_pgn, "");

	*pgn = candi_pgn;

	if(candi_pgn->_pg_count > pg_count)
	{
		struct _pg_node* new_pgn = _fetch_free_pgn(pool);
		_set_payload(new_pgn, (char*)_get_payload(candi_pgn) + pg_count * _get_page_size(pool->_cfg.page_size_order));
		new_pgn->_pg_count = candi_pgn->_pg_count - pg_count;
		candi_pgn->_pg_count = pg_count;

		_link_rbn(pool, new_pgn);
		_link_fln(pool, new_pgn);
	}

Exit1:
	return 0;
error_ret:
	return -1;
}

static inline i32 _merge_free_node(struct vm_page_pool* pool, struct _pg_node* prev, struct _pg_node* next)
{
	err_exit((char*)_get_payload(next) != (char*)_get_payload(prev) + prev->_pg_count * _get_page_size(pool->_cfg.page_size_order), "");

	_unlink_fln(pool, next);
	_unlink_rbn(pool, next);

	prev->_pg_count = prev->_pg_count + next->_pg_count;

	_return_free_pgn(pool, next);

	return 0;
error_ret:
	return -1;
}

static i32 _return_free_node(struct vm_page_pool* pool, struct _pg_node* pgn)
{
	i32 rslt = 0;
	struct rbnode* succ;
	struct _pg_node* succ_pgn;

	succ = rb_succ(&pgn->_rb_node);
	while(succ && rslt >= 0)
	{
		succ_pgn = _conv_rbn(succ);
		if(succ_pgn->_using || pgn->_pg_count + succ_pgn->_pg_count > pool->_cfg.max_page_count_alloc)
			break;

		rslt = _merge_free_node(pool, pgn, succ_pgn);
		succ = rb_succ(&pgn->_rb_node);
	}

	_link_fln(pool, pgn);

	return 0;
}

static struct vm_page_pool* _pgp_init_chunk(void* addr, u64 size, u64 max_page_count_alloc, u64 pg_size_order)
{
	struct vm_page_pool* pool;
	i64 remain_count;
	u64 chunk_pg_count;
	void* pg;
	void* cur_offset = addr;
	u64 pg_size = _get_page_size(pg_size_order);

	pool = (struct vm_page_pool*)cur_offset;
	cur_offset = move_ptr_align64(cur_offset, sizeof(struct vm_page_pool));

	pool->_chunck_label = PGP_CHUNK_LABEL;
	pool->addr_begin = addr;
	pool->addr_end = (void*)round_down((u64)addr + size, _get_page_size(pg_size_order));

	lst_new(&pool->_free_pgn_list);
	rb_init(&pool->_pgn_tree, 0);

	pool->_cfg.max_page_count_alloc = max_page_count_alloc;
	pool->_cfg.freelist_count = log_2(max_page_count_alloc) + 1;
	pool->_cfg.page_size_order = pg_size_order;

	pool->_free_list = (struct dlist*)cur_offset;
	cur_offset = (char*)cur_offset + sizeof(struct dlist) * pool->_cfg.freelist_count;

	for(u64 i = 0; i < pool->_cfg.freelist_count; ++i)
	{
		lst_new(&pool->_free_list[i]);
	}

	pool->_chunk_pgcount = ((u64)pool->addr_end - (u64)cur_offset) / (pg_size + sizeof(struct _pg_node));

	pool->_pgn_pool = (struct _pg_node*)cur_offset;
	cur_offset = move_ptr_align64(cur_offset, sizeof(struct _pg_node) * pool->_chunk_pgcount);
	cur_offset = (void*)round_up((u64)cur_offset, pg_size);

	pool->_chunk_addr = cur_offset;

	for(u64 i = 0; i < pool->_chunk_pgcount; ++i)
	{
		lst_clr(&pool->_pgn_pool[i]._free_pgn_node);
		lst_push_back(&pool->_free_pgn_list, &pool->_pgn_pool[i]._free_pgn_node);
	}

	chunk_pg_count = ((u64)pool->addr_end - (u64)cur_offset) / pg_size;

	remain_count = (i64)pool->_chunk_pgcount - (i64)pool->_cfg.max_page_count_alloc;
	pg = pool->_chunk_addr;


	while(remain_count > 0)
	{
		struct _pg_node* pgn = _fetch_free_pgn(pool);
		pgn->_payload_addr = 0;

		_set_payload(pgn, pg);
		if(remain_count >= (i64)pool->_cfg.max_page_count_alloc)
			pgn->_pg_count = pool->_cfg.max_page_count_alloc;
		else
			pgn->_pg_count = remain_count;

		_link_rbn(pool, pgn);
		_link_fln(pool, pgn);

		pg = (char*)pg + pgn->_pg_count * pg_size;
		remain_count -= pgn->_pg_count;
	}

	pool->_alloc_count = 0;
	pool->_free_count = 0;

	return pool;
}

static struct vm_page_pool* _pgp_load_chunk(void* addr)
{
	void* cur_offset;
	struct vm_page_pool* pool;

	pool = (struct vm_page_pool*)addr;
	cur_offset = move_ptr_align64(addr, sizeof(struct vm_page_pool));

	err_exit(pool->_chunck_label != PGP_CHUNK_LABEL, "");
	err_exit(pool->addr_begin != pool || addr >= pool->addr_end, "");

	return pool;
error_ret:
	return 0;
}

struct vm_page_pool* vpp_create(void* addr, u64 total_size, u64 page_size_k, u64 max_page_count_alloc)
{
	struct vm_page_pool* pool = NULL;
	u64 page_size = page_size_k * 1024;

	err_exit(!addr || ((u64)addr & 7) != 0 || total_size <= page_size, "");
	err_exit(total_size <= sizeof(struct vm_page_pool), "");

	pool = _pgp_init_chunk(addr, total_size, max_page_count_alloc, log_2(page_size));
	err_exit(!pool, "");

	return pool;
error_ret:
	if(pool)
		vpp_destroy(pool);

	return 0;
}

struct vm_page_pool* vpp_load(void* addr)
{
	struct vm_page_pool* pool;

	err_exit(!addr || ((u64)addr & 7) != 0, "");

	pool = _pgp_load_chunk(addr);
	err_exit(!pool, "");

	return pool;
error_ret:
	if(pool)
		vpp_destroy(pool);

	return 0;
}

void vpp_destroy(struct vm_page_pool* pool)
{
	if(pool && pool->_chunck_label == PGP_CHUNK_LABEL)
	{
		pool->_chunck_label = 0;
	}
}


void* vpp_alloc(struct vm_page_pool* pool, u64 size)
{
	void* payload;
	u64 pg_count;
	struct _pg_node* pgn;
	i32 rslt;

	pg_count = (round_up(size, _get_page_size(pool->_cfg.page_size_order)) >> pool->_cfg.page_size_order);

	rslt = _take_free_node(pool, pg_count, &pgn);
	err_exit(rslt < 0 || !pgn, "");

	payload = _get_payload(pgn);
	pgn->_using = 1;

	++pool->_alloc_count;

	return payload;
error_ret:
	return 0;
}

void* vpp_alloc_page(struct vm_page_pool* pool)
{
	return vpp_alloc(pool, 1);
}

void* vpp_realloc(struct vm_page_pool* pool, void* payload, u64 size)
{
	i32 rslt;
	struct _pg_node* pgn;
	u64 pg_count;

	err_exit(!payload, "");
	err_exit(((u64)payload & (_get_page_size(pool->_cfg.page_size_order) - 1)) != 0, "");

	pg_count = (round_up(size, _get_page_size(pool->_cfg.page_size_order)) >> pool->_cfg.page_size_order);

	pgn = _pgn_from_payload(pool, payload);
	err_exit(!pgn || !pgn->_using, "");

	if(pgn->_pg_count >= pg_count) goto Exit1;

	pgn->_using = 0;
	rslt = _return_free_node(pool, pgn);
	err_exit(rslt < 0, "");

	++pool->_free_count;

	return vpp_alloc(pool, size);

Exit1:
	return payload;
error_ret:
	return 0;
}

void* vpp_get_page(struct vm_page_pool* pool, void* ptr)
{
	return NULL;
}

int vpp_free(struct vm_page_pool* pool, void* payload)
{
	i32 rslt;
	struct _pg_node* pgn;

	err_exit(((u64)payload & (_get_page_size(pool->_cfg.page_size_order) - 1)) != 0, "");

	pgn = _pgn_from_payload(pool, payload);
	err_exit(!pgn || !pgn->_using, "");

	pgn->_using = 0;
	rslt = _return_free_node(pool, pgn);
	err_exit(rslt < 0, "");

	++pool->_free_count;

	return 0;
error_ret:
	return -1;
}

i32 vpp_check(struct vm_page_pool* pgp)
{
	i32 rslt = 0;
//	struct _pgpool_impl* pool = _conv_impl(pgp);

//	for(i32 i = 0; i < pool->_cfg.freelist_count; ++i)
//	{
//		rslt = lst_check(&pool->_flh[i]._free_list);
//		if(rslt < 0)
//		{
//			printf("!!! loop in freelist [%ld] !!!\n", i);
//			return -1;
//		}
//	}

	return rslt;
}

