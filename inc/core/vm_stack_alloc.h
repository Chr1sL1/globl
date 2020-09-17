#ifndef __stk_pool_h__
#define __stk_pool_h__


#ifdef __cplusplus
extern "C" {
#endif

struct vm_stack_allocator;

struct vm_stack_allocator* stack_allocator_create(const char* allocator_name, u64 cocurent_stack_cnt, u64 stack_frm_size);
struct vm_stack_allocator* stack_allocator_load(const char* allocator_name);
void stack_allocator_destroy(struct vm_stack_allocator* stkp);

void* stack_allocator_alloc(struct vm_stack_allocator* stkp, u64* stack_frame_size);
i32 stack_allocator_free(struct vm_stack_allocator* stkp, void* p);

#ifdef __cplusplus
}
#endif

#endif

