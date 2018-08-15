#include "syslib/shmem.h"
#include "syslib/misc.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define SHMM_MID_PAGE_THRESHOLD		(1UL << 18)
#define SHMM_HUGE_PAGE_THRESHOLD	(1UL << 28)

#define SHMM_TAG					(0x1234567890abcdef)

#define SHM_PAGE_SHIFT				(12)
#define SHM_PAGE_SIZE				(1UL << SHM_PAGE_SHIFT)

#define SHM_HUGE_SHIFT				(26)
#define SHM_HUGE_2MB				(21 << SHM_HUGE_SHIFT)
#define SHM_HUGE_1GB				(30 << SHM_HUGE_SHIFT)

struct _shmm_blk_impl
{
	unsigned long _shmm_tag;
	struct shmm_blk _the_blk;

	int _the_key;
	int _fd;
};

static inline struct _shmm_blk_impl* _conv_blk(struct shmm_blk* blk)
{
	return (struct _shmm_blk_impl*)((unsigned long)blk - (unsigned long)&(((struct _shmm_blk_impl*)(0))->_the_blk));
}

inline void* shmm_begin_addr(struct shmm_blk* shm)
{
	if(!shm) goto error_ret;

	return (void*)shm + shm->addr_begin_offset;
error_ret:
	return 0;
}

inline void* shmm_end_addr(struct shmm_blk* shm)
{
	if(!shm) goto error_ret;

	return (void*)shm + shm->addr_end_offset;
error_ret:
	return 0;
}

struct shmm_blk* shmm_create(int key, void* at_addr, unsigned long size, int try_huge_page)
{
	int flag;
	int fd;
	void* ret_addr = 0;
	void* addr_begin;
	struct _shmm_blk_impl* sbi;

	if(key == IPC_PRIVATE || key <= 0 || size <= 0)
		goto error_ret;

	if(((unsigned long)at_addr & (SHM_PAGE_SIZE - 1)) != 0)
		goto error_ret;

__shmm_create_remap:	
	flag = 0;

#ifdef __linux__
	if(try_huge_page)
	{
		if(size > SHMM_MID_PAGE_THRESHOLD)
		{
			flag |= SHM_HUGETLB;
			printf("huge page used by key: 0x%x.\n", key);
		}
	}
#endif

	flag |= IPC_CREAT;
	flag |= IPC_EXCL;
	flag |= SHM_R;
	flag |= SHM_W;
	flag |= S_IRUSR;
	flag |= S_IWUSR;

	size = round_up(size, SHM_PAGE_SIZE) + SHM_PAGE_SIZE;

	fd = shmget(key, size, flag);
	if(fd < 0)
	{
		if(try_huge_page)
		{
			try_huge_page = 0;
			goto __shmm_create_remap;
		}

		goto error_ret;
	}

	ret_addr = shmat(fd, at_addr, SHM_RND);
	if(ret_addr == (void*)(-1))
		goto error_ret;

	sbi = (struct _shmm_blk_impl*)ret_addr;
	sbi->_shmm_tag = SHMM_TAG;

	sbi->_the_blk.addr_end_offset = size;

	addr_begin = move_ptr_roundup(ret_addr, sizeof(struct _shmm_blk_impl), SHM_PAGE_SIZE);
	sbi->_the_blk.addr_begin_offset = addr_begin - ret_addr;

	sbi->_the_key = key;
	sbi->_fd = fd;

	rb_fillnew(&sbi->_the_blk.rb_node);

	return &sbi->_the_blk;
error_ret:
	if(sbi)
	{
		if(ret_addr)
			shmdt(ret_addr);
	}
	return 0;

}

struct shmm_blk* shmm_open(int key, void* at_addr)
{
	at_addr = _conv_blk((struct shmm_blk*)at_addr);
	return shmm_open_raw(key, at_addr);
error_ret:
	return 0;
}

struct shmm_blk* shmm_open_raw(int key, void* at_addr)
{
	int flag = 0;
	int fd;
	void* ret_addr = 0;
	struct _shmm_blk_impl* sbi = 0;

	if(key == IPC_PRIVATE || key <= 0)
		goto error_ret;

	if(at_addr && ((unsigned long)at_addr & (SHM_PAGE_SIZE - 1)) != 0)
		goto error_ret;

	flag |= SHM_R;
	flag |= SHM_W;
	flag |= S_IRUSR;
	flag |= S_IWUSR;

	fd = shmget(key, 0, flag);
	if(fd < 0)
		goto error_ret;

	ret_addr = shmat(fd, at_addr, 0);
	if(ret_addr == (void*)(-1))
		goto error_ret;

	sbi = (struct _shmm_blk_impl*)ret_addr;
	if(sbi->_shmm_tag != SHMM_TAG || sbi->_the_key != key)
		goto error_ret;

	return &sbi->_the_blk;
error_ret:
	if(sbi)
	{
		if(ret_addr)
			shmdt(ret_addr);
	}
	return 0;

}

inline long shmm_close(struct shmm_blk* shm)
{
	struct _shmm_blk_impl* sbi = _conv_blk(shm);
	if(!sbi->_the_blk.addr_begin_offset || !sbi->_the_blk.addr_end_offset)
		goto error_ret;

	if(sbi->_shmm_tag != SHMM_TAG)
		goto error_ret;

	shmdt((void*)shm);

	return 0;
error_ret:
	return -1;
}

inline long shmm_destroy(struct shmm_blk* shm)
{
	int fd;
	long rslt;
	void* ret_addr;
	struct _shmm_blk_impl* sbi = _conv_blk(shm);

	rslt = shmctl(sbi->_fd, IPC_RMID, 0);
	if(rslt < 0)
		goto error_ret;

	if(shmm_close(shm) < 0)
		goto error_ret;

	return 0;
error_ret:
	return -1;
}
