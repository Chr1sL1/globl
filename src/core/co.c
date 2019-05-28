#include <stdio.h>

#include "core/co.h"
#include "core/mmspace.h"
#include "core/misc.h"

#define CO_MAGIC_NUM	(0x6677667788558855)
#define CO_ZONE_NAME	"sys_co_zone"

#define SAVE_CONTEXT

struct _reg_context
{
	unsigned long rax;
	unsigned long rbx;
	unsigned long rcx;
	unsigned long rdx;
	unsigned long rdi;
	unsigned long rsi;
	unsigned long rbp;
	unsigned long rsp;
	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;
	unsigned long r12;
	unsigned long r13;
	unsigned long r14;
	unsigned long r15;
} __attribute__((aligned(16)));

struct co_task
{
	unsigned long _magic_num;		// 0x0
	unsigned char _co_resume_flag;	// 0x08
	unsigned char _co_jump_flag;	// 0x09
	unsigned char _co_running;		// 0x0A
	unsigned char _co_reserve[5];	// 0x0B ~ 0x0F

	void* _co_stack_top;			// 0x10
	co_func_t _co_func;				// 0x18
	void* _co_yield_ret_rsp;		// 0x20
	void* _co_func_ret_addr;		// 0x28
	void* _co_final_ret_addr;		// 0x30
	void* _co_stack_bottom;			// 0x38

	unsigned long _rdtsc_yield;		// 0x40
	unsigned long _rdtsc_resume;	// 0x48	

	/*****************************************/

	struct slnode _list_node;
} __cache_aligned__;



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

extern int asm_co_run(struct co_task*, void*);
extern int asm_co_yield(struct co_task*);
extern int asm_co_resume(struct co_task*);

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
	return (struct co_task*)((unsigned long)node - (unsigned long)&(((struct co_task*)(0))->_list_node));
}

struct co_task* co_create(co_func_t func)
{
	int rslt;
	void* co_stack;
	int stack_size;
	struct co_task* co;

	err_exit(!func, "co_create: invalid func.");

	co_stack = mm_area_alloc(0, MM_AREA_STACK);
	err_exit(!co_stack, "co_create: alloc stack failed.");

	stack_size = round_down(mm_get_cfg()->mm_cfg[MM_AREA_STACK].stk_frm_size - 16, 16);
	co = (struct co_task*)(co_stack + stack_size - sizeof(struct co_task));
	co = move_ptr_align64(co, 0) - cache_line_size;

	co->_magic_num = CO_MAGIC_NUM;
	co->_co_func = func;
	co->_co_stack_top = co - sizeof(struct _reg_context);
	co->_co_stack_bottom = co_stack;
	co->_co_resume_flag = 0;

	printf("co_create: %p, size: %u\n", co, sizeof(struct co_task));
	return co;
error_ret:
	return 0;
}

void co_destroy(struct co_task* co)
{
	struct co_task* coi = __conv_co(co);
	err_exit(!coi, "co_destroy: invalid param");

	if(coi->_co_stack_bottom)
		mm_free(coi->_co_stack_bottom);

error_ret:
	return;
}

int co_run(struct co_task* co, void* co_func_param)
{
	printf("co_run: %p\n", co);
	struct co_task* coi = __conv_co(co);
	err_exit(!coi, "co_run: invalid param");

	coi->_co_running = 1;

	asm_co_run(coi, co_func_param);

	return 0;
error_ret:
	return -1;
}

int co_yield(struct co_task* co)
{
	struct co_task* coi = __conv_co(co);
	err_exit(!coi, "co_yield: invalid param");
	err_exit(!coi->_co_running, "co_yield: co is not running.");

	return asm_co_yield(coi);
error_ret:
	printf("co: %p\n", co);
	return -1;
}

int co_resume(struct co_task* co)
{
	struct co_task* coi = __conv_co(co);
	err_exit(!coi, "co_resume: invalid param");
	err_exit(!coi->_co_running, "co_resume: co is not running.");

	return asm_co_resume(coi);
error_ret:
	return -1;
}


unsigned long co_profile_yield(struct co_task* co)
{
	return co->_rdtsc_yield;
}

unsigned long co_profile_resume(struct co_task* co)
{
	return co->_rdtsc_resume;
}

int init_co_holder(struct co_holder* ch)
{
	sl_init(&ch->_co_list);
	return 0;
error_ret:
	return -1;
}

int push_co(struct co_holder* ch, struct co_task* co)
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

int free_all_co(struct co_holder* ch)
{

error_ret:
	return -1;
}


