#include "common_types.h"
#include "core/shm_key.h"
#include "core/misc.h"

#include <stdlib.h>
#include <stdio.h>

#define SHM_TYPE_BITS		(4)

#define SHM_AR_TYPE_BITS		(4)
#define SHM_AR_INDEX_BITS		(8)
#define SHM_APP_TYPE_BITS		(6)
#define SHM_APP_INDEX_BITS		(10)

#define SHM_MSG_POOL_ORDER_BITS	(5)
#define SHM_MSG_POOL_INDEX_BITS	(7)

enum MM_SHM_TYPE
{
	MM_SHM_MEMORY_SPACE,
	MM_SHM_IPC,
	MM_SHM_MSG_POOL,

	MM_SHM_COUNT, // should be no more than 15.
};

#pragma pack(1)
union shm_key
{
	struct
	{
		union
		{
			struct
			{
				unsigned dummy : 28;
				unsigned type : SHM_TYPE_BITS;			// 4
			};

			struct
			{
				unsigned app_idx : SHM_APP_INDEX_BITS;
				unsigned app_type : SHM_APP_TYPE_BITS;
				unsigned area_idx : SHM_AR_INDEX_BITS;
				unsigned area_type : SHM_AR_TYPE_BITS;
			};

			struct
			{
				unsigned service_reserved : 12;
				unsigned service_idx : SHM_SERVICE_INDEX_BITS; // 6
				unsigned service_type : SHM_SERVICE_TYPE_BITS; // 10
			};

			struct 
			{
				unsigned msg_pool_idx : SHM_MSG_POOL_INDEX_BITS;	// 5
				unsigned msg_pool_order : SHM_MSG_POOL_ORDER_BITS;	// 7
				unsigned msg_pool_service_idx : SHM_SERVICE_INDEX_BITS; // 6
				unsigned msg_pool_service_type : SHM_SERVICE_TYPE_BITS; // 10
			};
		};
	};

	i32 the_key;
};
#pragma pack()

i32 create_mmspace_key(i32 area_type, i32 area_idx, i32 app_type, i32 app_idx)
{
	union shm_key key;
	key.the_key = 0;
	key.type = MM_SHM_MEMORY_SPACE;

	err_exit(area_type >= (1 << SHM_AR_TYPE_BITS), "area_type invalid.");
	err_exit(area_idx >= (1 << SHM_AR_INDEX_BITS), "area_idx invalid.");
	err_exit(app_type >= (1 << SHM_APP_TYPE_BITS), "app_type invalid.");
	err_exit(app_idx >= (1 << SHM_APP_INDEX_BITS), "app_idx invalid.");

	key.area_type = area_type;
	key.area_idx = area_idx;
	key.app_type = app_type;
	key.app_idx = app_idx;

	return key.the_key;
error_ret:
	return -1;
}

i32 create_ipc_channel_key(i32 service_type, i32 service_idx)
{
	union shm_key key;
	key.the_key = 0;
	key.type = MM_SHM_IPC;

	err_exit(service_type >= (1 << SHM_SERVICE_TYPE_BITS), "service_type invalid.");
	err_exit(service_idx >= (1 << SHM_SERVICE_INDEX_BITS), "service_idx invalid.");

	key.service_type = service_type;
	key.service_idx = service_idx;
	key.service_reserved = 0;

	return key.the_key;
error_ret:
	return -1;
}

i32 create_msg_pool_key(i32 service_type, i32 service_idx, i32 pool_order, i32 pool_idx)
{
	union shm_key key;
	key.the_key = 0;
	key.type = MM_SHM_MSG_POOL;

	err_exit(service_type >= (1 << SHM_SERVICE_TYPE_BITS), "service_type invalid.");
	err_exit(service_idx >= (1 << SHM_SERVICE_INDEX_BITS), "service_idx invalid.");
	err_exit(pool_order >= (1 << SHM_MSG_POOL_ORDER_BITS), "pool_order invalid.");
	err_exit(pool_idx >= (1 << SHM_MSG_POOL_INDEX_BITS), "pool_idx invalid.");

	key.msg_pool_service_type = service_type;
	key.msg_pool_service_idx = service_idx;
	key.msg_pool_order = pool_order;
	key.msg_pool_idx = pool_idx;

	return key.the_key;
error_ret:
	return -1;

}






