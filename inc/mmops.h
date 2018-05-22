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

enum MM_SHM_TYPE
{
	MM_SHM_MEMORY_SPACE,
	MM_SHM_IPC,

	MM_SHM_COUNT, // should be no more than 15.
};


#pragma pack(1)
union shmm_sub_key
{
	struct
	{
		unsigned char ar_idx;
		unsigned char ar_type;
	};

	struct
	{
		unsigned short ipc_channel;
	};

	unsigned short sub_key;
};
#pragma pack()

// shm_type should be no more than MM_SHM_COUNT.
// sys_id should be no more than 8191.
//
int mm_create_shm_key(int shm_type, int sys_id, const union shmm_sub_key* sub_key);

#endif

