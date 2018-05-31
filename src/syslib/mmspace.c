#include "syslib/mmspace.h"
#include "syslib/shmem.h"
#include "syslib/dlist.h"
#include "syslib/rbtree.h"
#include "syslib/misc.h"
#include "syslib/mmops.h"
#include "syslib/hash.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>

#define MM_LABEL (0x6666666666666666UL)
#define SLAB_LABEL (0x22222222UL)
#define CACHE_OBJ_COUNT (32)
#define CACHE_OBJ_COUNT_MAX (64)
#define ZONE_NAME_LEN (32)
#define ZONE_HASH_SIZE (64)

struct _mm_section_impl
{
	int _area_type;
	int _padding;

	void* _allocator;

}__attribute__((aligned(8)));

struct _mm_area_impl
{
	struct dlist _section_list;
	struct _mm_section_impl* _free_section;

}__attribute__((aligned(8)));


struct _mm_shmm_save
{
	void* _base_addr;
	int _key;
	int _size;

}__attribute__((aligned(8)));

struct _mm_space_impl
{
	unsigned long _mm_label;
	int _next_shmm_key;
	int _total_shmm_count;

	struct shmm_blk* _this_shm;
	struct mm_space_config _cfg;
	struct _mm_area_impl _area_list[MM_AREA_COUNT];

	struct rbtree _all_section_tree;
	struct dlist _zone_list;
	struct _mm_shmm_save* _shmm_save_list;
	struct hash_table _zone_hash;
	void* _usr_globl;

}__attribute__((aligned(8)));

struct _mmcache_impl
{
	struct mmcache _the_zone;
	unsigned long _cache_size;
	unsigned long _obj_aligned_size;

	mmcache_obj_ctor _obj_ctor;
	mmcache_obj_dtor _obj_dtor;

	struct dlist _full_slab_list;
	struct dlist _empty_slab_list;
	struct dlist _partial_slab_list;

	struct dlnode _list_node;
	struct hash_node _hash_node;
};

static struct _mm_space_impl* __the_mmspace = 0;

extern struct mm_ops __mmp_ops;
extern struct mm_ops __pgp_ops;
extern struct mm_ops __stkp_ops;

static struct mm_ops* __mm_area_ops[MM_AREA_COUNT] =
{
	[MM_AREA_NUBBLE] = &__mmp_ops,
	[MM_AREA_PAGE] = &__pgp_ops,
	[MM_AREA_CACHE] = &__pgp_ops,
	[MM_AREA_PERSIS] = &__mmp_ops,
	[MM_AREA_STACK] = &__stkp_ops,
};

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
// mmcache:
//

struct _mm_cache
{
	struct _mmcache_impl* _cache;
	struct dlnode _list_node;

	unsigned int _slab_label;
	unsigned short _free_count;
	unsigned short _obj_count;

	unsigned long _alloc_bits;

	void* _obj_ptr;

}__attribute__((aligned(8)));


static inline struct _mmcache_impl* _conv_zone_impl(struct mmcache* mmz)
{
	return (struct _mmcache_impl*)((unsigned long)mmz - (unsigned long)&((struct _mmcache_impl*)(0))->_the_zone);
}

static inline struct _mmcache_impl* _conv_zone_lst_node(struct dlnode* dln)
{
	return (struct _mmcache_impl*)((unsigned long)dln- (unsigned long)&((struct _mmcache_impl*)(0))->_list_node);
}

static inline struct _mmcache_impl* _conv_zone_hash_node(struct hash_node* hn)
{
	return (struct _mmcache_impl*)((unsigned long)hn- (unsigned long)&((struct _mmcache_impl*)(0))->_hash_node);
}

static inline struct _mm_cache* _conv_cache(struct dlnode* dln)
{
	return (struct _mm_cache*)((unsigned long)dln- (unsigned long)&((struct _mm_cache*)(0))->_list_node);
}

static struct _mm_cache* _cache_of_obj(void* obj)
{
	void* p = (void*)round_down((unsigned long)obj, __the_mmspace->_cfg.mm_cfg[MM_AREA_CACHE].page_size);

	for(unsigned int i = 0; i < __the_mmspace->_cfg.mm_cfg[MM_AREA_CACHE].maxpg_count; ++i)
	{
		struct _mm_cache* mmc = (struct _mm_cache*)(p - i * __the_mmspace->_cfg.mm_cfg[MM_AREA_CACHE].page_size);
		if(mmc->_slab_label == SLAB_LABEL)
			return mmc;
	}

	return 0;
}

static inline long _mm_zcache_full(struct _mm_cache* mc)
{
	return mc->_free_count <= 0;
}

static inline long _mm_zcache_empty(struct _mm_cache* mc)
{
	return mc->_free_count >= mc->_obj_count;
}

static inline int _mmc_next_free_obj(struct _mm_cache* mc)
{
	int idx = (int)bsf(~(mc->_alloc_bits));

	if(idx >= mc->_obj_count) goto error_ret;

	return idx;
error_ret:
	return -1;
}

static long _check_alloc_bits(struct _mm_cache* mc)
{
	int count = 0;

	for(int i = 0; i < 64; ++i)
	{
		if((mc->_alloc_bits & (1UL << i)) != 0)
			++count;
	}

	err_exit(count != (mc->_obj_count - mc->_free_count), "fk");

	return 0;
error_ret:
	return -1;
}

static inline void* _mm_zfetch_obj(struct _mm_cache* mc)
{
	void* p;
	int idx = _mmc_next_free_obj(mc);

	if(idx < 0) goto error_ret;

	p = mc->_obj_ptr + idx * mc->_cache->_the_zone.obj_size;

//	err_exit(_check_alloc_bits(mc) < 0, "check failed");

	mc->_alloc_bits |= (1UL << idx);
	--mc->_free_count;

//	err_exit(_check_alloc_bits(mc) < 0, "check failed");

	return p;
error_ret:
	return 0;
}

static inline long _mm_zreturn_obj(struct _mm_cache* mc, void* p)
{
	long idx;

	err_exit(p < mc->_obj_ptr, "invalid p.");

	idx = (p - mc->_obj_ptr) / mc->_cache->_the_zone.obj_size;

	if(idx < 0 || idx >= mc->_obj_count) goto error_ret;

	err_exit((mc->_alloc_bits & (1UL << idx)) == 0, "return twice!!");

//	err_exit(_check_alloc_bits(mc) < 0, "check failed");

//	printf("alloc bits: 0x%lx\n", mc->_alloc_bits);

	++mc->_free_count;
	mc->_alloc_bits &= ~(1UL << idx);

//	err_exit(_check_alloc_bits(mc) < 0, "check failed");

	return 0;
error_ret:
	return -1;
}


static inline long _mm_zmove_cache(struct _mm_cache* mc, struct dlist* from_list, struct dlist* to_list)
{
	long rslt;

	if(from_list)
	{
		rslt = lst_remove(from_list, &mc->_list_node);
		if(rslt < 0) goto error_ret;
	}

	return lst_push_front(to_list, &mc->_list_node);
error_ret:
	return -1;
}

static inline struct _mm_cache* _mm_zfirst_cache_from_list(struct dlist* from_list)
{
	struct dlnode* dln = from_list->head.next;
	if(dln == &from_list->tail) goto error_ret;

	return  _conv_cache(dln);
error_ret:
	return 0;
}

static void _mm_ztry_recall_empty_cache(struct _mmcache_impl* mzi)
{
	struct dlnode* dln = mzi->_empty_slab_list.head.next;
	while(dln != &mzi->_empty_slab_list.tail)
	{
		struct _mm_cache* mc = _conv_cache(dln);
		mm_free(mc);
		dln = dln->next;
	}

	lst_new(&mzi->_empty_slab_list);
}

static void _mm_ztry_recall(void)
{
	struct dlnode* dln = __the_mmspace->_zone_list.head.next;
	while(dln != &__the_mmspace->_zone_list.tail)
	{
		struct _mmcache_impl* mzi = _conv_zone_lst_node(dln);
		_mm_ztry_recall_empty_cache(mzi);
		dln = dln->next;
	}
}

static inline void* _mm_zalloc_pg(struct _mmcache_impl* mzi)
{
	unsigned long alloc_size = mzi->_the_zone.obj_size * CACHE_OBJ_COUNT + sizeof(struct _mm_cache); 
	void* pg = mm_area_alloc(alloc_size, MM_AREA_CACHE);
	if(!pg)
	{
		_mm_ztry_recall();
		pg = mm_area_alloc(alloc_size, MM_AREA_CACHE);
	}

	return pg;
}


static struct _mm_cache* _mm_zcache_new(struct _mmcache_impl* mzi)
{
	struct _mm_cache* mc = (struct _mm_cache*)_mm_zalloc_pg(mzi);
	if(!mc) goto error_ret;

	mc->_slab_label = SLAB_LABEL;
	mc->_cache = mzi;
	lst_clr(&mc->_list_node);

	mc->_obj_count = (mzi->_cache_size - sizeof(struct _mm_cache)) / mzi->_the_zone.obj_size;
	mc->_obj_count = mc->_obj_count <= CACHE_OBJ_COUNT_MAX ? mc->_obj_count : CACHE_OBJ_COUNT_MAX;
	mc->_obj_ptr = (void*)mc + mzi->_cache_size - mc->_obj_count * mzi->_the_zone.obj_size;

	mc->_alloc_bits = 0;
	mc->_free_count = mc->_obj_count;

	__builtin_prefetch(mc->_obj_ptr);

	_mm_zmove_cache(mc, 0, &mzi->_empty_slab_list);

	return mc;
error_ret:
	if(mc)
		mm_free(mc);
	return 0;
}


struct mmcache* mm_cache_create(const char* name, unsigned int obj_size, mmcache_obj_ctor ctor, mmcache_obj_dtor dtor)
{
	long rslt;
	unsigned long cache_size;
	struct _mmcache_impl* mzi;
	struct hash_node* hn;
	if(!__the_mmspace) goto error_ret;

	hn = hash_search(&__the_mmspace->_zone_hash, name);
	if(hn) goto error_ret;

	obj_size = round_up(obj_size, 8);

	cache_size = round_up(obj_size * CACHE_OBJ_COUNT + sizeof(struct _mm_cache), __the_mmspace->_cfg.mm_cfg[MM_AREA_CACHE].page_size);

	if(cache_size >= __the_mmspace->_cfg.mm_cfg[MM_AREA_CACHE].page_size * __the_mmspace->_cfg.mm_cfg[MM_AREA_CACHE].maxpg_count)
		goto error_ret;

	mzi = mm_area_alloc(sizeof(struct _mmcache_impl), MM_AREA_PERSIS);
	if(!mzi) goto error_ret;

	lst_new(&mzi->_empty_slab_list);
	lst_new(&mzi->_partial_slab_list);
	lst_new(&mzi->_full_slab_list);
	lst_clr(&mzi->_list_node);

	mzi->_the_zone.obj_size = obj_size;
	mzi->_cache_size = cache_size;

	mzi->_obj_ctor = ctor;
	mzi->_obj_dtor = dtor;

	strncpy(mzi->_hash_node.hash_key, name, HASH_KEY_LEN);

	rslt = lst_push_back(&__the_mmspace->_zone_list, &mzi->_list_node);
	if(rslt < 0) goto error_ret;

	rslt = hash_insert(&__the_mmspace->_zone_hash, &mzi->_hash_node);
	if(rslt < 0) goto error_ret;

	return &mzi->_the_zone;
error_ret:
	if(mzi)
		mm_free(mzi);
	return 0;
}

int mm_cache_destroy(struct mmcache* mmz)
{
error_ret:
	return -1;
}

void* mm_cache_alloc(struct mmcache* mmz)
{
	void* p;
	struct _mm_cache* mc;
	struct _mmcache_impl* mzi = _conv_zone_impl(mmz);

	if(!lst_empty(&mzi->_partial_slab_list))
	{
		mc = _mm_zfirst_cache_from_list(&mzi->_partial_slab_list);
		if(!mc) goto error_ret;

		p = _mm_zfetch_obj(mc);
		if(!p) goto error_ret;

		if(_mm_zcache_full(mc))
			_mm_zmove_cache(mc, &mzi->_partial_slab_list, &mzi->_full_slab_list);
	}
	else
	{
		mc = _mm_zfirst_cache_from_list(&mzi->_empty_slab_list);
		if(!mc)
		{
			mc = _mm_zcache_new(mzi);
			if(!mc) goto error_ret;
		}

		p = _mm_zfetch_obj(mc);
		if(!p) goto error_ret;

		_mm_zmove_cache(mc, &mzi->_empty_slab_list, &mzi->_partial_slab_list);
	}

	if(mzi->_obj_ctor)
		(*mzi->_obj_ctor)(p);

	return p;
error_ret:
	return 0;
}

long mm_cache_free(struct mmcache* mmz, void* p)
{
	long rslt;
	long from_full;
	struct _mm_cache* mc;
	struct _mmcache_impl* mzi = _conv_zone_impl(mmz);

	if(!p) goto error_ret;

	if(mzi->_obj_dtor)
		(*mzi->_obj_dtor)(p);

	mc = _cache_of_obj(p);
	if(!mc || _mm_zcache_empty(mc)) goto error_ret;

	from_full = _mm_zcache_full(mc);

	rslt = _mm_zreturn_obj(mc, p);
	if(rslt < 0) goto error_ret;

	if(_mm_zcache_empty(mc))
		_mm_zmove_cache(mc, &mzi->_partial_slab_list, &mzi->_empty_slab_list);

	if(from_full)
		_mm_zmove_cache(mc, &mzi->_full_slab_list, &mzi->_partial_slab_list);

	return 0;
error_ret:
	return -1;
}

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
// mmspace:
//

static inline int _make_shmm_key(struct _mm_space_impl* mm, int ar_type, int area_idx)
{
	return  ((mm->_cfg.sys_shmm_key) << 16) + (ar_type << 8) + area_idx;
}

static inline struct shmm_blk* _conv_shmm_from_rbn(struct rbnode* rbn)
{
	return (struct shmm_blk*)((unsigned long)rbn - (unsigned long)&((struct shmm_blk*)(0))->rb_node);
}

static inline struct shmm_blk* _conv_shmm_from_dln(struct dlnode* dln)
{
	return (struct shmm_blk*)((unsigned long)dln - (unsigned long)&((struct shmm_blk*)(0))->lst_node);
}

static inline struct _mm_section_impl* _get_section(struct shmm_blk* shm)
{
	return (struct _mm_section_impl*)shmm_begin_addr(shm);
}

static long _shmm_comp_func(void* key, struct rbnode* rbn)
{
	struct shmm_blk* shm = _conv_shmm_from_rbn(rbn);
	void* addr_begin = shmm_begin_addr(shm);
	void* addr_end = shmm_end_addr(shm);

	if(key < addr_begin) return -1;
	else if(key >= addr_end) return 1;

	return 0;
}

static long _mm_load_area(struct _mm_space_impl* mm)
{
	long rslt;
	struct dlnode* dln;

	for(int i = 0; i < mm->_total_shmm_count; ++i)
	{
		printf("load section shmm_key: 0x%x\n", mm->_shmm_save_list[i]._key);
		struct shmm_blk* shm = shmm_open(mm->_shmm_save_list[i]._key, mm->_shmm_save_list[i]._base_addr);
		if(!shm) goto error_ret;
	}

	for(int i = MM_AREA_BEGIN; i < MM_AREA_COUNT; ++i)
	{
		dln = mm->_area_list[i]._section_list.head.next;

		while(dln != &mm->_area_list[i]._section_list.tail)
		{
			struct shmm_blk* shm = _conv_shmm_from_dln(dln);

			struct _mm_section_impl* sec = _get_section(shm);

			sec->_allocator = (*__mm_area_ops[i]->load_func)(sec->_allocator);
			if(!sec->_allocator) goto error_ret;

			dln = dln->next;
		}
	}

	return 0;
error_ret:
	return -1;
}

static long _mm_create_section(struct _mm_space_impl* mm, int ar_type)
{
	long rslt;
	int shmm_key;
	struct _mm_area_impl* ar;
	struct _mm_section_impl* sec = 0;
	struct shmm_blk* shm = 0;
	union shmm_sub_key sub_key;
	void* addr_begin;

	err_exit(mm->_total_shmm_count >= mm->_cfg.max_shmm_count, "too much shmm section.");

	sub_key.ar_type = ar_type;
	sub_key.ar_idx = ++mm->_next_shmm_key;

	shmm_key = mm_create_shm_key(MM_SHM_MEMORY_SPACE, mm->_cfg.sys_shmm_key, &sub_key);

	shm = shmm_create(shmm_key, 0, mm->_cfg.mm_cfg[ar_type].total_size, mm->_cfg.try_huge_page);
	err_exit(!shm, "create shmm error.");

	ar = &mm->_area_list[ar_type];

	addr_begin = shmm_begin_addr(shm);
	err_exit(!addr_begin, "shmm begin addr error.");

	lst_new(&ar->_section_list);
	
	rb_fillnew(&shm->rb_node);
	shm->rb_node.key = addr_begin;
	rslt = rb_insert(&mm->_all_section_tree, &shm->rb_node);
	err_exit(rslt < 0, "shmm rb insert error.");

	lst_clr(&shm->lst_node);
	rslt = lst_push_back(&ar->_section_list, &shm->lst_node);
	err_exit(rslt < 0, "shmm link error.");


	sec = (struct _mm_section_impl*)addr_begin;

	sec->_area_type = ar_type;
	sec->_padding = 0;

	sec->_allocator = (*__mm_area_ops[ar_type]->create_func)(addr_begin + sizeof(struct _mm_section_impl), &mm->_cfg.mm_cfg[ar_type]);
	err_exit(!sec->_allocator, "shmm create allocator error.");
	printf("created allocator: %p, type[%d].\n", sec->_allocator, ar_type);

	ar->_free_section = sec;

	mm->_shmm_save_list[mm->_total_shmm_count]._base_addr = shm;
	mm->_shmm_save_list[mm->_total_shmm_count]._key = shmm_key;
	mm->_shmm_save_list[mm->_total_shmm_count]._size = mm->_cfg.mm_cfg[ar_type].total_size;

	++mm->_total_shmm_count;

	printf("new section shmm_key: 0x%x, size: %lu\n", shmm_key, mm->_cfg.mm_cfg[ar_type].total_size);

	return 0;
error_ret:
	if(shm)
		shmm_destroy(shm);
	return -1;

}

long mm_initialize(struct mm_space_config* cfg)
{
	long rslt;
	unsigned long shm_size;
	struct shmm_blk* shm;
	struct _mm_space_impl* mm;
	void* addr_begin;

	if(__the_mmspace || !cfg || cfg->max_shmm_count <= 0) goto error_ret;

	/* low mem                                                        high mem
	 *   || struct _mm_space_impl || _zone_hash_list | _shmm_save_list |
	 */

	shm_size = sizeof(struct _mm_space_impl) + sizeof(struct _mm_shmm_save) * cfg->max_shmm_count + sizeof(struct dlist) * ZONE_HASH_SIZE;

	shm = shmm_open_raw(cfg->sys_shmm_key, (void*)cfg->sys_begin_addr);
	if(shm)
	{
		addr_begin = shmm_begin_addr(shm);
		if(!addr_begin) goto error_ret;

		mm = (struct _mm_space_impl*)addr_begin;
		if(mm->_mm_label != MM_LABEL) goto error_ret;

		rslt = _mm_load_area(mm);
		if(rslt < 0) goto error_ret;

		rb_reset_compare_function(&mm->_all_section_tree, _shmm_comp_func);

		goto succ_ret;
	}

	shm = shmm_create(cfg->sys_shmm_key, (void*)cfg->sys_begin_addr, shm_size, cfg->try_huge_page);
	if(!shm) goto error_ret;

	addr_begin = shmm_begin_addr(shm);
	if(!addr_begin) goto error_ret;

	mm = (struct _mm_space_impl*)(addr_begin);
	memcpy(&mm->_cfg, cfg, sizeof(struct mm_space_config));
	mm->_mm_label = MM_LABEL;

	rb_init(&mm->_all_section_tree, _shmm_comp_func);
	lst_new(&mm->_zone_list);

	mm->_this_shm = shm;
	mm->_total_shmm_count = 0;
	mm->_usr_globl = 0;
	mm->_zone_hash.hash_list = (struct dlist*)((void*)mm + sizeof(struct _mm_space_impl));
	mm->_zone_hash.bucket_size = ZONE_HASH_SIZE;

	for(int i = 0; i < ZONE_HASH_SIZE; ++i)
	{
		lst_new(&mm->_zone_hash.hash_list[i]);
	}


	mm->_shmm_save_list = (struct _mm_shmm_save*)(&mm->_zone_hash.hash_list[ZONE_HASH_SIZE]);

	for(int i = MM_AREA_BEGIN; i < MM_AREA_COUNT; ++i)
	{
		rslt = _mm_create_section(mm, i);
		if(rslt < 0) goto error_ret;
	}

succ_ret:
	__the_mmspace = mm;
	return 0;
error_ret:
	mm_uninitialize();
	return -1;
}

long mm_reinitialize(struct mm_space_config* cfg)
{
	long rslt = mm_initialize(cfg);
	if(rslt < 0) goto error_ret;

	mm_uninitialize();

	return mm_initialize(cfg);
error_ret:
	return -1;
}

long mm_uninitialize(void)
{
	if(!__the_mmspace) goto error_ret;

	for(int i = 0; i < __the_mmspace->_total_shmm_count; ++i)
	{
		struct shmm_blk* shm = (struct shmm_blk*)__the_mmspace->_shmm_save_list[i]._base_addr;
		if(!shm) continue;

		shmm_destroy(shm);
	}

	shmm_destroy(__the_mmspace->_this_shm);

	__the_mmspace = 0;

	return 0;
error_ret:
	return -1;
}

inline void mm_save_globl_data(void* p)
{
	if(!__the_mmspace) goto error_ret;

	__the_mmspace->_usr_globl = p;
error_ret:
	return;
}

inline void* mm_load_globl_data(void)
{
	if(!__the_mmspace) goto error_ret;

	return __the_mmspace->_usr_globl;
error_ret:
	return 0;
}

struct mmcache* mm_search_zone(const char* zone_name)
{
	struct _mmcache_impl* mzi;
	struct hash_node* hn;

	hn = hash_search(&__the_mmspace->_zone_hash, zone_name);
	if(!hn) goto error_ret;

	mzi = _conv_zone_hash_node(hn);

	return &mzi->_the_zone;
error_ret:
	return 0;
}


void* mm_area_alloc(unsigned long size, int ar_type)
{
	long rslt;
	void* p;
	struct dlnode* dln;
	struct shmm_blk* shm;
	struct _mm_area_impl* ar;
	unsigned long alloc_count, free_count;

	err_exit(ar_type < MM_AREA_BEGIN || ar_type >= MM_AREA_COUNT, "mm_area_alloc ar_type error.");

	ar = &__the_mmspace->_area_list[ar_type];

	err_exit(!ar->_free_section, "mm_area_alloc no free section.");

retry_alloc:

	p = (*__mm_area_ops[ar_type]->alloc_func)(ar->_free_section->_allocator, size);
	if(p) goto succ_ret;

	dln = ar->_section_list.head.next;

	while(dln != &ar->_section_list.tail)
	{
		shm = _conv_shmm_from_dln(dln);
		ar->_free_section = _get_section(shm);

		p = (*__mm_area_ops[ar_type]->alloc_func)(ar->_free_section->_allocator, size);
		if(p) goto succ_ret;

		dln = dln->next;
	}

	if(__mm_area_ops[ar_type]->counts_func)
	{
		(*__mm_area_ops[ar_type]->counts_func)(ar->_free_section->_allocator, &alloc_count, &free_count);
		printf("alloc: %lu, free: %lu, ar_type: %d\n", alloc_count, free_count, ar_type);
	}

	rslt = _mm_create_section(__the_mmspace, ar_type);
	err_exit(rslt < 0, "mm_area_alloc create section error.");

	goto retry_alloc;

succ_ret:
	__builtin_prefetch(p);
	return p;
error_ret:
	return 0;

}

void* mm_alloc(unsigned long size)
{
	unsigned long odr;
	int ar_type;

	err_exit(size == 0, "mm_alloc size 0");
	if(!__the_mmspace) goto error_ret;

	odr = log_2(size) + 1;

	if(odr < __the_mmspace->_cfg.mm_cfg[MM_AREA_NUBBLE].max_order)
		ar_type = MM_AREA_NUBBLE;
	else
		ar_type = MM_AREA_PAGE;

	return mm_area_alloc(size, ar_type);

error_ret:
	return 0;
}

long mm_free(void* p)
{
	long rslt;
	struct rbnode* rbn, *hot;
	struct _mm_section_impl* sec;
	struct shmm_blk* shm;

	if(!__the_mmspace) goto error_ret;

	rbn = rb_search(&__the_mmspace->_all_section_tree, p, &hot);
	if(!rbn) goto error_ret;

	shm = _conv_shmm_from_rbn(rbn);
	sec = _get_section(shm);

	rslt = (*__mm_area_ops[sec->_area_type]->free_func)(sec->_allocator, p);
	if(rslt < 0) goto error_ret;

	return rslt;
error_ret:
	return -1;
}

const struct mm_space_config* mm_get_cfg(void)
{
	return &__the_mmspace->_cfg;

}

