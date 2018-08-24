#ifndef __objpool_h__
#define __objpool_h__

struct objpool;

typedef void (*objpool_ctor)(void* obj_ptr);
typedef void (*objpool_dtor)(void* obj_ptr);

struct objpool* objpool_create(void* addr, unsigned int size, unsigned int obj_size, objpool_ctor ctor, objpool_dtor dtor);
struct objpool* objpool_load(void* addr, objpool_ctor ctor, objpool_dtor dtor);
unsigned long objpool_mem_usage(unsigned long obj_count, unsigned long obj_size);

int objpool_destroy(struct objpool* mm);

void* objpool_alloc(struct objpool* mm);
int objpool_free(struct objpool* mm, void* p);

int objpool_check(struct objpool* mm);

#endif

