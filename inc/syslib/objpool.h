#ifndef __objpool_h__
#define __objpool_h__

//struct objpool
//{
//	void* addr_begin;
//	void* addr_end;
//	unsigned long obj_size;
//};

struct objpool;

struct objpool* objpool_create(void* addr, unsigned int size, unsigned int obj_size);
struct objpool* objpool_load(void* addr);

int objpool_destroy(struct objpool* mm);

void* objpool_alloc(struct objpool* mm);
int objpool_free(struct objpool* mm, void* p);

int objpool_check(struct objpool* mm);

#endif

