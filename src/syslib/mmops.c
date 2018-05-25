#include "syslib/mmops.h"


#define SHM_TYPE_SHIFT (28)
#define SHM_SYSID_SHIFT (16)


int mm_create_shm_key(int shm_type, int sys_id, const union shmm_sub_key* sub_key)
{
	if(shm_type >= MM_SHM_COUNT) goto error_ret;
	if(sys_id > (1 << (SHM_TYPE_SHIFT - SHM_SYSID_SHIFT + 1)) - 1) goto error_ret;

	return (shm_type << 28) + (sys_id << 16) + sub_key->sub_key;
error_ret:
	return -1;
}




