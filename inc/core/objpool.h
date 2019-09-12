#ifndef __objpool_h__
#define __objpool_h__

struct objpool;

typedef void (*objpool_ctor)(void* obj_ptr);
typedef void (*objpool_dtor)(void* obj_ptr);

struct objpool* objpool_create(void* addr, u32 size, u32 obj_size, objpool_ctor ctor, objpool_dtor dtor);
struct objpool* objpool_load(void* addr, objpool_ctor ctor, objpool_dtor dtor);
u64 objpool_mem_usage(u64 obj_count, u64 obj_size);

i32 objpool_destroy(struct objpool* mm);

void* objpool_alloc(struct objpool* mm);
i32 objpool_free(struct objpool* mm, void* p);

i32 objpool_check(struct objpool* mm);

#endif

