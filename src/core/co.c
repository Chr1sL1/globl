#include <stdio.h>

#include "common_types.h"
#include "core/co.h"
#include "core/misc.h"
#include "core/asm.h"
#include "core/vm_stack_alloc.h"

#define CO_MAGIC_NUM	(0x6677667788558855)
#define CO_ZONE_NAME	"sys_co_zone"
#define CO_ALLOCATOR_NAME	"core_co_allocator"

#define SAVE_CONTEXT

struct _reg_context
{
	u64 rax;
	u64 rbx;
	u64 rcx;
	u64 rdx;
	u64 rdi;
	u64 rsi;
	u64 rbp;
	u64 rsp;
	u64 r8;
	u64 r9;
	u64 r10;
	u64 r11;
	u64 r12;
	u64 r13;
	u64 r14;
	u64 r15;
} __attribute__((aligned(16)));

struct co_task
{
	u64 _magic_num;		// 0x0
	u8 _co_resume_flag;	// 0x08
	u8 _co_jump_flag;	// 0x09
	u8 _co_running;		// 0x0A
	u8 _co_reserve[5];	// 0x0B ~ 0x0F

	void* _co_stack_top;			// 0x10
	co_func_t _co_func;				// 0x18
	void* _co_yield_ret_rsp;		// 0x20
	void* _co_func_ret_addr;		// 0x28
	void* _co_final_ret_addr;		// 0x30
	void* _co_stack_bottom;			// 0x38

	u64 _rdtsc_yield;		// 0x40
	u64 _rdtsc_resume;	// 0x48	

	/*****************************************/

	struct slnode _list_node;
} __cache_aligned__;

static inline void __clear_co_task(struct co_task* co)
{
	co->_magic_num = 0;		// 0x0
	co->_co_resume_flag = 0;	// 0x08
	co->_co_jump_flag = 0;	// 0x09
	co->_co_running = 0;		// 0x0A

	co->_co_stack_top = 0;			// 0x10
	co->_co_func = 0;				// 0x18
	co->_co_yield_ret_rsp = 0;		// 0x20
	co->_co_func_ret_addr = 0;		// 0x28
	co->_co_final_ret_addr = 0;		// 0x30
	co->_co_stack_bottom = 0;			// 0x38
}

struct vm_stack_allocator* __co_stack_allocator = NULL;

i32 co_module_load(u32 cocurrent_stack_cnt, u32 stack_frame_size)
{
	__co_stack_allocator = stack_allocator_load(CO_ALLOCATOR_NAME);
	if(!__co_stack_allocator)
		__co_stack_allocator = stack_allocator_create(CO_ALLOCATOR_NAME, cocurrent_stack_cnt, stack_frame_size);

	err_exit(!__co_stack_allocator, "co module load failed.");
	return 0;
error_ret:
	return -1;
}

void co_module_unload(void)
{
	if(__co_stack_allocator)
		stack_allocator_destroy(__co_stack_allocator);

	return;
}

/*************************************************
 *			memory layout:
 *			----------------- <---- high mem
 *			|               |
 *			|   co_task     |
 *			|               |
 *			-----------------
 *			|               |
 *			| _reg_context  |
 *			|               |
 *			-----------------
 *			|               |
 *			|               |
 *			|               |
 *			|   co stack    |
 *			|               |
 *			|               |
 *			|               |
 *			----------------- <---- low mem
 *
 * ***********************************************/

extern i32 asm_co_run(struct co_task*, void*);
extern i32 asm_co_yield(struct co_task*);
extern i32 asm_co_resume(struct co_task*);

static inline struct co_task* __conv_co(struct co_task* co)
{
	struct co_task* coi = (struct co_task*)co;
	err_exit(!coi || coi->_magic_num != CO_MAGIC_NUM, "invalid co.");

	return coi;
error_ret:
	return 0;
}

static inline struct co_task* __conv_co_from_slnode(struct slnode* node)
{
	return (struct co_task*)((u64)node - (u64)&(((struct co_task*)(0))->_list_node));
}

struct co_task* co_create(co_func_t func)
{
	i32 rslt;
	void* co_stack;
	u64 stack_size;
	struct co_task* co;

	err_exit(!func, "co_create: invalid func.");

	co_stack = stack_allocator_alloc(__co_stack_allocator, &stack_size);
	err_exit(!co_stack, "co_create: alloc stack failed.");

	stack_size = round_down(stack_size, 16);
	co = (struct co_task*)(co_stack + stack_size - sizeof(struct co_task));
	co = (struct co_task*)(round_down((u64)co, cache_line_size));

	co->_magic_num = CO_MAGIC_NUM;
	co->_co_func = func;
	co->_co_stack_top = co - sizeof(struct _reg_context);
	co->_co_stack_bottom = co_stack;
	co->_co_resume_flag = 0;

//	printf("co_create_succ: %p, size: %lu\n", co, sizeof(struct co_task));
	return co;
error_ret:
	return 0;
}

void co_destroy(struct co_task* co)
{
	void* co_stack;
	struct co_task* coi = __conv_co(co);
	err_exit(!coi, "co_destroy: invalid param");

	co_stack = coi->_co_stack_bottom;
	__clear_co_task(coi);

	if(co_stack)
	{
//		printf("co_destroy_succ\n");
		stack_allocator_free(__co_stack_allocator, co_stack);
	}

error_ret:
	return;
}

i32 co_run(struct co_task* co, void* co_func_param)
{
//	printf("co_run: %p\n", co);
	struct co_task* coi = __conv_co(co);
	err_exit(!coi, "co_run: invalid param");

	coi->_co_running = 1;

	asm_co_run(coi, co_func_param);

	return 0;
error_ret:
	return -1;
}

i32 co_yield(struct co_task* co)
{
	struct co_task* coi = __conv_co(co);
	err_exit(!coi, "co_yield: invalid param");
	err_exit(!coi->_co_running, "co_yield: co is not running.");

	return asm_co_yield(coi);
error_ret:
//	printf("co: %p\n", co);
	return -1;
}

i32 co_resume(struct co_task* co)
{
	struct co_task* coi = __conv_co(co);
	err_exit(!coi, "co_resume: invalid param");
	err_exit(!coi->_co_running, "co_resume: co is not running.");

	return asm_co_resume(coi);
error_ret:
	return -1;
}


u64 co_profile_yield(struct co_task* co)
{
	return co->_rdtsc_yield;
}

u64 co_profile_resume(struct co_task* co)
{
	return co->_rdtsc_resume;
}

i32 init_co_holder(struct co_holder* ch)
{
	sl_init(&ch->_co_list);
	return 0;
error_ret:
	return -1;
}

i32 push_co(struct co_holder* ch, struct co_task* co)
{
	struct co_task* coi = __conv_co(co);
	err_exit(!coi, "co_yield: invalid param");

	coi->_list_node._next = 0;

	sl_push_head(&ch->_co_list, &coi->_list_node);

	return 0;
error_ret:
	return -1;
}

struct co_task* pop_co(struct co_holder* ch)
{

error_ret:
	return 0;
}

i32 free_all_co(struct co_holder* ch)
{

error_ret:
	return -1;
}



