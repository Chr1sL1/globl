#ifndef __dlist_h__
#define __dlist_h__

struct dlnode
{
	struct dlnode* prev;
	struct dlnode* next;
};

struct dlist
{
//	i32 size;
	struct dlnode head;
	struct dlnode tail;
};

i32 lst_new(struct dlist* lst);
i32 lst_clr(struct dlnode* node);
i32 lst_empty(struct dlist* lst);


i32 lst_insert_before(struct dlist* lst, struct dlnode* suc, struct dlnode* node);
i32 lst_insert_after(struct dlist* lst, struct dlnode* prv, struct dlnode* node);

i32 lst_remove(struct dlist* lst, struct dlnode* node);
i32 lst_remove_node(struct dlnode* node);

i32 lst_push_back(struct dlist* lst, struct dlnode* node);
i32 lst_push_front(struct dlist* lst, struct dlnode* node);

struct dlnode* lst_pop_back(struct dlist* lst);
struct dlnode* lst_pop_front(struct dlist* lst);

struct dlnode* lst_first(struct dlist* lst);
struct dlnode* lst_last(struct dlist* lst);

i32 lst_check(struct dlist* lst);

#endif
