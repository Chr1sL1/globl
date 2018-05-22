#ifndef __graph_h__
#define __graph_h__

struct dlnode;
struct dlist;

struct rbnode;
struct rbtree;

struct ngb_node
{
	int key;
	int weight;
	struct dlnode ngb_node;
};

struct gnode
{
	struct rbnode table_node;
	struct dlist ngb_list;
	void* udata;
	int visited;
};

struct graph
{
	struct rbtree gnode_table;
};

typedef int (*_search_func)(struct gnode*);

int graph_new(struct graph* g);
int graph_add_node(struct graph* g, struct gnode* node, int key);
struct gnode* graph_del_node(struct graph* g, int node_key);
int graph_add_edge(struct graph* g, struct ngb_node* ngb1, struct ngb_node* ngb2);

int graph_bfs(struct graph* g, int node_from, _search_func f);
int graph_dfs(struct graph* g, int node_from, _search_func f);

#endif
