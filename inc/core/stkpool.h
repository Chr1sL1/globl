#ifndef __stk_pool_h__
#define __stk_pool_h__


struct mm_config;

struct stkpool
{
	void* addr_begin;
	void* addr_end;
};

struct stkpool* stkp_create(void* addr, struct mm_config* cfg);
struct stkpool* stkp_load(void* addr);
void stkp_destroy(struct stkpool* stkp);

void* stkp_alloc(struct stkpool* stkp);
long stkp_free(struct stkpool* stkp, void* p);

#endif

