#include <stdio.h>
#include "core/slist.h"
#include "core/misc.h"


void sl_init(struct slist* the_list)
{
	the_list->_list_head._next = 0;
}

void sl_uninit(struct slist* the_list)
{

}

void sl_push_head(struct slist* the_list, struct slnode* the_node)
{
	the_node->_next = the_list->_list_head._next;
	the_list->_list_head._next = the_node->_next;
}

struct slnode* sl_pop_head(struct slist* the_list)
{
	struct slnode* node = the_list->_list_head._next;
	err_exit(node, "empty list.");

	the_list->_list_head._next = node->_next;
	node->_next = 0;
error_ret:
	return node;
}

