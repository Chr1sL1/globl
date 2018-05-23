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
	unsigned long _magic_num;
	co_func_t _co_func;
	void* _co_stack_top;
	void* _co_stack_bottom;
	void* _co_func_ret_addr;
	void* _co_yield_addr;
	void* _co_yield_ret_rsp;
	void* _co_run_rsp;
	void* _co_run_rbp;
	void* _co_run_rbx;
	unsigned long _resume_flag;
} __attribute__((aligned(16)));

extern asm_co_run(struct co_impl*, void*, co_func_t func);
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

	asm volatile ("movq %%rbx, %0\n" :"=r" (coi->_co_run_rbx));
	asm volatile ("movq %%rbp, %0\n" :"=r" (coi->_co_run_rbp));
	asm volatile ("movq %%rsp, %0\n" :"=r" (coi->_co_run_rsp));

	asm volatile ("movq %0, %%rsp\n" : :"r" (coi->_co_stack_top - 16));
	__co_run(coi, co_func_param);

	asm volatile ("movq %0, %%rsp\n" : :"r" (coi->_co_run_rsp));
	asm volatile ("movq %0, %%rbp\n" : :"r" (coi->_co_run_rbp));
	asm volatile ("movq %0, %%rbx\n" : :"r" (coi->_co_run_rbx));

//	printf("after co_yield rip: [%p]\n", coi->_co_func_ret_addr);
//	printf("after co_run rsp: [%p]\n", coi->_co_run_rsp);
//	printf("after co_run rbp: [%p]\n", coi->_co_run_rbp);

	return 0;
error_ret:
	return -1;
}

int co_yield(co_t co)
{
	unsigned long current_rsp;
	printf("co_yield: %p\n", co);
	struct _co_impl* coi = __conv_co(co);
	err_exit(!coi, "co_yield: invalid param");

	asm volatile ("movq %%rax, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->rax));
	asm volatile ("movq %%rbx, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->rbx));
	asm volatile ("movq %%rcx, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->rcx));
	asm volatile ("movq %%rdx, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->rdx));
	asm volatile ("movq %%rsi, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->rsi));
	asm volatile ("movq %%rdi, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->rdi));
	asm volatile ("movq %%rsp, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->rsp));
	asm volatile ("movq %%rbp, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->rbp));
	asm volatile ("movq %%r8, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->r8));
	asm volatile ("movq %%r9, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->r9));
	asm volatile ("movq %%r10, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->r10));
	asm volatile ("movq %%r11, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->r11));
	asm volatile ("movq %%r12, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->r12));
	asm volatile ("movq %%r13, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->r13));
	asm volatile ("movq %%r14, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->r14));
	asm volatile ("movq %%r15, %0\n" :"=r" (((struct _reg_context*)(coi->_co_stack_top))->r15));
	asm volatile ("leaq 0x0(%%rip), %0\n" :"=r" (coi->_co_yield_addr));

	if(!coi->_resume_flag)
	{
		asm volatile ("movq %0, %%rsp\n" : :"r" (coi->_co_yield_ret_rsp));
		asm volatile ("jmpq %0\n" : :"r" (coi->_co_func_ret_addr));
	}
	else
	{
		coi->_resume_flag = 0;
	}

	return 0;
error_ret:
	printf("co: %p\n", co);
	return -1;
}

int co_resume(co_t co)
{
	printf("co_resume: %p\n", co);
	struct _co_impl* coi = __conv_co(co);
	err_exit(!coi, "co_resume: invalid param");


	coi->_resume_flag = 1;

	asm volatile ("movq %0, %%rax\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->rax));
	asm volatile ("movq %0, %%rbx\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->rbx));
	asm volatile ("movq %0, %%rcx\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->rcx));
	asm volatile ("movq %0, %%rdx\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->rdx));
	asm volatile ("movq %0, %%rsi\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->rsi));
	asm volatile ("movq %0, %%rdi\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->rdi));
	asm volatile ("movq %0, %%rsp\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->rsp));
	asm volatile ("movq %0, %%rbp\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->rbp));
	asm volatile ("movq %0, %%r8\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->r8));
	asm volatile ("movq %0, %%r9\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->r9));
	asm volatile ("movq %0, %%r10\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->r10));
	asm volatile ("movq %0, %%r11\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->r11));
	asm volatile ("movq %0, %%r12\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->r12));
	asm volatile ("movq %0, %%r13\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->r13));
	asm volatile ("movq %0, %%r14\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->r14));
	asm volatile ("movq %0, %%r15\n" : :"r" (((struct _reg_context*)(coi->_co_stack_top))->r15));

	asm volatile ("jmpq %0\n" : :"r" (coi->_co_yield_addr));

	return 0;
error_ret:
	return -1;
}
