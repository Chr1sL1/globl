#include "core/objpool.h"
#include "core/dlist.h"
#include "core/misc.h"

#include <stdlib.h>

#define OBJPOOL_LABEL (0xeded00122100dedeUL)
#define SLB_OBJ_LABEL (0xabcd7890)

#define UFB_USED (1)

struct objpool
{
	void* addr_begin;
	void* addr_end;
	unsigned long obj_size;
};

struct _obj_header
{
	unsigned int _obj_label;
	unsigned int _obj_flag;
};

struct _obj_node
{
	struct dlnode _fln;
	struct _obj_header* _obj;
};

struct _objpool_impl
{
	unsigned long _chunk_label;
	struct objpool _the_pool;
	struct dlist _free_list;

	void* _chunk_addr;

	unsigned int _actual_obj_size;
	unsigned int _obj_count;

	objpool_ctor _ctor;
	objpool_dtor _dtor;

	struct _obj_node _node_pool[0];
};

static int _objpool_destroy(struct _objpool_impl* umi);


static inline void* _get_payload(struct _obj_header* uoh)
{
	return (void*)(uoh + 1);
}

static inline void _set_flag(struct _obj_header* uoh, unsigned int flag)
{
	uoh->_obj_flag |= flag;
}

static inline void _clear_flag(struct _obj_header* uoh, unsigned int flag)
{
	uoh->_obj_flag &= ~flag;
}

static inline struct _objpool_impl* _conv_impl(struct objpool* mm)
{
	return (struct _objpool_impl*)((void*)mm - (unsigned long)(&((struct _objpool_impl*)(0))->_the_pool));
}

static inline struct _obj_node* _conv_fln(struct dlnode* fln)
{
	return (struct _obj_node*)((void*)fln - (unsigned long)(&((struct _obj_node*)(0))->_fln));
}

static inline struct _obj_node* _get_node_from_obj(struct _objpool_impl* umi, void* uoh)
{
	unsigned long idx;

	if(uoh < umi->_chunk_addr || uoh >= umi->_the_pool.addr_end)
		goto error_ret;

	idx = (uoh - umi->_chunk_addr) / umi->_actual_obj_size;
	if(idx >= umi->_obj_count)
		goto error_ret;

	return &umi->_node_pool[idx];
error_ret:
	return 0;
}

static inline struct _obj_node* _fetch_fln(struct _objpool_impl* umi)
{
	struct dlnode* dln = lst_pop_front(&umi->_free_list);
	if(!dln) goto error_ret;

	lst_clr(dln);

	return _conv_fln(dln);
error_ret:
	return 0;
}

static inline void _return_fln(struct _objpool_impl* umi, struct _obj_node* un)
{
	lst_clr(&un->_fln);
	lst_push_front(&umi->_free_list, &un->_fln);
}

struct objpool* objpool_create(void* addr, unsigned int size, unsigned int obj_size, objpool_ctor ctor, objpool_dtor dtor)
{
	struct _objpool_impl* umi;
	void* cur_pos = addr;
	unsigned long chunk_size;

	if(!addr || ((unsigned long)addr & 7) != 0) goto error_ret;
	if(size <= sizeof(struct _objpool_impl))
		goto error_ret;

	umi = (struct _objpool_impl*)addr;
	if(umi->_chunk_label == OBJPOOL_LABEL)
		goto error_ret;

	cur_pos = move_ptr_align8(cur_pos, sizeof(struct _objpool_impl));

	umi->_the_pool.addr_begin = addr;
	umi->_the_pool.addr_end = addr + size;

	umi->_the_pool.obj_size = obj_size;
	umi->_actual_obj_size = align8(obj_size + sizeof(struct _obj_header));

	umi->_obj_count = (umi->_the_pool.addr_end - cur_pos) / (sizeof(struct _obj_node) + umi->_actual_obj_size);

//	umi->_node_pool = (struct _obj_node*)cur_pos;
	cur_pos = move_ptr_align8(cur_pos, sizeof(struct _obj_node) * umi->_obj_count);

	umi->_chunk_addr = cur_pos;

	umi->_ctor = ctor;
	umi->_dtor = dtor;

	lst_new(&umi->_free_list);

	for(unsigned long i = 0; i < umi->_obj_count; ++i)
	{
		struct _obj_header* uoh = (struct _obj_header*)(umi->_chunk_addr + i * umi->_actual_obj_size);
		uoh->_obj_label = SLB_OBJ_LABEL;
		uoh->_obj_flag = 0;

		umi->_node_pool[i]._obj = uoh;

		lst_clr(&umi->_node_pool[i]._fln);
		lst_push_back(&umi->_free_list, &umi->_node_pool[i]._fln);
	}

	return &umi->_the_pool;
error_ret:
	if(umi)
		_objpool_destroy(umi);
	return 0;
}

struct objpool* objpool_load(void* addr, objpool_ctor ctor, objpool_dtor dtor)
{
	struct _objpool_impl* umi;
	void* cur_pos = addr;

	if(!addr || ((unsigned long)addr & 63) != 0) goto error_ret;

	umi = (struct _objpool_impl*)(cur_pos);
	cur_pos = move_ptr_align64(addr, sizeof(struct _objpool_impl));

	if(umi->_chunk_label != OBJPOOL_LABEL)
		goto error_ret;

	if(umi->_the_pool.addr_begin != addr || umi->_the_pool.addr_begin >= umi->_the_pool.addr_end)
		goto error_ret;

	return &umi->_the_pool;
error_ret:
	return 0;
}

inline unsigned long objpool_mem_usage(unsigned long obj_count, unsigned long obj_size)
{
	return align8(sizeof(struct _objpool_impl)) + align8(sizeof(struct _obj_node) * obj_count)
		+ align8(obj_size + sizeof(struct _obj_header)) * obj_count; 
}

static int _objpool_destroy(struct _objpool_impl* umi)
{
	if(!umi) goto error_ret;

	if(umi->_chunk_label != OBJPOOL_LABEL)
		goto error_ret;

	umi->_chunk_label = 0;
	umi->_the_pool.addr_begin = 0;
	umi->_the_pool.addr_end = 0;

	return 0;
error_ret:
	return -1;
}

int objpool_destroy(struct objpool* mm)
{
	struct _objpool_impl* umi = _conv_impl(mm);
	if(!umi) goto error_ret;

	return _objpool_destroy(umi);
error_ret:
	return -1;
}

void* objpool_alloc(struct objpool* mm)
{
	struct _obj_node* un;
	struct _obj_header* uoh;
	struct _objpool_impl* umi = _conv_impl(mm);
	void* obj_ptr;
	if(!umi) goto error_ret;

	un = _fetch_fln(umi);
	if(!un) goto error_ret;

	uoh = un->_obj;
	if(!uoh || uoh->_obj_label != SLB_OBJ_LABEL || (uoh->_obj_flag & UFB_USED) != 0)
		goto error_ret;

	_set_flag(uoh, UFB_USED);

	obj_ptr = _get_payload(uoh);

	if(umi->_ctor && obj_ptr)
		(*umi->_ctor)(obj_ptr);

	return obj_ptr;
error_ret:
	return 0;
}

int objpool_free(struct objpool* mm, void* p)
{
	struct _obj_node* un;
	struct _obj_header* uoh;
	struct _objpool_impl* umi = _conv_impl(mm);
	if(!umi) goto error_ret;

	uoh = (struct _obj_header*)(p - sizeof(struct _obj_header));

	if(uoh->_obj_label != SLB_OBJ_LABEL || (uoh->_obj_flag & UFB_USED) == 0)
		goto error_ret;

	un = _get_node_from_obj(umi, uoh);
	if(!un || un->_obj != uoh)
		goto error_ret;

	if(umi->_dtor && p)
		(*umi->_dtor)(p);

	_clear_flag(uoh, UFB_USED);

	_return_fln(umi, un);

	return 0;
error_ret:
	return -1;
}

int objpool_check(struct objpool* mm)
{
	struct _objpool_impl* umi = _conv_impl(mm);
	if(!umi) goto error_ret;

	for(long i = 0; i < umi->_obj_count; ++i)
	{
		struct _obj_node* node = &umi->_node_pool[i];

		if(node->_obj->_obj_label != SLB_OBJ_LABEL)
			goto error_ret;

		if((node->_obj->_obj_flag & UFB_USED) != 0)
			goto error_ret;
	}

	return 0;
error_ret:
	return -1;
}

