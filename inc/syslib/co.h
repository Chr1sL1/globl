#ifndef __co_h__
#define __co_h__

#include "slist.h"

struct co_task;

typedef void (*co_func_t)(struct co_task*, void*);

struct co_task* co_create(co_func_t func);
void co_destroy(struct co_task* co);

int co_run(struct co_task* co, void* co_func_param);
int co_yield(struct co_task* co);
int co_resume(struct co_task* co);

struct co_holder
{
	struct slist _co_list;
};

int init_co_holder(struct co_holder* ch);
int push_co(struct co_holder* ch, struct co_task* co);
struct co_task* pop_co(struct co_holder* ch);
int free_all_co(struct co_holder* ch);

#endif

