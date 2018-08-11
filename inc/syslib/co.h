#ifndef __co_h__
#define __co_h__

#include "slist.h"

typedef void* co_t;
typedef void (*co_func_t)(co_t, void*);

co_t co_create(co_func_t func);
void co_destroy(co_t co);

int co_run(co_t co, void* co_func_param);
int co_yield(co_t co);
int co_resume(co_t co);

struct co_holder
{
	struct slist _co_list;
};

int init_co_holder(struct co_holder* ch);
int push_co(struct co_holder* ch, co_t co);
co_t pop_co(struct co_holder* ch);
int free_all_co(struct co_holder* ch);

#endif

