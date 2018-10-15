#ifndef __shm_key_h__
#define __shm_key_h__

int create_mmspace_key(int area_type, int area_idx, int app_type, int app_idx);
int create_ipc_channel_key(int service_type, int service_idx);
int create_msg_pool_key(int service_type, int service_idx, int pool_order, int pool_idx);

#endif

