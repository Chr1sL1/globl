#ifndef __VM_SLAB_ALLOC_H__
#define __VM_SLAB_ALLOC_H__

struct vm_slab_allocator;
struct vm_page_pool;

struct vm_slab_allocator* vsa_create(const char* allocator_name, struct vm_page_pool* page_pool, u32 min_obj_size, u32 max_obj_size, u32 init_obj_count, u32 alignment_order);
struct vm_slab_allocator* vsa_load(const char* allocator_name);

void vsa_destroy(struct vm_slab_allocator* allocator);

void* vsa_alloc(struct vm_slab_allocator* allocator, u64 size);
void vsa_free(struct vm_slab_allocator* allocator, void* p);

void vsa_debug_info(struct vm_slab_allocator* allocator);

#endif


