#ifndef __vmspace_h__
#define __vmspace_h__

i32 vm_create_space(i32 nKey, u64 qwSize, i32 bTryHugePage);
i32 vm_open_space(i32 nKey);

void* vm_new_chunk(const char* szName, u64 qwChunkSize);
void* vm_find_chunk(const char* szName);

i32 vm_destroy_space(void);

i32 vm_check_last_success(void);
i32 vm_reset_last_success(void);
i32 vm_set_last_success(void);

struct VMUsage
{
	u64 llChunkCnt;
	u64 llUsedSize;
	u64 llTotalSize;
};

i32 vm_mem_usage(struct VMUsage* pUsageData);

#endif

