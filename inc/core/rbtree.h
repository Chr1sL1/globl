#ifndef __rbtree_h__
#define __rbtree_h__

struct rbnode
{
	void* key;
	struct rbnode* lchild;
	struct rbnode* rchild;
	union
	{
		struct rbnode* p;
		unsigned isblack : 2;
	};
};


typedef void (*order_function)(struct rbnode* n);


// if key < n2->key, returns -1
// if key = n2->key, returns 0 
// if key > n2->key, returns 1
//
typedef i32 (*compare_function)(void* key, struct rbnode* n);

struct rbtree
{
	int size;
	int depth;
	struct rbnode* root;
	compare_function cfunc;
};

void rb_init(struct rbtree* t, compare_function cf);
void rb_fillnew(struct rbnode* node);
struct rbnode* rb_parent(struct rbnode* node);
struct rbnode* rb_sibling(struct rbnode* node);
struct rbnode* rb_succ(struct rbnode* node);

i32 rb_insert(struct rbtree* t, struct rbnode* node);
struct rbnode* rb_search(struct rbtree* t, void* key, struct rbnode** hot);
struct rbnode* rb_remove(struct rbtree* t, void* key);
void rb_remove_node(struct rbtree* t, struct rbnode* x);

void rb_reset_compare_function(struct rbtree* t, compare_function cf);

void rb_traverse(struct rbtree* t, order_function f);

//debug issue
void pre_order(struct rbnode* node, order_function f);
void in_order(struct rbnode* node, order_function f);
void post_order(struct rbnode* node, order_function f);

#endif
