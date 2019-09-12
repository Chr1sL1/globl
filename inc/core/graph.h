#ifndef __graph_h__
#define __graph_h__

struct dlnode;
struct dlist;

struct rbnode;
struct rbtree;

struct ngb_node
{
	i32 key;
	i32 weight;
	struct dlnode ngb_node;
};

struct gnode
{
	struct rbnode table_node;
	struct dlist ngb_list;
	void* udata;
	i32 visited;
};

struct graph
{
	struct rbtree gnode_table;
};

typedef i32 (*_search_func)(struct gnode*);

i32 graph_new(struct graph* g);
i32 graph_add_node(struct graph* g, struct gnode* node, i32 key);
struct gnode* graph_del_node(struct graph* g, i32 node_key);
i32 graph_add_edge(struct graph* g, struct ngb_node* ngb1, struct ngb_node* ngb2);

i32 graph_bfs(struct graph* g, i32 node_from, _search_func f);
i32 graph_dfs(struct graph* g, i32 node_from, _search_func f);

#endif
