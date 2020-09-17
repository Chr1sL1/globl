#ifndef __timer_h__
#define __timer_h__

struct timer_node;
typedef void(*timer_func_t)(struct timer_node*, void*);

i32 timer_module_load(void);
void timer_module_unload(void);

struct timer_node* timer_schedule(u32 delay_tick, timer_func_t callback_func, i32 run_once, void* param);

void del_timer(struct timer_node* timer_node);

void on_tick(void);

u64 dbg_current_tick(void);

#endif

