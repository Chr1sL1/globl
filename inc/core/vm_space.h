#ifndef __vmspace_h__
#define __vmspace_h__

i32 vm_create_space(i32 nKey, u64 qwSize, i32 bTryHugePage, i32 nLogicPageSizeK, i32 nMaxPageCntPerAlloc);
i32 vm_open_space(i32 nKey);
i32 vm_destroy_space(void);

void* vm_new_chunk(const char* name, u64 chunk_size);
i32 vm_del_chunk(const char* name);
void* vm_find_chunk(const char* name);

void* vm_alloc_page(u64 require_size);
i32 vm_free_page(void* page);

i32 vm_create_common_allocator(u32 min_obj_size, u32 max_obj_size, u32 init_obj_cnt);
void* vm_common_alloc(u32 obj_size);
void vm_common_free(void* obj);

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

