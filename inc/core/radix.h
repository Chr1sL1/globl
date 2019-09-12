#ifndef __radix_h__
#define __radix_h__

struct radix_node
{
	union
	{
		struct radix_node** sub_node;
		void* leaf_node;
		unsigned is_leaf : 2;
	};
};

struct radix_tree
{
	struct radix_node* root;
	i32 depth;
	i32 layer_node_count;
};



#endif

