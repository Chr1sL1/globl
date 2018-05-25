#include "syslib/bin_tree.h"

void bt_fillnew(struct btnode* node)
{
	node->parent = 0;
	node->lchild = 0;
	node->rchild = 0;
}

long bt_insert(struct bintree* t, struct btnode* parent, struct btnode* node)
{
	if(t == 0 || parent == 0 || node == 0) goto error_ret;

	if(parent->lchild == 0)
		parent->lchild = node;
	else if(parent->rchild == 0)
		parent->rchild = node;
	else goto error_ret;

	node->parent = parent;

	return 0;
error_ret:
	return -1;
}

long bt_remove(struct bintree* t, struct btnode* node)
{
	if(t == 0 || node == 0) goto error_ret;

	if(t->root == node)
		t->root = 0;
	else
	{
		if(node->parent == 0)
			goto error_ret;

		if(node == node->parent->lchild)
			node->parent->lchild = 0;
		else if(node == node->parent->rchild)
			node->parent->rchild = 0;
		else goto error_ret;

		node->parent = 0;
	}

	return 0;
error_ret:
	return -1;
}

struct btnode* bt_sibling(struct btnode* node)
{
	if(node == 0 || node->parent == 0) goto error_ret;

	if(node == node->parent->lchild)
		return node->parent->rchild;
	else if(node == node->parent->rchild)
		return node->parent->lchild;

error_ret:
	return 0;
}
