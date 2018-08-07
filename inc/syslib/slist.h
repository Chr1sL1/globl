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

void sl_init(struct slist* the_list) __attribute__((__always_inline__));
void sl_uninit(struct slist* the_list) __attribute__((__always_inline__));

void sl_push_head(struct slist* the_list, struct slnode* the_node) __attribute__((__always_inline__));
struct slnode* sl_pop_head(struct slist* the_list) __attribute__((__always_inline__));

#endif

