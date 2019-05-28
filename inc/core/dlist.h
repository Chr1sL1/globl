#ifndef __dlist_h__
#define __dlist_h__

struct dlnode
{
	struct dlnode* prev;
	struct dlnode* next;
};

struct dlist
{
//	long size;
	struct dlnode head;
	struct dlnode tail;
};

long lst_new(struct dlist* lst);
long lst_clr(struct dlnode* node);
long lst_empty(struct dlist* lst);


long lst_insert_before(struct dlist* lst, struct dlnode* suc, struct dlnode* node);
long lst_insert_after(struct dlist* lst, struct dlnode* prv, struct dlnode* node);

long lst_remove(struct dlist* lst, struct dlnode* node);
long lst_remove_node(struct dlnode* node);

long lst_push_back(struct dlist* lst, struct dlnode* node);
long lst_push_front(struct dlist* lst, struct dlnode* node);

struct dlnode* lst_pop_back(struct dlist* lst);
struct dlnode* lst_pop_front(struct dlist* lst);

struct dlnode* lst_first(struct dlist* lst);
struct dlnode* lst_last(struct dlist* lst);

long lst_check(struct dlist* lst);

#endif
