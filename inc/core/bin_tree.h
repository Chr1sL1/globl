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
	i32 size;
	i32 depth;
	struct btnode* root;
};


void bt_fillnew(struct btnode* node);
i32 bt_insert(struct bintree* t, struct btnode* parent, struct btnode* node);
i32 bt_remove(struct bintree* t, struct btnode* node);
struct btnode* bt_sibling(struct btnode* node);

#endif// __bin_tree_h__
