#ifndef __slist_h__
#define __slist_h__

struct slnode
{
	volatile struct slnode* _next;
};


struct slist
{
	struct slnode _list_head;
};

void sl_init(struct slist* the_list);
void sl_uninit(struct slist* the_list);

void sl_push_head(struct slist* the_list, struct slnode* the_node);
struct slnode* sl_pop_head(struct slist* the_list);

#endif

