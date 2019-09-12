#include "common_types.h"
#include "core/pgpool.h"
#include "core/mmops.h"
#include "core/dlist.h"
#include "core/misc.h"
#include "core/asm.h"
#include "core/rbtree.h"
#include <stdio.h>

//#define PG_SIZE			(4096)
#define PG_SIZE_SHIFT	(12)

#define PGP_CHUNK_LABEL (0xbaba2121fdfd9696UL)
#define HASH_COUNT		(256)

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
		unsigned using : 2;
	};

	i32 _pg_count;
}__attribute__((aligned(8)));

struct _free_list_head
{
	struct dlist _free_list;
	i32 _op_count;
}__attribute__((aligned(8)));

struct _pgp_cfg
{
	u64 maxpg_count;
	u64 freelist_count;
	u64 pg_size;
}__attribute__((aligned(8)));

struct _pgpool_impl
{
	u64 _chunck_label;
	struct pgpool _the_pool;
	struct _pgp_cfg _cfg;

	void* _chunk_addr;
	i32 _chunk_pgcount;

	struct rbtree _pgn_tree;
	struct _free_list_head* _flh;

	struct dlist _free_pgn_list;
	struct _pg_node* _pgn_pool;

	u64 _alloc_count;
	u64 _free_count;

}__attribute__((aligned(8)));


static inline struct _pgpool_impl* _conv_impl(struct pgpool* pgp)
{
	return (struct _pgpool_impl*)((void*)pgp - (u64)(&((struct _pgpool_impl*)(0))->_the_pool));
}


static void* __pgp_create_agent(void* addr, struct mm_config* cfg)
{
	return pgp_create(addr, cfg);
}

static void* __pgp_load_agent(void* addr)
{
	return pgp_load(addr);
}

static void __pgp_destroy_agent(void* alloc)
{
	pgp_destroy((struct pgpool*)alloc);
}

static void* __pgp_alloc_agent(void* alloc, u64 size)
{
	return pgp_alloc((struct pgpool*)alloc, size);
}

static i32 __pgp_free_agent(void* alloc, void* p)
{
	return pgp_free((struct pgpool*)alloc, p);
}

static void __pgp_counts(void* alloc, u64* alloc_count, u64* free_count)
{
	struct _pgpool_impl* pgpi = _conv_impl((struct pgpool*)alloc);
	*alloc_count = pgpi->_alloc_count;
	*free_count = pgpi->_free_count;
}

struct mm_ops __pgp_ops =
{
	.create_func = __pgp_create_agent,
	.load_func = __pgp_load_agent,
	.destroy_func = __pgp_destroy_agent,

	.alloc_func = __pgp_alloc_agent,
	.free_func = __pgp_free_agent,
	.counts_func = __pgp_counts,
};


static inline struct _pg_node* _conv_rbn(struct rbnode* rbn)
{
	return (struct _pg_node*)((void*)rbn - (u64)(&((struct _pg_node*)(0))->_rb_node));
}

static inline struct _pg_node* _conv_fln(struct dlnode* fln)
{
	return (struct _pg_node*)((void*)fln - (u64)(&((struct _pg_node*)(0))->_fln_node));
}

static inline struct _pg_node* _conv_free_pgn(struct dlnode* fln)
{
	return (struct _pg_node*)((void*)fln - (u64)(&((struct _pg_node*)(0))->_free_pgn_node));
}

static inline void _set_payload(struct _pg_node* node, void* payload)
{
	node->_payload_addr = (void*)((u64)payload | node->using);
}

static inline void* _get_payload(struct _pg_node* node)
{
	return (void*)(((u64)(node->_payload_addr)) & (~3));
}

static inline struct _pg_node* _fetch_free_pgn(struct _pgpool_impl* pgpi)
{
	struct dlnode* dln = lst_pop_front(&pgpi->_free_pgn_list);
	struct _pg_node* pgn = _conv_free_pgn(dln);

	return pgn;
}

static inline i32 _return_free_pgn(struct _pgpool_impl* pgpi, struct _pg_node* pgn)
{
	return lst_push_front(&pgpi->_free_pgn_list, &pgn->_free_pgn_node);
}

static inline struct _pg_node* _fetch_fln(struct _pgpool_impl* pgpi, i32 flh_idx)
{
	struct _pg_node* pgn;
	struct dlnode* fln = lst_pop_front(&pgpi->_flh[flh_idx]._free_list);
	pgn = _conv_fln(fln);

//	if(lst_check(&pgpi->_flh[flh_idx]._free_list) < 0)
//		goto error_ret;

	return pgn;
error_ret:
	return 0;
}

static inline i32 _link_fln(struct _pgpool_impl* pgpi, struct _pg_node* pgn)
{
	i32 rslt;
	i32 flh_idx = log_2(pgn->_pg_count);

	lst_clr(&pgn->_fln_node);

	rslt = lst_push_front(&pgpi->_flh[flh_idx]._free_list, &pgn->_fln_node);

//	if(lst_check(&pgpi->_flh[flh_idx]._free_list) < 0)
//		goto error_ret;

	return rslt;
error_ret:
	return -1;
}

static inline i32 _unlink_fln(struct _pgpool_impl* pgpi, struct _pg_node* pgn)
{
	i32 rslt;
	i32 flh_idx = log_2(pgn->_pg_count);
	rslt = lst_remove(&pgpi->_flh[flh_idx]._free_list, &pgn->_fln_node);

//	if(lst_check(&pgpi->_flh[flh_idx]._free_list) < 0)
//		goto error_ret;

	return rslt;
error_ret:
	return -1;
}

static inline i32 _link_rbn(struct _pgpool_impl* pgpi, struct _pg_node* pgn)
{
	rb_fillnew(&pgn->_rb_node);
	pgn->_rb_node.key = _get_payload(pgn);

	return rb_insert(&pgpi->_pgn_tree, &pgn->_rb_node);
}

static inline void _unlink_rbn(struct _pgpool_impl* pgpi, struct _pg_node* pgn)
{
	rb_remove_node(&pgpi->_pgn_tree, &pgn->_rb_node);
}

static inline struct _pg_node* _pgn_from_payload(struct _pgpool_impl* pgpi, void* payload)
{
	struct rbnode* hot;
	struct rbnode* rbn;

	rbn = rb_search(&pgpi->_pgn_tree, payload, &hot);
	if(!rbn) goto error_ret;

	return _conv_rbn(rbn);
error_ret:
	return 0;
}

static i32 _take_free_node(struct _pgpool_impl* pgpi, i32 pg_count, struct _pg_node** pgn)
{
	i32 rslt;
	i32 flh_idx = log_2(pg_count);
	i32 idx = flh_idx;

	struct dlnode* dln;
	struct _pg_node* candi_pgn = 0;

	*pgn = 0;

	if(is_2power(pg_count) && !lst_empty(&pgpi->_flh[idx]._free_list))
	{
		*pgn = _fetch_fln(pgpi, idx);
		if(!(*pgn) || (*pgn)->using) goto error_ret;

		goto succ_ret;
	}

	for(idx = flh_idx; idx < pgpi->_cfg.freelist_count; ++idx)
	{
		if(lst_empty(&pgpi->_flh[idx]._free_list))
			continue;

		dln = pgpi->_flh[idx]._free_list.head.next;

		while(dln != &pgpi->_flh[idx]._free_list.tail)
		{
			candi_pgn = _conv_fln(dln);
			if(candi_pgn->_pg_count >= pg_count && !candi_pgn->using)
			{
				_unlink_fln(pgpi, candi_pgn);
				goto candi_found;
			}
			candi_pgn = 0;

			dln = dln->next;
		}
	}

candi_found:	

	if(!candi_pgn) goto error_ret;

	*pgn = candi_pgn;

	if(candi_pgn->_pg_count > pg_count)
	{
		struct _pg_node* new_pgn = _fetch_free_pgn(pgpi);
		_set_payload(new_pgn, _get_payload(candi_pgn) + pg_count * pgpi->_cfg.pg_size);
		new_pgn->_pg_count = candi_pgn->_pg_count - pg_count;
		candi_pgn->_pg_count = pg_count;

		_link_rbn(pgpi, new_pgn);
		_link_fln(pgpi, new_pgn);
	}

succ_ret:
	return 0;
error_ret:
	return -1;
}

static inline i32 _merge_free_node(struct _pgpool_impl* pgpi, struct _pg_node* prev, struct _pg_node* next)
{
	if(_get_payload(next) != _get_payload(prev) + prev->_pg_count * pgpi->_cfg.pg_size)
		goto error_ret;

	_unlink_fln(pgpi, next);
	_unlink_rbn(pgpi, next);

	prev->_pg_count = prev->_pg_count + next->_pg_count;

	_return_free_pgn(pgpi, next);

	return 0;
error_ret:
	return -1;
}

static i32 _return_free_node(struct _pgpool_impl* pgpi, struct _pg_node* pgn)
{
	i32 found = 0;
	i32 rslt = 0;
	struct rbnode* succ;
	struct _pg_node* succ_pgn;

	succ = rb_succ(&pgn->_rb_node);
	while(succ && rslt >= 0)
	{
		succ_pgn = _conv_rbn(succ);
		if(succ_pgn->using || pgn->_pg_count + succ_pgn->_pg_count > pgpi->_cfg.maxpg_count)
			break;

		rslt = _merge_free_node(pgpi, pgn, succ_pgn);
		succ = rb_succ(&pgn->_rb_node);
	}

succ_ret:
	_link_fln(pgpi, pgn);
	return 0;
error_ret:
	return -1;
}

static struct _pgpool_impl* _pgp_init_chunk(void* addr, u64 size, u32 maxpg_count, u32 pg_size)
{
	struct _pgpool_impl* pgpi;
	i32 remain_count, flh_idx, rslt;
	void* pg;
	void* cur_offset = addr;
	u64 chunk_pg_count;

	pgpi = (struct _pgpool_impl*)cur_offset;
	cur_offset = move_ptr_align64(cur_offset, sizeof(struct _pgpool_impl));

	pgpi->_chunck_label = PGP_CHUNK_LABEL;
	pgpi->_the_pool.addr_begin = addr;
	pgpi->_the_pool.addr_end = (void*)round_down((u64)addr + size, pg_size);

	lst_new(&pgpi->_free_pgn_list);
	rb_init(&pgpi->_pgn_tree, 0);

	pgpi->_cfg.maxpg_count = maxpg_count;
	pgpi->_cfg.freelist_count = log_2(maxpg_count) + 1;
	pgpi->_cfg.pg_size = pg_size;

	pgpi->_flh = (struct _free_list_head*)cur_offset;
	cur_offset += sizeof(struct _free_list_head) * pgpi->_cfg.freelist_count;

	for(i32 i = 0; i < pgpi->_cfg.freelist_count; ++i)
	{
		lst_new(&pgpi->_flh[i]._free_list);
	}

	pgpi->_chunk_pgcount = (pgpi->_the_pool.addr_end - cur_offset) / (pg_size+ sizeof(struct _pg_node));

	pgpi->_pgn_pool = (struct _pg_node*)cur_offset;
	cur_offset = move_ptr_align64(cur_offset, sizeof(struct _pg_node) * pgpi->_chunk_pgcount);
	cur_offset = (void*)round_up((u64)cur_offset, pg_size);

	pgpi->_chunk_addr = cur_offset;

	for(i32 i = 0; i < pgpi->_chunk_pgcount; ++i)
	{
		lst_clr(&pgpi->_pgn_pool[i]._free_pgn_node);
		lst_push_back(&pgpi->_free_pgn_list, &pgpi->_pgn_pool[i]._free_pgn_node);
	}

	chunk_pg_count = (pgpi->_the_pool.addr_end - cur_offset) / pg_size;

	remain_count = pgpi->_chunk_pgcount - pgpi->_cfg.maxpg_count;
	pg = pgpi->_chunk_addr;

	while(remain_count > 0)
	{
		struct _pg_node* pgn = _fetch_free_pgn(pgpi);

		_set_payload(pgn, pg);
		if(remain_count >= pgpi->_cfg.maxpg_count)
			pgn->_pg_count = pgpi->_cfg.maxpg_count;
		else
			pgn->_pg_count = remain_count;

		_link_rbn(pgpi, pgn);
		_link_fln(pgpi, pgn);

		if(rslt < 0) goto error_ret;

		pg += pgn->_pg_count * pg_size;
		remain_count -= pgn->_pg_count;
	}

	return pgpi;
error_ret:
	return 0;
}

static struct _pgpool_impl* _pgp_load_chunk(void* addr)
{
	void* cur_offset;
	struct _pgpool_impl* pgpi;

	pgpi = _conv_impl((struct pgpool*)addr);
	cur_offset = move_ptr_align64(addr, sizeof(struct _pgpool_impl));

	if(pgpi->_chunck_label != PGP_CHUNK_LABEL)
		goto error_ret;

	if(pgpi->_the_pool.addr_begin != pgpi || addr >= pgpi->_the_pool.addr_end)
		goto error_ret;

	return pgpi;
error_ret:
	return 0;
}

struct pgpool* pgp_create(void* addr, struct mm_config* cfg)
{
	i32 rslt = 0;
	struct _pgpool_impl* pgpi;

	if(!addr || ((u64)addr & 7) != 0 || cfg->total_size <= cfg->page_size) goto error_ret;

	if(!is_2power(cfg->page_size)) goto error_ret;

	pgpi = _pgp_init_chunk(addr, cfg->total_size, cfg->maxpg_count, cfg->page_size);
	if(!pgpi) goto error_ret;

	return &pgpi->_the_pool;
error_ret:
	if(pgpi)
		pgp_destroy(&pgpi->_the_pool);

	return 0;
}

struct pgpool* pgp_load(void* addr)
{
	struct _pgpool_impl* pgpi;

	if(!addr || ((u64)addr & 7) != 0) goto error_ret;

	pgpi = _pgp_load_chunk(addr);
	if(!pgpi) goto error_ret;

	return &pgpi->_the_pool;
error_ret:
	if(pgpi)
		pgp_destroy(&pgpi->_the_pool);

	return 0;
}

void pgp_destroy(struct pgpool* pgp)
{
	struct _pgpool_impl* pgpi = _conv_impl(pgp);

	if(pgpi && pgpi->_chunck_label == PGP_CHUNK_LABEL)
	{
		pgpi->_chunck_label = 0;
	}
}


void* pgp_alloc(struct pgpool* pgp, u64 size)
{
	void* payload;
	i32 flh_idx, rslt, pg_count;
	struct _pg_node* pgn;
	struct _pgpool_impl* pgpi = _conv_impl(pgp);

	pg_count = round_up(size, pgpi->_cfg.pg_size) / pgpi->_cfg.pg_size;

	rslt = _take_free_node(pgpi, pg_count, &pgn);
	err_exit(rslt < 0 || !pgn, "pgp_alloc: take free node error.");

	payload = _get_payload(pgn);
	pgn->using = 1;

	++pgpi->_alloc_count;

	return payload;
error_ret:
	return 0;
}

i32 pgp_free(struct pgpool* pgp, void* payload)
{
	i32 rslt;
	struct _pg_node* pgn;
	struct _pgpool_impl* pgpi = _conv_impl(pgp);

	if(((u64)payload & (pgpi->_cfg.pg_size - 1)) != 0)
		goto error_ret;

	pgn = _pgn_from_payload(pgpi, payload);
	if(!pgn || !pgn->using)
		goto error_ret;

	pgn->using = 0;
	rslt = _return_free_node(pgpi, pgn);
	if(rslt < 0) goto error_ret;

	++pgpi->_free_count;

	return 0;
error_ret:
	return -1;
}

i32 pgp_check(struct pgpool* pgp)
{
	i32 rslt = 0;
	struct _pgpool_impl* pgpi = _conv_impl(pgp);

	for(i32 i = 0; i < pgpi->_cfg.freelist_count; ++i)
	{
		rslt = lst_check(&pgpi->_flh[i]._free_list);
		if(rslt < 0)
		{
			printf("!!! loop in freelist [%ld] !!!\n", i);
			return -1;
		}
	}

	return rslt;
}

