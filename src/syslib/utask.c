#include "utask.h"
#include "misc.h"
#include "mmspace.h"
#include <stdio.h>

#define UTASK_MAGIC_NUM	 0x1234567887654321
#define UTASK_ZONE_NAME	"sys_utsk"

enum __utask_impl_state
{
	uts_invalid = 0,

	uts_inited,
	uts_waiting,
	uts_running,
	uts_finished,

	uts_total,
};

struct __reg_values
{
	unsigned long rax;
	unsigned long rbx;
	unsigned long rcx;
	unsigned long rdx;
	unsigned long rsi;
	unsigned long rdi;
	unsigned long rsp;
	unsigned long rbp;
	unsigned long rip;

	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;
	unsigned long r12;
	unsigned long r13;
	unsigned long r14;
	unsigned long r15;
};

struct _utask_impl
{
	void* stk;
	long stk_size;
	task_function tsk_func;

	long _task_state;
	void* _yield_pos;
	void* _next_rsp;
	long _jmp_flag;
	void* _resume_pos;
	void* _resume_running_pos;

	struct __reg_values _saved_regs;
	long _magic_number;
};

extern void asm_run_task(struct _utask_impl* tsk, void* udata);
extern void asm_yield_task(struct _utask_impl* tsk);
extern void asm_resume_task(struct _utask_impl* tsk);

static struct mmcache* __utsk_zone = 0;

static struct _utask_impl* _conv_task(utask_t tsk)
{
	struct _utask_impl* itsk = (struct _utask_impl*)tsk;
	err_exit(!itsk || itsk->_magic_number != UTASK_MAGIC_NUM, "utask: invalid argument");

	return itsk;
error_ret:
	return 0;
}

static inline long _utask_try_restore_zone(void)
{
	if(!__utsk_zone)
	{
		__utsk_zone = mm_search_zone(UTASK_ZONE_NAME);
		if(!__utsk_zone)
		{
			__utsk_zone = mm_cache_create(UTASK_ZONE_NAME, sizeof(struct _utask_impl), 0, 0);
			if(!__utsk_zone) goto error_ret;
		}
		else
		{
			if(__utsk_zone->obj_size != sizeof(struct _utask_impl))
				goto error_ret;
		}
	}

	return 0;
error_ret:
	return -1;
}

utask_t utsk_create(task_function tfunc)
{
	struct _utask_impl* tsk;

	if(!tsk) goto error_ret;
	if(!tfunc) goto error_ret;

	if(_utask_try_restore_zone() < 0)
		goto error_ret;

	tsk = mm_cache_alloc(__utsk_zone);
	if(!tsk) goto error_ret;

	tsk->stk_size = round_down(mm_get_cfg()->mm_cfg[MM_AREA_STACK].stk_frm_size - 16, 16);
	tsk->tsk_func = tfunc;
	tsk->_magic_number = UTASK_MAGIC_NUM;
	tsk->_task_state = uts_inited;

	tsk->stk = mm_area_alloc(tsk->stk_size, MM_AREA_STACK);
	if(!tsk->stk) goto error_ret;

	return tsk;
error_ret:
	if(tsk)
		 mm_cache_free(__utsk_zone, tsk);
	return 0;
}

void utsk_destroy(utask_t tsk)
{
	struct _utask_impl* t = _conv_task(tsk);
	if(!t) goto error_ret;

	if(t->stk)
		mm_free(t->stk);

	mm_cache_free(__utsk_zone, t);

error_ret:
	return;
}

int utsk_run(utask_t tsk, void* udata)
{
	struct _utask_impl* itsk = _conv_task(tsk);
	err_exit(!itsk, "--");
	err_exit(itsk->_task_state != uts_inited, "run task: state error.");

	itsk->_task_state = uts_running; 

	asm_run_task(itsk, udata);

	return 0;
error_ret:
	return -1;
}

int utsk_yield(utask_t tsk)
{
	struct _utask_impl* itsk = _conv_task(tsk);
	err_exit(!itsk, "--");
	err_exit(itsk->_task_state != uts_running, "yield task: state error.");

	itsk->_task_state = uts_waiting; 

	asm_yield_task(itsk);

	return 0;
error_ret:
	return -1;
}

int utsk_resume(utask_t tsk)
{
	struct _utask_impl* itsk = _conv_task(tsk);
	err_exit(!itsk, "--");
	err_exit(itsk->_task_state != uts_waiting, "resume task: state error.");

	itsk->_task_state = uts_running; 
	asm_resume_task(itsk);

	return 0;
error_ret:
	return -1;
}

