#include "common_types.h"
#include "core/mmpool.h"
#include "core/mmops.h"
#include "core/dlist.h"
#include "core/misc.h"
#include "core/asm.h"
#include <stdio.h>

// absolutely not thread-safe.

//#define FREE_LIST_COUNT	(17)	// payload size: MIN_PAYLOAD_SIZE * (1 << list_idx) <list_idx: 0 ~ FREE_LIST_COUNT - 1>
#define CHUNK_LABEL (0xabab1212dfdf6969)
#define TAIL_LABEL (0x1a2b3c4d)


#pragma pack(1)
// 8 bytes payload head
struct _block_head
{
	unsigned _flag : 4;
	unsigned _fln_idx : 28;
	u32 _block_size;
};

// 8 bytes payload tail 
struct _block_tail
{
	u32 _tail_label;
	u32 _block_size;
};

//struct _chunk_head
//{
//	u64 _chunck_label;
//	u64 _addr_begin;
//	u64 _addr_end;
//};

#pragma pack()

#define BLOCK_BIT_USED			1
#define BLOCK_BIT_UNM			2
#define BLOCK_BIT_UNM_SUC		4	

#define PAGE_SIZE				0x1000

#define HEAD_TAIL_SIZE (sizeof(struct _block_head) + sizeof(struct _block_tail))

//#define MIN_BLOCK_SIZE (32)
//#define MAX_BLOCK_SIZE (MIN_BLOCK_SIZE * (1 << (FREE_LIST_COUNT - 1)))
//
//#define MIN_PAYLOAD_SIZE (MIN_BLOCK_SIZE - HEAD_TAIL_SIZE)
//#define MAX_PAYLOAD_SIZE (MAX_BLOCK_SIZE - HEAD_TAIL_SIZE)

struct _free_list_node
{
	union
	{
		struct dlnode _list_node;
		struct dlnode _free_fln_node;
	};

	struct _block_head* _block;
	i32 _idx;
}__attribute__((aligned(8)));

struct _free_list_head
{
	struct dlist _free_list;
	i32 _op_count;
}__attribute__((aligned(8)));

struct _mmpool_cfg
{
	u32 _min_block_order;
	u32 _max_block_order;

	i32 _free_list_count;
	i32 _min_payload_size;
	i32 _max_payload_size;
}__attribute__((aligned(8)));

struct _mmpool_impl
{
	u64 _chunck_label;
	struct mmpool _pool;
	struct _mmpool_cfg _cfg;

	void* _chunk_addr;
	i32 _chunk_size;

	u64 _fln_count;
	struct dlist _free_fln_list;
	struct _free_list_node* _fln_pool;
	struct _free_list_head* _flh;
}__attribute__((aligned(8)));

static inline struct _mmpool_impl* _conv_mmp(struct mmpool* pl)
{
	return (struct _mmpool_impl*)((u64)pl - (u64)&(((struct _mmpool_impl*)(0))->_pool));
}

static i32 _return_free_node_to_head(struct _mmpool_impl* mmpi, struct _block_head* hd);
static i32 _return_free_node_to_tail(struct _mmpool_impl* mmpi, struct _block_head* hd);
static i32 _take_free_node(struct _mmpool_impl* mmpi, i32 flh_idx, struct _block_head** bh);

static struct _mmpool_impl* _mmp_init_chunk(void* addr, u64 size, u32 min_block_order, u32 max_block_order);
static struct _mmpool_impl* _mmp_load_chunk(void* addr);


static void* __mmp_create_agent(void* addr, struct mm_config* cfg)
{
	return mmp_create(addr, cfg);
}

static void* __mmp_load_agent(void* addr)
{
	return mmp_load(addr);
}

static void __mmp_destroy_agent(void* alloc)
{
	mmp_destroy((struct mmpool*)alloc);
}

static void* __mmp_alloc_agent(void* alloc, u64 size)
{
	return mmp_alloc((struct mmpool*)alloc, size);
}

static i32 __mmp_free_agent(void* alloc, void* p)
{
	return mmp_free((struct mmpool*)alloc, p);
}

struct mm_ops __mmp_ops =
{
	.create_func = __mmp_create_agent,
	.load_func = __mmp_load_agent,
	.destroy_func = __mmp_destroy_agent,

	.alloc_func = __mmp_alloc_agent,
	.free_func = __mmp_free_agent,
};




static inline i32 _block_size(i32 idx)
{
	return (1 << idx);
}

static inline i32 _payload_size(i32 block_size)
{
	return block_size - HEAD_TAIL_SIZE;
}

static inline void* _payload_addr(void* block_addr)
{
	return block_addr + sizeof(struct _block_head);
}

static inline i32 _block_size_by_payload(i32 payload_size)
{
	return payload_size + HEAD_TAIL_SIZE;
}

static inline i32 _block_size_by_idx(struct _mmpool_impl* mmpi, i32 free_list_idx)
{
	return _block_size(mmpi->_cfg._min_block_order) * (1 << free_list_idx);
}

static inline struct _free_list_head* _get_flh(struct _mmpool_impl* mmpi, struct _block_head* bh)
{
	i32 idx = log_2(bh->_block_size) - mmpi->_cfg._min_block_order;

	if(idx >= mmpi->_cfg._free_list_count) goto error_ret;

	return &mmpi->_flh[idx];
error_ret:
	return 0;
}

static inline struct _free_list_node* _fetch_free_fln(struct _mmpool_impl* mmpi)
{
	struct dlnode* dln = lst_pop_front(&mmpi->_free_fln_list);
	struct _free_list_node* fln = (struct _free_list_node*)((void*)dln - (void*)(&((struct _free_list_node*)0)->_free_fln_node));

	fln->_block = 0;
	lst_clr(&fln->_list_node);

	return fln;
error_ret:
	return 0;
}

static inline i32 _return_free_fln(struct _mmpool_impl* mmpi, struct _free_list_node* fln)
{
	i32 rslt;
	fln->_block = 0;
	lst_clr(&fln->_free_fln_node);
	rslt = lst_push_front(&mmpi->_free_fln_list, &fln->_free_fln_node);
	if(rslt < 0) goto error_ret;

	return 0;
error_ret:
	return -1;
}

static inline void* _block_head(void* payload_addr)
{
	return (struct _block_head*)(payload_addr + sizeof(struct _block_head));
}

static inline struct _block_tail* _get_tail(struct _block_head* bh)
{
	return (struct _block_tail*)((void*)bh + bh->_block_size - sizeof(struct _block_tail));
}

static inline struct _block_head* _prev_block(struct _block_head* bh)
{
	struct _block_tail* tl = (struct _block_tail*)((void*)bh - sizeof(struct _block_tail));
	return (struct _block_head*)((void*)bh - tl->_block_size);
}

static inline struct _block_head* _next_block(struct _block_head* bh)
{
	return (struct _block_head*)((void*)bh + bh->_block_size);
}

static inline i32 _normalize_payload_size(i32 payload_size)
{
	i32 blk_size = _block_size_by_payload(payload_size);
	blk_size = round_up_2power(blk_size);
	return _payload_size(blk_size);
}

static inline i32 _normalize_block_size(i32 payload_size)
{
	i32 blk_size = _block_size_by_payload(payload_size);
	blk_size = round_up_2power(blk_size);

	return blk_size;
}

static inline i32 _flh_idx(struct _mmpool_impl* mmpi, i32 payload_size)
{
	i32 blk_size = _normalize_block_size(payload_size);
	i32 min_blk_size = _block_size(mmpi->_cfg._min_block_order);

	if(blk_size > _block_size(mmpi->_cfg._max_block_order)) goto error_ret;

	if(blk_size < min_blk_size)
		blk_size = min_blk_size;

	return log_2(blk_size) - mmpi->_cfg._min_block_order;
error_ret:
	return -1;
}

static inline void _make_block_tail(struct _block_tail* tail, u32 block_size)
{
	tail->_tail_label = TAIL_LABEL;
	tail->_block_size = block_size;
}

static inline struct _block_head* _make_block(void* addr, u32 block_size)
{
	struct _block_head* head = 0;
	struct _block_tail* tail = 0;

	if(!is_2power(block_size)) goto error_ret;

	head = (struct _block_head*)addr;
	head->_block_size = block_size;
	head->_flag = 0;

	tail = _get_tail(head);
	_make_block_tail(tail, block_size);

	return head;
error_ret:
	return 0;
}

static inline struct _block_head* _make_unmblock(void* addr, u32 block_size)
{
	struct _block_head* head = 0;
	struct _block_tail* tail = 0;

	head = (struct _block_head*)addr;
	head->_block_size = block_size;
	head->_flag = BLOCK_BIT_UNM;

	tail = _get_tail(head);
	_make_block_tail(tail, block_size);

	return head;
}

static i32 _return_free_node_to_head(struct _mmpool_impl* mmpi, struct _block_head* hd)
{
	i32 rslt;
	i32 flh_idx;
	struct _free_list_node* fln;

	if((hd->_flag & BLOCK_BIT_USED) != 0) goto error_ret;
	if(hd->_block_size > _block_size(mmpi->_cfg._max_block_order)) goto error_ret;

	flh_idx = log_2(hd->_block_size) - mmpi->_cfg._min_block_order;

	fln = _fetch_free_fln(mmpi);
	if(!fln) goto error_ret;

	fln->_block = hd;
	hd->_fln_idx = fln->_idx;
	rslt = lst_push_front(&mmpi->_flh[flh_idx]._free_list, &fln->_list_node);

	if(rslt < 0) goto error_ret;

	++mmpi->_flh[flh_idx]._op_count;

	return 0;
error_ret:
	return -1;
}

static i32 _return_free_node_to_tail(struct _mmpool_impl* mmpi, struct _block_head* hd)
{
	i32 rslt;
	i32 flh_idx;
	struct _free_list_node* fln;

	if((hd->_flag & BLOCK_BIT_USED) != 0) goto error_ret;
	if(hd->_block_size > _block_size(mmpi->_cfg._max_block_order)) goto error_ret;

	flh_idx = log_2(hd->_block_size) - mmpi->_cfg._min_block_order;

	fln = _fetch_free_fln(mmpi);
	if(!fln) goto error_ret;

	fln->_block = hd;
	hd->_fln_idx = fln->_idx;
	rslt = lst_push_back(&mmpi->_flh[flh_idx]._free_list, &fln->_list_node);

	if(rslt < 0) goto error_ret;

	++mmpi->_flh[flh_idx]._op_count;

	return 0;
error_ret:
	return -1;
}

// similar to the page-fault process
static i32 _take_free_node(struct _mmpool_impl* mmpi, i32 flh_idx, struct _block_head** rbh)
{
	struct _free_list_node* fln = 0;
	void* bh;
	i32 idx = flh_idx;

	*rbh = 0;

	while(idx < mmpi->_cfg._free_list_count && lst_empty(&mmpi->_flh[idx]._free_list))
	{
		++idx;
	}

	if(idx < mmpi->_cfg._free_list_count)
		fln = (struct _free_list_node*)lst_pop_front(&mmpi->_flh[idx]._free_list);

	if(!fln) goto error_ret;

	if(idx > flh_idx)
	{
		bh = fln->_block;
		struct _block_head* h;
		i32 new_size = ((struct _block_head*)bh)->_block_size;

		for(i32 i = 0; i < idx - flh_idx; ++i)
		{
			new_size >>= 1;
			h = bh + new_size;
			h = _make_block(h, new_size);
			_return_free_node_to_head(mmpi, h);
		}

		bh = _make_block(bh, new_size);
		_return_free_node_to_head(mmpi, bh);

		goto succ_ret;
	}

	*rbh = fln->_block;
	(*rbh)->_fln_idx = 0;

succ_ret:
	return _return_free_fln(mmpi, fln);
error_ret:
	return -1;
}

static i32 _try_spare_block(struct _mmpool_impl* mmpi, struct _block_head* bh, i32 payload_size)
{
	struct _block_head* h;
	i32 offset;
	void* end;

	if(bh->_flag != 0) goto error_ret;
	else if(!is_2power(bh->_block_size)) goto error_ret;

	payload_size = (i32)round_up(payload_size, 8);

	if(payload_size <= mmpi->_cfg._min_payload_size) goto succ_ret;

	end = (void*)bh + bh->_block_size;
	offset = (i32)bh->_block_size - HEAD_TAIL_SIZE - payload_size;

	if(offset > 0)
		offset = round_down_2power(offset);

	if(offset >= _block_size(mmpi->_cfg._min_block_order))
	{
		h = _make_block(end - offset, offset);
		h->_flag |= BLOCK_BIT_UNM_SUC;
		_return_free_node_to_head(mmpi, h);

		bh = _make_unmblock(bh, bh->_block_size - offset);
	}

	// remain block is the block for the payload, with buddy flag, so bh->_blk_size remains the original size;
	// bh is for the block head, and t is for the block tail.
	//

succ_ret:
	return 0;
error_ret:
	return -1;
}

static inline i32 _unlink_block(struct _mmpool_impl* mmpi, struct _block_head* bh)
{
	i32 rslt;
	struct _free_list_node* fln = &mmpi->_fln_pool[bh->_fln_idx];
	struct _free_list_head* flh = _get_flh(mmpi, bh);
	rslt = lst_remove(&flh->_free_list, &fln->_list_node);
	if(rslt < 0) goto error_ret;
	bh->_fln_idx = 0;

	return _return_free_fln(mmpi, fln);
error_ret:
	return -1;
}

static i32 _merge_free_blocks(struct _mmpool_impl* mmpi, struct _block_head* bh, i32 max_count)
{
	i32 rslt;
	i32 count = 0;
	i32 block_size = bh->_block_size;

	struct _free_list_node* fln;
	struct _free_list_head* flh;
	struct _block_head* nbh = _next_block(bh);

	if(nbh->_flag & BLOCK_BIT_USED) goto succ_ret;

	rslt = _unlink_block(mmpi, bh);
	if(rslt < 0) goto error_ret;

	while(count < max_count && block_size < _block_size(mmpi->_cfg._max_block_order))
	{
		struct _block_head* nbh = _next_block(bh);
		if(nbh->_flag != 0) break;

		rslt = _unlink_block(mmpi, nbh);
		if(rslt < 0) goto error_ret;

		block_size += nbh->_block_size;
	}

	if(!is_2power(block_size)) goto error_ret;

	_make_block(bh, block_size);

succ_ret:
	return count;
error_ret:
	return -1;
}

static struct _block_head* _merge_buddy_next(struct _mmpool_impl* mmpi, struct _block_head* bh, struct _block_head* nbh)
{
	i32 rslt;
	i64 offset1, offset2;
	i64 next_block_size;

	if(bh->_flag != 0 || nbh->_flag != 0) goto error_ret;

	offset1 = (i64)bh - (i64)mmpi->_chunk_addr;
	offset2 = (i64)nbh - (i64)mmpi->_chunk_addr;

	if(nbh->_flag != 0) goto error_ret;

	if(bh->_block_size != nbh->_block_size) goto error_ret;

	next_block_size = (bh->_block_size << 1);

	if((((i64)bh - (i64)mmpi->_chunk_addr) & (-next_block_size)) != (((i64)nbh - (i64)mmpi->_chunk_addr) & (-next_block_size)))
		goto error_ret;

	rslt = _unlink_block(mmpi, nbh);
	if(rslt < 0) goto error_ret;

	return _make_block(bh, next_block_size);
error_ret:
	return 0;
}

static struct _block_head* _merge_buddy_prev(struct _mmpool_impl* mmpi, struct _block_head* bh, struct _block_head* nbh)
{
	i32 rslt;
	i64 offset1, offset2;
	i64 next_block_size;

	if(bh->_flag != 0 || nbh->_flag != 0) goto error_ret;

	offset1 = (i64)bh - (i64)mmpi->_chunk_addr;
	offset2 = (i64)nbh - (i64)mmpi->_chunk_addr;

	if(nbh->_flag != 0) goto error_ret;

	if(bh->_block_size != nbh->_block_size) goto error_ret;

	next_block_size = (bh->_block_size << 1);

	if((((i64)bh - (i64)mmpi->_chunk_addr) & (-next_block_size)) != (((i64)nbh - (i64)mmpi->_chunk_addr) & (-next_block_size)))
		goto error_ret;

	rslt = _unlink_block(mmpi, bh);
	if(rslt < 0) goto error_ret;

	return _make_block(bh, next_block_size);
error_ret:
	return 0;
}

static struct _block_head* _merge_to_unmblock(struct _mmpool_impl* mmpi, struct _block_head* bh)
{
	i32 rslt;
	i32 block_size;
	i32 unm_used = 0;
	struct _block_head* nbh;

	if((bh->_flag & BLOCK_BIT_UNM) == 0)
		goto error_ret;

	if(bh->_flag & BLOCK_BIT_USED)
		unm_used = 1;

	nbh = _next_block(bh);
	if((nbh->_flag & BLOCK_BIT_UNM_SUC) == 0)
		goto error_ret;
	if((nbh->_flag & BLOCK_BIT_USED) != 0)
		goto error_ret;

	rslt = _unlink_block(mmpi, nbh);
	if(rslt < 0)
		goto error_ret;

	block_size = bh->_block_size + nbh->_block_size;
	if(!is_2power(block_size))
		goto error_ret;

	bh = _make_block(bh, block_size);
	if(unm_used)
		bh->_flag |= BLOCK_BIT_USED;

	return bh;
error_ret:
	printf("merge unmblock failed.\n");
	return 0;
}

static i32  _mmp_init_block(struct _mmpool_impl* mmpi, void* blk, i32 head_idx)
{
	struct _block_head* hd = _make_block(blk, _block_size_by_idx(mmpi, head_idx));
	if(!hd) goto error_ret;

	return _return_free_node_to_head(mmpi, hd);
error_ret:
	return -1;
}

static struct _mmpool_impl* _mmp_init_chunk(void* addr, u64 size, u32 min_block_order, u32 max_block_order)
{
	void* chk1;
	void* chk2 = 0;
	void* end;
	void* cur_section = addr;

	i32 blk_size;
	i32 rslt = -1;
	u64 min_block_size, max_block_size;

	struct _mmpool_impl* mmpi;

	min_block_size = 1 << min_block_order;
	max_block_size = 1 << max_block_order;

	if(min_block_size > max_block_size)
		goto error_ret;

	if(min_block_size < HEAD_TAIL_SIZE || max_block_size <= min_block_size)
		goto error_ret;

	mmpi = (struct _mmpool_impl*)cur_section;
	if(mmpi->_chunck_label == CHUNK_LABEL)
		goto error_ret;

	cur_section = move_ptr_align64(cur_section, sizeof(struct _mmpool_impl));

	mmpi->_chunck_label = CHUNK_LABEL;
	mmpi->_pool.addr_begin = addr;
	mmpi->_pool.addr_end = (void*)round_up((u64)addr + size, 8);

	mmpi->_cfg._min_block_order = min_block_order;
	mmpi->_cfg._max_block_order = max_block_order;
	mmpi->_cfg._free_list_count = max_block_order - min_block_order + 1;
	mmpi->_cfg._min_payload_size = min_block_size - HEAD_TAIL_SIZE;
	mmpi->_cfg._max_payload_size = max_block_size - HEAD_TAIL_SIZE;

	mmpi->_flh = (struct _free_list_head*)(cur_section);
	cur_section = move_ptr_align64(cur_section, mmpi->_cfg._free_list_count * sizeof(struct _free_list_head));

	mmpi->_fln_pool = (struct _free_list_node*)(cur_section);
	mmpi->_fln_count = (mmpi->_pool.addr_end - cur_section) / round_up(_block_size(min_block_order) + sizeof(struct _free_list_node), 8);

	cur_section = move_ptr_align64(cur_section, mmpi->_fln_count * sizeof(struct _free_list_node));

	lst_new(&mmpi->_free_fln_list);

	for(i32 i = 0; i < mmpi->_fln_count; ++i)
	{
		lst_clr(&mmpi->_fln_pool[i]._free_fln_node);
		mmpi->_fln_pool[i]._block = 0;
		mmpi->_fln_pool[i]._idx = i;
		lst_push_back(&mmpi->_free_fln_list, &mmpi->_fln_pool[i]._free_fln_node);
	}

	for(i32 i = 0; i < mmpi->_cfg._free_list_count; ++i)
	{
		mmpi->_flh[i]._op_count = 0;
		lst_new(&mmpi->_flh[i]._free_list);
	}

	if(cur_section >= mmpi->_pool.addr_end) goto error_ret;

	mmpi->_chunk_addr = cur_section;
	mmpi->_chunk_size = (mmpi->_pool.addr_end - cur_section) & (-_block_size(mmpi->_cfg._max_block_order));

	end = mmpi->_chunk_addr + mmpi->_chunk_size;
	chk1 = mmpi->_chunk_addr;

	for(i32 idx = mmpi->_cfg._free_list_count - 1; idx > 0 && chk1 < end && chk2 < end; idx >>= 1)
	{
		blk_size = _block_size_by_idx(mmpi, idx);

		chk2 = chk1 + blk_size;

		while(chk2 <= end)
		{
			rslt = _mmp_init_block(mmpi, chk1, idx);
			if(rslt < 0) goto error_ret;

			chk1 = chk2;
			chk2 = chk1 + blk_size;
		}
	}

	return mmpi;
error_ret:
	return 0;
}

static struct _mmpool_impl* _mmp_load_chunk(void* addr)
{
	void* cur_section;
	struct _mmpool_impl* mmpi;

	mmpi = _conv_mmp((struct mmpool*)(addr));
	if(mmpi->_chunck_label != CHUNK_LABEL) goto error_ret;
	if(mmpi->_pool.addr_begin != mmpi || mmpi->_pool.addr_end <= addr) goto error_ret;

	cur_section = move_ptr_align64(addr, sizeof(struct _mmpool_impl));

	return mmpi;
error_ret:
	return 0;
}

struct mmpool* mmp_create(void* addr, struct mm_config* cfg)
{
	struct _mmpool_impl* mmpi = 0;
	i32 result = 0;

	if(!addr) goto error_ret;

	// chunk must be 8-byte aligned.
	if((((u64)addr) & 7) != 0) goto error_ret;
	if(cfg->total_size < _block_size(cfg->min_order)) goto error_ret;
	if(cfg->total_size > 0xFFFFFFFF) goto error_ret;

	mmpi = _mmp_init_chunk(addr, cfg->total_size, cfg->min_order, cfg->max_order);

	if(!mmpi) goto error_ret;

	return &mmpi->_pool;

error_ret:

	return 0;
}

struct mmpool* mmp_load(void* addr)
{
	struct _mmpool_impl* mmpi;

	if((((u64)addr) & 7) != 0) goto error_ret;

	mmpi = _mmp_load_chunk(addr);

	if(!mmpi) goto error_ret;

	return &mmpi->_pool;
error_ret:
	return 0;
}

void mmp_destroy(struct mmpool* mmp)
{
	struct _mmpool_impl* mmpi = _conv_mmp(mmp);

	if(mmpi->_chunck_label != CHUNK_LABEL)
		goto error_ret;

	mmpi->_chunck_label = 0;
	mmpi->_pool.addr_begin = 0;
	mmpi->_pool.addr_end = 0;

	return;
error_ret:
	return;
}

void* mmp_alloc(struct mmpool* mmp, u64 payload_size)
{
	i32 rslt = 0;
	i32 flh_idx = 0;
	struct _mmpool_impl* mmpi = _conv_mmp(mmp);
	struct _block_head* bh = 0;

	if(payload_size <= 0 || payload_size > mmpi->_cfg._max_payload_size) goto error_ret;

	flh_idx = _flh_idx(mmpi, payload_size);
	if(flh_idx < 0) goto error_ret;

	do
	{
		rslt = _take_free_node(mmpi, flh_idx, &bh);
		if(rslt < 0) goto error_ret;
	}
	while(bh == 0);


	if((bh->_flag & BLOCK_BIT_USED) || (bh->_flag & BLOCK_BIT_UNM))
		goto error_ret;

	bh->_flag |= BLOCK_BIT_USED;

	return _payload_addr(bh);
error_ret:
	return 0;
}

i32 mmp_free(struct mmpool* mmp, void* p)
{
	i32 rslt = 0;
	i32 max_block_size;
	struct _mmpool_impl* mmpi = _conv_mmp(mmp);
	struct _block_head* bh;
	struct _block_tail* tl;
	struct _block_head* ret_bh;

	if(!p) goto error_ret;

	bh = (struct _block_head*)(p - sizeof(struct _block_head));

	tl = _get_tail(bh);

	if(tl->_block_size != bh->_block_size || tl->_tail_label != TAIL_LABEL)
		goto error_ret;
	
	if((bh->_flag & BLOCK_BIT_USED) == 0) goto error_ret;
	bh->_flag ^= BLOCK_BIT_USED;

	max_block_size = _block_size(mmpi->_cfg._max_block_order);
	while(bh->_block_size < max_block_size)
	{
		ret_bh = _merge_buddy_next(mmpi, bh, _next_block(bh));
		if(!ret_bh)
			ret_bh = _merge_buddy_prev(mmpi, _prev_block(bh), bh);

		if(!ret_bh)
			break;

		bh = ret_bh;
	}

	rslt = _return_free_node_to_head(mmpi, bh);
	if(rslt < 0) goto error_ret;

	return rslt;
succ_ret:
	return 0;
error_ret:
	return -1;
}

i32 mmp_check(struct mmpool* mmp)
{
	i32 sum_list_size = 0;
	i32 sum_free_size = 0, sum_used_size = 0;
	i32 free_list_free_size = 0;

	struct _block_head* h = 0;
	struct _mmpool_impl* mmpi = _conv_mmp(mmp);

	h = (struct _block_head*)(mmpi->_chunk_addr);

	while((void*)h < (void*)mmpi->_chunk_addr + mmpi->_chunk_size)
	{
		if((h->_flag & BLOCK_BIT_USED) == 0 && (h->_flag & BLOCK_BIT_UNM) == 0)
			sum_free_size += h->_block_size;

		if((h->_flag & BLOCK_BIT_UNM) != 0)
			printf("unm flag: %d, block: %u\n", h->_flag, h->_block_size);

		h = _next_block(h);
	}

	for(i32 i = 0; i < mmpi->_cfg._free_list_count; ++i)
	{
		struct dlnode* dln = mmpi->_flh[i]._free_list.head.next;
//		print("flh idx: [%ld], size: [%ld]\n", i, mmpi->_flh[i]._free_list.size);
		i32 block_size = _block_size_by_idx(mmpi, i);
		while(dln != &mmpi->_flh[i]._free_list.tail)
		{
			i32 bsbi = 0;
			struct _free_list_node* fln = (struct _free_list_node*)dln;
			struct _block_head* hd = (struct _block_head*)fln->_block;
			printf("\tflag: %d, block_size: %u, block_addr: [%lx]\n", hd->_flag, hd->_block_size, (u64)hd);

			if(hd->_flag != 0 || hd->_block_size != block_size)
				return -1;

			free_list_free_size += hd->_block_size;

			dln = dln->next;
		}
	}
	printf("free size: %d:%d\n", sum_free_size, free_list_free_size);

//	for(i32 i = 0; i < FREE_LIST_COUNT; ++i)
//	{
//		sum_list_size += mmpi->_flh[i]._free_list.size;
//	}
//
//	if(sum_list_size + mmpi->_free_fln_list.size != mmpi->_fln_count) goto error_ret;

	for(i32 i = 0; i < mmpi->_cfg._free_list_count; ++i)
	{
		lst_check(&mmpi->_flh[i]._free_list);
//		print("list[%ld]: node count[%ld], op_count[%ld].\n", i,
//				mmpi->_flh[i]._free_list.size, mmpi->_flh[i]._op_count);
	}

	lst_check(&mmpi->_free_fln_list);

	return 0;
error_ret:
	return -1;
}

i32 mmp_freelist_profile(struct mmpool* mmp)
{
	struct _mmpool_impl* mmpi = _conv_mmp(mmp);

	for(i32 i = 0; i < mmpi->_cfg._free_list_count; ++i)
	{
//		printf(">>> freelist [%ld], size [%ld].\n", i, mmpi->_flh[i]._free_list.size);

	}

	return 0;
error_ret:
	return -1;
}


