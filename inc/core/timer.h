#ifndef __timer_h__
#define __timer_h__

struct timer_node;
typedef void(*timer_func_t)(struct timer_node*, void*);

int init_timer(void);

struct timer_node* timer_schedule(unsigned int delay_tick, timer_func_t callback_func, int run_once, void* param);

void del_timer(struct timer_node* timer_node);

void on_tick(void);

unsigned long dbg_current_tick(void);

#endif

