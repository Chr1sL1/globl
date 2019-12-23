#ifndef __shm_key_h__
#define __shm_key_h__

#define SHM_SERVICE_TYPE_BITS	(6)
#define SHM_SERVICE_INDEX_BITS	(10)

i32 create_vmspace_key(i32 area_type, i32 area_idx, i32 app_type, i32 app_idx);
i32 create_ipc_channel_key(i32 service_type, i32 service_idx);
i32 create_msg_pool_key(i32 service_type, i32 service_idx, i32 pool_order, i32 pool_idx);

#endif

