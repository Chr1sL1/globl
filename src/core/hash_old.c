#include "common_types.h"
#include "core/hash_old.h"
#include <string.h>

u64 _hash_value(const char* key, u64 bucket_size)
{
	u64 sum = 0;

	for(int i = 0; key[i] != 0; ++i)
		sum += key[i];

	return sum % bucket_size;
}


inline i32 hash_insert(struct hash_table* ht, struct hash_node* hn)
{
	u64 hash_value;

	if(!ht || !hn) goto error_ret;
	if(ht->bucket_size == 0) goto error_ret;

	lst_clr(&hn->list_node);

	hash_value = _hash_value(hn->hash_key, ht->bucket_size);

	return lst_push_back(&ht->hash_list[hash_value], &hn->list_node);
error_ret:
	return -1;
}

static struct hash_node* _hash_search(struct hash_table* ht, u64 hash_value, const char* key)
{
	struct dlnode* dln;
	struct hash_node* hn = 0;

	dln = ht->hash_list[hash_value].head.next;

	while(dln != &ht->hash_list[hash_value].tail)
	{
		hn = (struct hash_node*)((u64)dln - (u64)(&((struct hash_node*)(0))->list_node));

		if(strcmp(hn->hash_key, key) == 0)
			break;

		dln = dln->next;
	}

	return hn;
error_ret:
	return 0;
}

struct hash_node* hash_search(struct hash_table* ht, const char* key)
{
	struct dlnode* dln;
	struct hash_node* hn = 0;
	u64 hash_value;

	if(!ht || !key) goto error_ret;
	if(ht->bucket_size == 0) goto error_ret;

	hash_value = _hash_value(key, ht->bucket_size);

	return _hash_search(ht, hash_value, key);
error_ret:
	return 0;
}


i32 hash_remove(struct hash_table* ht, const char* key)
{
	struct dlnode* dln;
	struct hash_node* hn = 0;
	u64 hash_value;

	if(!ht || !key) goto error_ret;
	if(ht->bucket_size == 0) goto error_ret;

	hash_value = _hash_value(key, ht->bucket_size);

	hn = _hash_search(ht, hash_value, key);
	if(!hn) goto error_ret;

	return lst_remove(&ht->hash_list[hash_value], &hn->list_node);
error_ret:
	return -1;
}

