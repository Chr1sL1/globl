#ifndef __shmem_h__
#define __shmem_h__

#include "rbtree.h"
#include "dlist.h"

#define MAX_SHMM_NAME_LEN (255)

struct shmm_blk
{
	unsigned long addr_begin_offset;
	unsigned long addr_end_offset;

	struct rbnode rb_node;
	struct dlnode lst_node;
};

struct shmm_blk* shmm_create(int key, unsigned long size, int try_huge_page);
struct shmm_blk* shmm_open(int key, void* at_addr);
struct shmm_blk* shmm_open_raw(int key, void* at_addr);
struct shmm_blk* shmm_reload(int key);


long shmm_close(struct shmm_blk* shm);
long shmm_destroy(struct shmm_blk* shm);

void* shmm_begin_addr(struct shmm_blk* shm);
void* shmm_end_addr(struct shmm_blk* shm);

#endif

