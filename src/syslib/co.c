#include "co.h"
#include "mmspace.h"
#include "misc.h"
#include <stdio.h>

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

struct _co_impl
{
	unsigned long _magic_num;		// 0x0
	co_func_t _co_func;				// 0x08

	void* _co_stack_top;			// 0x10
	void* _co_stack_bottom;			// 0x18

	void* _co_yield_ret_rsp;		// 0x20
	void* _co_func_ret_addr;		// 0x28

	unsigned long _co_resume_flag;	// 0x30
	unsigned long _co_jump_flag;	// 0x38

	unsigned long _co_running;		// 0x40
	void* _co_final_ret_addr;		// 0x48


} __attribute__((aligned(16)));

extern asm_co_run(struct co_impl*, void*);
extern asm_co_yield(struct co_impl*);
extern asm_co_resume(struct co_impl*);

static void __co_run(struct _co_impl* coi, void* param) __attribute__((noinline));

static inline struct _co_impl* __conv_co(co_t co)
{
	struct _co_impl* coi = (struct _co_impl*)co;
	err_exit(!coi || coi->_magic_num != CO_MAGIC_NUM, "invalid co.");

	return coi;
error_ret:
	return 0;
}

co_t co_create(co_func_t func)
{
	int rslt;
	void* co_stack;
	int stack_size;
	struct _co_impl* co;

	err_exit(!func, "co_create: invalid func.");

	co_stack = mm_area_alloc(0, MM_AREA_STACK);
	err_exit(!co_stack, "co_create: alloc stack failed.");

	stack_size = round_down(mm_get_cfg()->mm_cfg[MM_AREA_STACK].stk_frm_size - 16, 16);
	co = (struct _co_impl*)(co_stack + stack_size - sizeof(struct _co_impl));

	co->_magic_num = CO_MAGIC_NUM;
	co->_co_func = func;
	co->_co_stack_top = co - sizeof(struct _reg_context);
	co->_co_stack_bottom = co_stack;
	co->_co_resume_flag = 0;

	printf("co_create: %p\n", co);
	return co;
error_ret:
	return 0;
}

void co_destroy(co_t co)
{
	struct _co_impl* coi = __conv_co(co);
	err_exit(!coi, "co_destroy: invalid param");

	if(coi->_co_stack_bottom)
		mm_free(coi->_co_stack_bottom);

error_ret:
	return;
}

static void __co_run(struct _co_impl* coi, void* param)
{
	printf("__co_run: %p\n", coi);
	coi->_co_func_ret_addr = __builtin_return_address(0);

	printf("__co_run ret: [%p]\n", (coi->_co_func_ret_addr));

	asm volatile ("movq %%rsp, %0\n" :"=r" (coi->_co_yield_ret_rsp));
	asm volatile ("movq %0, %%rdi\n" : :"r" (coi));
	asm volatile ("movq %0, %%rsi\n" : :"r" (param));

	asm volatile ("call *(%0)\n" : :"r" (&coi->_co_func));
}

int co_run(co_t co, void* co_func_param)
{
	printf("co_run: %p\n", co);
	struct _co_impl* coi = __conv_co(co);
	err_exit(!coi, "co_run: invalid param");

	coi->_co_running = 1;

	asm_co_run(coi, co_func_param);

	return 0;
error_ret:
	return -1;
}

int co_yield(co_t co)
{
	unsigned long current_rsp;
	struct _co_impl* coi = __conv_co(co);
	err_exit(!coi, "co_yield: invalid param");
	err_exit(!coi->_co_running, "co_yield: co is not running.");

	return asm_co_yield(coi);
error_ret:
	printf("co: %p\n", co);
	return -1;
}

int co_resume(co_t co)
{
	struct _co_impl* coi = __conv_co(co);
	err_exit(!coi, "co_resume: invalid param");
	err_exit(!coi->_co_running, "co_resume: co is not running.");

	return asm_co_resume(coi);
error_ret:
	return -1;
}
