#ifndef __mm_ops_h__
#define __mm_ops_h__

struct mm_config
{
	unsigned long total_size;

	union
	{
		// for mmpool
		struct
		{
			unsigned int min_order;
			unsigned int max_order;
		};

		// for pgpool
		struct
		{
			unsigned int page_size;
			unsigned int maxpg_count;
		};

		// for stkpool
		struct
		{
			unsigned int stk_frm_size;
		};
	};
};

typedef void* (*mm_ops_create)(void*, struct mm_config*);
typedef void* (*mm_ops_load)(void*);
typedef void (*mm_ops_destroy)(void*);
typedef void* (*mm_ops_alloc)(void*, unsigned long);
typedef long (*mm_ops_free)(void*, void*);
typedef void (*mm_ops_counts)(void*, unsigned long*, unsigned long*);

struct mm_ops
{
	mm_ops_create create_func;
	mm_ops_load load_func;
	mm_ops_destroy destroy_func;

	mm_ops_alloc alloc_func;
	mm_ops_free free_func;

	mm_ops_counts counts_func;
};


int create_mmspace_key(int area_type, int area_idx, int app_type, int app_idx);
int create_ipc_channel_key(int service_type, int service_idx);

#endif

