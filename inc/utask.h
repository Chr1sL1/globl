#ifndef __utask_h__
#define __utask_h__

typedef void* utask_t;
typedef void (*task_function)(utask_t, void*);

utask_t utsk_create(task_function tfunc);
void utsk_destroy(utask_t tsk);
//

int utsk_run(utask_t tsk, void* udata);
int utsk_yield(utask_t tsk);
int utsk_resume(utask_t tsk);

#endif	// __utask_h__
