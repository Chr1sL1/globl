#ifndef __hash_h__
#define __hash_h__

#include "dlist.h"

#define HASH_KEY_LEN (64)

struct hash_node
{
	struct dlnode list_node;
	char hash_key[HASH_KEY_LEN];
};


struct hash_table
{
	u64 bucket_size;
	struct dlist* hash_list;
};

i32 hash_insert(struct hash_table* ht, struct hash_node* hn);
struct hash_node* hash_search(struct hash_table* ht, const char* key);
i32 hash_remove(struct hash_table* ht, const char* key);

#endif

