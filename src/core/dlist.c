#include "common_types.h"
#include "core/dlist.h"

inline i32 lst_new(struct dlist* lst)
{
	if(!lst) goto error_ret; 

	lst->head.prev = 0;
	lst->head.next = &lst->tail;

	lst->tail.prev = &lst->head;
	lst->tail.next = 0;

//	lst->size = 0;

	return 0;
error_ret:
	return -1;
}

inline i32 lst_clr(struct dlnode* node)
{
	if(!node) goto error_ret;

	node->prev = 0;
	node->next = 0;

	return 0;
error_ret:
	return -1;
}

inline BOOL lst_empty(struct dlist* lst)
{
	if(lst->head.next == &lst->tail || lst->tail.prev == &lst->head)
		return TRUE;

	return FALSE;
}


inline i32 lst_insert_before(struct dlist* lst, struct dlnode* suc, struct dlnode* node)
{
	if(!lst || !suc || !node) goto error_ret;
	if(suc == &lst->head) goto error_ret;

	node->prev = suc->prev;
	node->next = suc;

	suc->prev->next = node;
	suc->prev = node;

//	++lst->size;

//	if(lst_check(lst) < 0)
//		goto error_ret;

	return 0;
error_ret:
	return -1;
}

inline i32 lst_insert_after(struct dlist* lst, struct dlnode* prv, struct dlnode* node)
{
	if(!lst || !prv || !node) goto error_ret;
	if(prv == &lst->tail) goto error_ret;

	node->prev = prv;
	node->next = prv->next;

	prv->next->prev = node;
	prv->next = node;

//	++lst->size;

//	if(lst_check(lst) < 0)
//		goto error_ret;

	return 0;
error_ret:
	return -1;
}

inline i32 lst_remove(struct dlist* lst, struct dlnode* node)
{
	if(!lst || !node) goto error_ret;
	if(!node->prev || !node->next) goto error_ret;
	if(lst->tail.prev == &lst->head || lst->head.next == &lst->tail) goto error_ret;
	if(node == &lst->head || node == &lst->tail) goto error_ret;

	node->prev->next = node->next;
	node->next->prev = node->prev;

	node->prev = 0;
	node->next = 0;
	
//	--lst->size;

	return 0;
error_ret:
	return -1;
}

i32 lst_remove_node(struct dlnode* node)
{
	if(!node) goto error_ret;
	if(!node->prev || !node->next) goto error_ret;

	node->prev->next = node->next;
	node->next->prev = node->prev;

	node->prev = 0;
	node->next = 0;
	
//	--lst->size;

	return 0;
error_ret:
	return -1;
}


inline i32 lst_push_back(struct dlist* lst, struct dlnode* node)
{
	return lst_insert_before(lst, &lst->tail, node);
error_ret:
	return -1;
}

inline i32 lst_push_front(struct dlist* lst, struct dlnode* node)
{
	return lst_insert_after(lst, &lst->head, node);
error_ret:
	return -1;
}

inline struct dlnode* lst_pop_back(struct dlist* lst)
{
	if(!lst) goto error_ret;

	struct dlnode* node = lst->tail.prev;
	if(lst_remove(lst, node) < 0) goto error_ret;
	return node;
error_ret:
	return 0;
}

inline struct dlnode* lst_pop_front(struct dlist* lst)
{
	if(!lst) goto error_ret;

	struct dlnode* node = lst->head.next;
	if(lst_remove(lst, node) < 0) goto error_ret;
	return node;
error_ret:
	return 0;
}

inline struct dlnode* lst_first(struct dlist* lst)
{
	return lst->head.next;
error_ret:
	return 0;
}

inline struct dlnode* lst_last(struct dlist* lst)
{
	return &lst->tail;
error_ret:
	return 0;

}
static i32 _lst_check_loop(struct dlist* lst)
{
	struct dlnode* pf = lst->head.next;
	struct dlnode* ps = lst->head.next;

	while(1)
	{
		if(pf == &lst->tail || pf->next == &lst->tail)
			break;
		if(ps == &lst->tail)
			break;

		pf = pf->next->next;
		ps = ps->next;

		if(pf == ps)
			return -1;
	}

	pf = lst->tail.prev;
	ps = lst->tail.prev;

	while(1)
	{
		if(pf == &lst->head|| pf->prev == &lst->head)
			break;
		if(ps == &lst->head)
			break;

		pf = pf->prev->prev;
		ps = ps->prev;

		if(pf == ps)
			return -1;
	}

	return 0;
}

i32 lst_check(struct dlist* lst)
{
	return _lst_check_loop(lst);
}


