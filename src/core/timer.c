#include "common_types.h"
#include "core/timer.h"
#include "core/dlist.h"
#include "core/misc.h"
#include "core/vm_space.h"
#include <stdio.h>

#define TIMER_WHEEL_NAME	"core_timer_wheel"

#define TIMER_NODE_TYPE_MAGIC	(0x12341234)

#define TV_SET0_BITS	(8)
#define TV_SET_BITS		(6)

#define TV_SET0_SIZE	(1UL << TV_SET0_BITS)
#define TV_SET_SIZE		(1UL << TV_SET_BITS)

#define TV_SET0_MASK	((1UL << TV_SET0_BITS) - 1)
#define TV_SETN_MASK	((1UL << TV_SET_BITS) - 1)
#define TV_SET_SHIFT_BITS(N)	(TV_SET0_BITS + (N)* TV_SET_BITS)

#define TV_SET_MASK(N)	((1UL << (TV_SET0_BITS + (N + 1) * TV_SET_BITS)) - 1)
#define TV_SET_MASK_IDX(N)	(((1UL << TV_SET_BITS) - 1) << (TV_SET0_BITS + (N) * TV_SET_BITS))


#define TV_SET_CNT		(4)

struct timer_node
{
	i32 _magic;
	i32 _run_once;

	u64 _run_tick;
	u64 _delay_tick;
	timer_func_t _callback_func;
	void* _func_param;

	struct dlnode _lln;
};

struct tv_set
{
	struct dlist _list[TV_SET_SIZE];
};

struct timer_wheel
{
	u64 _current_tick;
	struct dlist _tv_set0[TV_SET0_SIZE];
	struct tv_set _tv_set[TV_SET_CNT];
};

static struct timer_wheel* __the_timer_wheel = NULL;

static i32 __init_timer(struct timer_wheel* tw)
{
	err_exit_silent(!tw);

	tw->_current_tick = 0;

	for (i32 i = 0; i < TV_SET0_SIZE; ++i)
	{
		lst_new(&tw->_tv_set0[i]);
	}

	for (i32 i = 0; i < TV_SET_CNT; ++i)
	{
		for (i32 j = 0; j < TV_SET_SIZE; ++j)
		{
			lst_new(&tw->_tv_set[i]._list[j]);
		}
	}

	return 0;
error_ret:
	return -1;
}


i32 timer_module_load(void)
{
	__the_timer_wheel = vm_find_chunk(TIMER_WHEEL_NAME);
	if(!__the_timer_wheel)
		__the_timer_wheel = vm_new_chunk(TIMER_WHEEL_NAME, sizeof(struct timer_wheel));

	return __init_timer(__the_timer_wheel);
error_ret:
	return -1;
}

void timer_module_unload(void)
{

}



static inline void _add_timer(struct timer_node* tn)
{
	i32 cur_idx;
	i32 diff_tick = tn->_run_tick - __the_timer_wheel->_current_tick;

//	pri32f("diff tick: %d\n", diff_tick);

	if (diff_tick < TV_SET0_SIZE)
	{
		lst_push_back(&__the_timer_wheel->_tv_set0[diff_tick & TV_SET0_MASK], &tn->_lln);
	}
	else
	{
		u32 idx;

		for(i32 i = TV_SET_CNT - 1; i >= 0; --i)
		{
			idx = ((diff_tick & TV_SET_MASK_IDX(i)) >> TV_SET_SHIFT_BITS(i));
			if(idx > 0)
			{
				cur_idx = ((__the_timer_wheel->_current_tick & TV_SET_MASK_IDX(i)) >> TV_SET_SHIFT_BITS(i));

				idx = ((idx + cur_idx) & TV_SETN_MASK);
//				pri32f("add to [%d:%d]\n", i, idx);
				lst_push_back(&__the_timer_wheel->_tv_set[i]._list[idx], &tn->_lln);
				break;
			}
		}
	}
}


struct timer_node* timer_schedule(u32 delay_tick, timer_func_t callback_func, i32 rune_once, void* param)
{
	struct timer_node* _node = NULL;

	err_exit(!callback_func, "add_timer: illegal param.");

	_node = (struct timer_node*)vm_common_alloc(sizeof(struct timer_node));
	err_exit(!_node, "add_timer: alloc timer node failed.");

	_node->_magic = TIMER_NODE_TYPE_MAGIC;
	_node->_delay_tick = delay_tick;
	_node->_run_tick = __the_timer_wheel->_current_tick + delay_tick;
	_node->_callback_func = callback_func;
	_node->_func_param = param;
	_node->_run_once = rune_once;
	lst_clr(&_node->_lln);

	_add_timer(_node);

	return _node;
error_ret:
	return 0;
}

static inline void _del_timer(struct timer_node* tn)
{
	lst_remove_node(&tn->_lln);
	vm_common_free(tn);
}

void del_timer(struct timer_node* timer_node)
{
	err_exit(!timer_node, "del_timer null node.");
	err_exit(timer_node->_magic != TIMER_NODE_TYPE_MAGIC, "del_timer type error.");

	_del_timer(timer_node);

	return;
error_ret:
	return;
}

static inline  void __casade0(void)
{
	i32 list_idx = ((__the_timer_wheel->_current_tick & TV_SET_MASK_IDX(0)) >> TV_SET0_BITS);

	struct dlnode* node = lst_first(&__the_timer_wheel->_tv_set[0]._list[list_idx]);
	while (node != lst_last(&__the_timer_wheel->_tv_set[0]._list[list_idx]))
	{
		struct dlnode* cur_node = node;
		node = node->next;

		struct timer_node* tn = (struct timer_node*)((u64)cur_node - (u64)&((struct timer_node*)(0))->_lln);
		lst_remove_node(cur_node);

		_add_timer(tn);
	}
}

static void __casade(i32 i)
{
	i32 idx, cnt = 0;
	i32 list_idx = ((__the_timer_wheel->_current_tick & TV_SET_MASK_IDX(i + 1)) >> TV_SET_SHIFT_BITS(i + 1));

	struct dlnode* node = lst_first(&__the_timer_wheel->_tv_set[i + 1]._list[list_idx]);
	while (node != lst_last(&__the_timer_wheel->_tv_set[i + 1]._list[list_idx]))
	{
		struct dlnode* cur_node = node;
		node = node->next;

		struct timer_node* tn = (struct timer_node*)((u64)cur_node - (u64)&((struct timer_node*)(0))->_lln);
		lst_remove_node(cur_node);

		_add_timer(tn);

		++cnt;
	}
}

void on_tick(void)
{

	if (__the_timer_wheel->_current_tick > 0 && (__the_timer_wheel->_current_tick & TV_SET0_MASK) == 0)
		__casade0();

	if(((__the_timer_wheel->_current_tick & TV_SET_MASK(1)) != 0) && (__the_timer_wheel->_current_tick & TV_SET_MASK(0)) == 0)
		__casade(0);

	if(((__the_timer_wheel->_current_tick & TV_SET_MASK(2)) != 0) && (__the_timer_wheel->_current_tick & TV_SET_MASK(1)) == 0)
		__casade(1);

	if(((__the_timer_wheel->_current_tick & TV_SET_MASK(3)) != 0) && (__the_timer_wheel->_current_tick & TV_SET_MASK(2)) == 0)
		__casade(2);

	struct dlnode* node = lst_first(&__the_timer_wheel->_tv_set0[__the_timer_wheel->_current_tick & TV_SET0_MASK]);
	while (node != lst_last(&__the_timer_wheel->_tv_set0[__the_timer_wheel->_current_tick & TV_SET0_MASK]))
	{
		struct dlnode* cur_node = node;
		node = node->next;

		struct timer_node* tn = (struct timer_node*)((u64)cur_node - (u64)&((struct timer_node*)(0))->_lln);

		(*tn->_callback_func)(tn, tn->_func_param);

		if(tn->_run_once)
			_del_timer(tn);
		else
		{
			lst_remove_node(cur_node);
			tn->_run_tick = __the_timer_wheel->_current_tick + tn->_delay_tick;
			_add_timer(tn);
		}
	}

	++__the_timer_wheel->_current_tick;
}

u64 dbg_current_tick(void)
{
	return __the_timer_wheel->_current_tick;
}
