#ifndef __bin_tree_h__
#define __bin_tree_h__

struct btnode
{
	struct btnode* parent;
	struct btnode* lchild;
	struct btnode* rchild;
};

struct bintree
{
	int size;
	int depth;
	struct btnode* root;
};


void bt_fillnew(struct btnode* node);
long bt_insert(struct bintree* t, struct btnode* parent, struct btnode* node);
long bt_remove(struct bintree* t, struct btnode* node);
struct btnode* bt_sibling(struct btnode* node);

#endif// __bin_tree_h__
