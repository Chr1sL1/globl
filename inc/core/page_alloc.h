#ifndef __pgpool_h__
#define __pgpool_h__


struct mm_config;

struct pgpool
{
	void* addr_begin;
	void* addr_end;
};

struct pgpool* pgp_create(void* addr, struct mm_config* cfg);
struct pgpool* pgp_load(void* addr);
void pgp_destroy(struct pgpool* up);

void* pgp_alloc(struct pgpool* up, u64 size);
i32 pgp_free(struct pgpool* up, void* p);


i32 pgp_check(struct pgpool* up);

#endif

