#ifndef __mmpool_h__
#define __mmpool_h__


struct mm_config;

struct mmpool
{
	void* addr_begin;
	void* addr_end;
};

struct mmpool* mmp_create(void* addr, struct mm_config* cfg);
struct mmpool* mmp_load(void* addr);
void mmp_destroy(struct mmpool* mmp);

void* mmp_alloc(struct mmpool* mmp, u64 size);
i32 mmp_free(struct mmpool* mmp, void* p);

i32 mmp_check(struct mmpool* mmp);
i32 mmp_freelist_profile(struct mmpool* mmp);


#endif

