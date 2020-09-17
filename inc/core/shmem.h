#ifndef __shmem_h__
#define __shmem_h__

#include "rbtree.h"
#include "dlist.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SHMM_NAME_LEN (255)

struct shmm_blk
{
	u64 _shmm_tag;
	u64 _addr_begin_offset;
	u64 _addr_end_offset;

	void* _raw_addr;
	i32 _the_key;
	i32 _fd;

	struct rbnode rb_node;
	struct dlnode lst_node;
};

struct shmm_blk* shmm_create(i32 key, u64 size, i32 try_huge_page);
struct shmm_blk* shmm_open(i32 key, void* at_addr);
struct shmm_blk* shmm_open_raw(i32 key, void* at_addr);
struct shmm_blk* shmm_reload(i32 key);


i32 shmm_close(struct shmm_blk* shm);
i32 shmm_destroy(struct shmm_blk* shm);

void* shmm_begin_addr(struct shmm_blk* shm);
void* shmm_end_addr(struct shmm_blk* shm);

#ifdef __cplusplus
}
#endif

#endif

