#ifndef __mm_ops_h__
#define __mm_ops_h__

struct mm_config
{
	u64 total_size;

	union
	{
		// for mmpool
		struct
		{
			u32 min_order;
			u32 max_order;
		};

		// for pgpool
		struct
		{
			u32 page_size;
			u32 maxpg_count;
		};

		// for stkpool
		struct
		{
			u32 stk_frm_size;
		};
	};
};

typedef void* (*mm_ops_create)(void*, struct mm_config*);
typedef void* (*mm_ops_load)(void*);
typedef void (*mm_ops_destroy)(void*);
typedef void* (*mm_ops_alloc)(void*, u64);
typedef i32 (*mm_ops_free)(void*, void*);
typedef void (*mm_ops_counts)(void*, u64*, u64*);

struct mm_ops
{
	mm_ops_create create_func;
	mm_ops_load load_func;
	mm_ops_destroy destroy_func;

	mm_ops_alloc alloc_func;
	mm_ops_free free_func;

	mm_ops_counts counts_func;
};


i32 create_mmspace_key(i32 area_type, i32 area_idx, i32 app_type, i32 app_idx);
i32 create_ipc_channel_key(i32 service_type, i32 service_idx);

#endif

