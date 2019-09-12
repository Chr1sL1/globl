#ifndef __vm_page_pool_h__
#define __vm_page_pool_h__

struct vm_page_pool;

struct vm_page_pool* vpp_create(void* addr, u64 total_size, u64 page_size_k, u64 max_page_count_per_alloc);
struct vm_page_pool* vpp_load(void* addr);
void vpp_destroy(struct vm_page_pool* up);

void* vpp_alloc(struct vm_page_pool* up, u64 size);
void* vpp_alloc_page(struct vm_page_pool* up);
void* vpp_realloc(struct vm_page_pool* up, void* origin_addr, u64 size);

void* vpp_get_page(struct vm_page_pool* up, void* ptr);

i32 vpp_free(struct vm_page_pool* up, void* p);


i32 vpp_check(struct vm_page_pool* up);

#endif
