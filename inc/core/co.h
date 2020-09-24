#ifndef __co_h__
#define __co_h__

#ifdef __cplusplus
extern "C" {
#endif

struct co_task;

typedef void (*co_func_t)(struct co_task*, void*);

i32 co_module_load(u32 cocurrent_stack_cnt, u32 stack_frame_size);
void co_module_unload(void);

struct co_task* co_create(co_func_t func);
void co_destroy(struct co_task* co);

i32 co_run(struct co_task* co, void* co_func_param);
i32 co_yield(struct co_task* co);
i32 co_resume(struct co_task* co);

u64 co_profile_yield(struct co_task* co);
u64 co_profile_resume(struct co_task* co);

#ifdef __cplusplus
}
#endif

#endif

