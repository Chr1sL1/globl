#include "common_types.h"
#include "core/vm_space.h"
#include "core/rbtree.h"
#include "core/hash.h"
#include "core/misc.h"
#include "core/asm.h"
#include "core/vm_page_alloc.h"
#include "core/vm_slab_alloc.h"
#include <stdio.h>
#include <string.h>

#ifdef __linux__
#include <unistd.h>
#include <errno.h>
#include <link.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif // __linux__

#define VM_CHUNK_NAME_LEN	(128)
#define VM_SPACE_TAG		(0x12345678ABCDEF00)
#define VM_CHUNK_TAG		(0xAB34EF7812CD3400)

#define VM_CACHELINE_SIZE	(64)

#define VM_PAGE_BITS		(12)
#define VM_PAGE_SIZE		(1ULL << VM_PAGE_BITS)

#define VM_2MB_PAGE_BITS	(21)
#define VM_2MB_PAGE_SIZE	(1ULL << VM_2MB_PAGE_BITS)

#define VM_1GB_PAGE_BITS	(30)
#define VM_1GB_PAGE_SIZE	(1ULL << VM_1GB_PAGE_BITS)

#define VM_HUGETLB_SHIFT	(26)
#define VM_HUGETLB_2MB		(21 << VM_HUGETLB_SHIFT)
#define VM_HUGETLB_1GB		(30 << VM_HUGETLB_SHIFT)

#define VM_RAND_START_MIN		(128ULL * 1024 * 1024 * 1024)
#define VM_RAND_SIZE			(16 * 1024)   // MB
#define VM_CHUNK_CNT_LIMIT	(256)

#define VM_COMMON_ALLOCATOR_NAME	"core_common_allocator"

struct vm_space;

struct vm_chunk
{
	void* chunk_data;
	u64 data_size;
	struct rbnode rb_node;
};

struct vm_space
{
	u64 qwTag;
	void* pStartAddr;
	void* pEndAddr;
	u64 qwTotalSize;
	u64 qwUsedSize;
	i32 nTlbType;
	i32 nFd;
	i32 bLastInitSuccess;
	i32 nNextChunkIdx;
	void* real_addr;
	struct rbtree rb_root;
	struct vm_page_alloc* page_pool;
	struct vm_slab_allocator* slab_allocator;
	struct vm_chunk chunk_record[VM_CHUNK_CNT_LIMIT];
};


static void* __brk_addr = 0;
static u64 __min_so_addr = (u64)-1LL;
static u64 __max_so_addr = 0LL;
static struct vm_space* __the_space = NULL;

static inline struct vm_chunk* __get_chunk_by_rbnode(struct rbnode* rbn)
{
	return (struct vm_chunk*)((u64)rbn - (u64)&(((struct vm_chunk*)(0))->rb_node));
}

#ifdef __linux__

static i32 __on_iterate_phdr(struct dl_phdr_info* info, size_t size, void* data)
{
	i32 bFindOverlap = 0;
	i32 nRetCode = 0;
	i32 nPageAreaCount = 0;

	printf("loaded: %s, segments: %d, base addr: %10lu", info->dlpi_name, info->dlpi_phnum, info->dlpi_addr);

	for(i32 i = 0; i < info->dlpi_phnum; ++i)
	{
		u64 qwStartAddr = (u64)((info->dlpi_addr + info->dlpi_phdr[i].p_vaddr) & (~(VM_PAGE_SIZE - 1)));
		u64 qwEndAddr = (u64)((info->dlpi_addr + info->dlpi_phdr[i].p_vaddr + info->dlpi_phdr[i].p_memsz + VM_PAGE_SIZE) & (~(VM_PAGE_SIZE - 1)));

		if(qwStartAddr < __min_so_addr && qwStartAddr > (u64)__brk_addr)
			__min_so_addr = qwStartAddr;

		if(qwEndAddr > __max_so_addr && qwEndAddr > (u64)__brk_addr)
			__max_so_addr = qwEndAddr;
	}

	return 0;
error_ret:
	return -1;
}

static inline void __gather_placed_vm_area(void)
{
	__brk_addr = sbrk(0);
	dl_iterate_phdr(__on_iterate_phdr, NULL);
}

static inline i32 __check_vm_area_overlap(u64 qwStartAddr, u64 qwEndAddr)
{
	return qwEndAddr <= __min_so_addr || qwStartAddr >= __max_so_addr;
}

#endif

static inline i32 __destroy_vm_space(struct vm_space* pSpace);

#ifdef __linux__
static i32 __shm_get(i32 nKey, i32 bTryHugeTLB, i32 bCreateNew, u64* qwSize, i32* nTlbType)
{
	i32 nRetCode = 0;
	i32 nOriShmFlag = 0, nShmFlag = 0;
	i32 nFd = 0;
	u64 qwTotalSize = *qwSize;

	i32 b1GBFailed = 0;

	err_exit(nKey == IPC_PRIVATE || nKey <= 0, "");

	nOriShmFlag = SHM_R | SHM_W | S_IRUSR | S_IWUSR;

	if(bCreateNew)
		nOriShmFlag = nOriShmFlag | IPC_CREAT | IPC_EXCL;

	while(1)
	{
		if(bTryHugeTLB)
		{
			if(!b1GBFailed)
			{
				*nTlbType = SHM_HUGETLB | VM_HUGETLB_1GB;
				qwTotalSize = round_up(*qwSize, VM_1GB_PAGE_SIZE);
			}
			else
			{
				*nTlbType = SHM_HUGETLB | VM_HUGETLB_2MB;
				qwTotalSize = round_up(*qwSize, VM_2MB_PAGE_SIZE);
			}
		}
		else
		{
			*nTlbType = 0;
			qwTotalSize = round_up(*qwSize, VM_PAGE_SIZE);
		}

		nShmFlag = nOriShmFlag | *nTlbType;

		nFd = shmget(nKey, qwTotalSize, nShmFlag);
		if(nFd <= 0)
		{
			err_exit(!bTryHugeTLB, "key: %d, qwTotalSize: %lu, errno: %d", nKey, qwTotalSize, errno);

			if(!b1GBFailed)
				b1GBFailed = 1;
			else
				bTryHugeTLB = 0;
		}
		else
			break;
	}

	*qwSize = qwTotalSize;

	return nFd;
error_ret:
	printf("__shm_get failed with error: %s", strerror(errno));
	return -1;
}
#endif

i32 vm_create_space(i32 nKey, u64 qwSize, i32 bTryHugeTLB, i32 nLogicPageSizeK, i32 nMaxPageCntPerAlloc)
{
	i32 nRetCode = 0;
	i32 nFd = 0, nTlbType = 0;
	void* pAddr = NULL;

	err_exit(__the_space != NULL, "");

	qwSize = round_up(qwSize, VM_PAGE_SIZE) + round_up(sizeof(struct vm_space), VM_PAGE_SIZE);

#ifdef __linux__

	__gather_placed_vm_area();

	nRetCode = vm_open_space(nKey);

	if(nRetCode >= 0)
		vm_destroy_space();

	nFd = __shm_get(nKey, bTryHugeTLB, 1, &qwSize, &nTlbType);
	err_exit(nFd <= 0, "");

	pAddr = (void*)round_up((u64)__brk_addr + VM_RAND_START_MIN + rand_ex(VM_RAND_SIZE) * 1024ULL * 1024ULL, VM_PAGE_SIZE); 

	__the_space = (struct vm_space*)shmat(nFd, pAddr, SHM_RND);
	if (__the_space == (void*)-1)
	{
		shmctl(nFd, IPC_RMID, 0);
		err_exit(1, "");
	}

#else
	__the_space = (struct vm_space*)VirtualAlloc(NULL, qwSize, MEM_COMMIT, PAGE_READWRITE);
	if (!__the_space)
	{
		printf("vm create space error code: %u.", GetLastError());
		err_exit(1, "");
	}
#endif

	__the_space->qwTag = VM_SPACE_TAG;
	__the_space->qwTotalSize = qwSize;
	__the_space->pStartAddr = __the_space;
	__the_space->pEndAddr = (char*)__the_space->pStartAddr + qwSize;
	__the_space->nTlbType = nTlbType;
	__the_space->nFd = nFd;
	__the_space->bLastInitSuccess = 0;
	__the_space->real_addr = (char*)__the_space + round_up(sizeof(struct vm_space), VM_PAGE_SIZE);
	rb_init(&__the_space->rb_root, 0);

	__the_space->page_pool = vpp_create(__the_space->real_addr, __the_space->qwTotalSize, nLogicPageSizeK, nMaxPageCntPerAlloc);
	err_exit(!__the_space->page_pool, "init page pool failed.");

	memset(__the_space->chunk_record, 0, sizeof(__the_space->chunk_record));

	return 0;
error_ret:
	return -1;
}

i32 vm_open_space(i32 nKey)
{
#ifdef __linux__
	i32 nRetCode = 0;
	i32 nFd = 0, nTlbType = 0;
	struct vm_space* pTmpSpace = NULL;
	void* pStartAddr = NULL;
	u64 qwSize = 0;
	i32 bTryHugeTLB = 0;

	err_exit(__the_space != NULL, "");

	__gather_placed_vm_area();

	qwSize = sizeof(struct vm_space);
	nFd = __shm_get(nKey, 0, 0, &qwSize, &nTlbType);
	err_exit(nFd <= 0, "");

	pTmpSpace = (struct vm_space*)shmat(nFd, NULL, 0);
	err_exit(pTmpSpace == (void*)(-1), "");
	err_exit(pTmpSpace->qwTag != VM_SPACE_TAG, "");

	pStartAddr = pTmpSpace->pStartAddr;
	err_exit(pStartAddr == NULL, "");

	nTlbType = pTmpSpace->nTlbType;
	qwSize = pTmpSpace->qwTotalSize;

	nRetCode = __check_vm_area_overlap((u64)pStartAddr, (u64)pStartAddr + pTmpSpace->qwTotalSize); 
	err_exit(!nRetCode, "");

	shmdt(pTmpSpace);
	pTmpSpace = NULL;

	bTryHugeTLB = (nTlbType & SHM_HUGETLB) == SHM_HUGETLB;

	nFd = __shm_get(nKey, bTryHugeTLB, 0, &qwSize, &nTlbType);
	err_exit(nFd <= 0, "");

	__the_space = (struct vm_space*)shmat(nFd, pStartAddr, 0);

	err_exit((void*)__the_space != pStartAddr, "");
	err_exit(__the_space->qwTag != VM_SPACE_TAG, "");

	__the_space->nFd = nFd;

	__the_space->page_pool = vpp_load(__the_space->page_pool);
	err_exit_silent(!__the_space->page_pool);

	return 0;
error_ret:
	if(pTmpSpace)
		shmdt(pTmpSpace);
	return -1;
#else
	return -1;
#endif
}

i32 vm_destroy_space(void)
{
	err_exit(!__the_space, "");
	err_exit(__the_space->qwTag != VM_SPACE_TAG, "");

#ifdef __linux__

	shmctl(__the_space->nFd, IPC_RMID, 0);
	shmdt(__the_space);
	__the_space = NULL;
#else
	VirtualFree((LPVOID)__the_space, __the_space->qwTotalSize, MEM_RELEASE);
#endif

	return 0;
error_ret:
	return -1;
}

static i32 __find_valid_chunk_pos(void)
{
	for(i32 i = __the_space->nNextChunkIdx; i < VM_CHUNK_CNT_LIMIT; ++i)
	{
		if(__the_space->chunk_record[i].chunk_data == NULL && __the_space->chunk_record[i].data_size == 0)
			return i;
	}

	return -1;
}

void* vm_new_chunk(const char* name, u64 chunk_size)
{
	i32 result;
	u64 name_hash;
	struct vm_chunk* new_chunk;

	err_exit(!__the_space, "");
	err_exit(__the_space->qwTag != VM_SPACE_TAG, "");
	err_exit(__the_space->nNextChunkIdx >= VM_CHUNK_CNT_LIMIT, "no more chunks");
	err_exit(__the_space->chunk_record[__the_space->nNextChunkIdx].chunk_data != NULL || __the_space->chunk_record[__the_space->nNextChunkIdx].data_size != 0, "invalid free chunk.");

	name_hash = hash_string(name);
	new_chunk = &__the_space->chunk_record[__the_space->nNextChunkIdx];

	rb_fillnew(&new_chunk->rb_node);
	new_chunk->rb_node.key = (void*)name_hash;

	result = rb_insert(&__the_space->rb_root, &new_chunk->rb_node);
	err_exit(result < 0, "rb_insert failed.");

	new_chunk->chunk_data = vpp_alloc(__the_space->page_pool, chunk_size);
	err_exit(new_chunk->chunk_data == 0, "vpp_alloc chunk data failed.");

	new_chunk->data_size = chunk_size;
	__the_space->nNextChunkIdx = __find_valid_chunk_pos();

	return new_chunk->chunk_data;

error_ret:
	rb_remove_node(&__the_space->rb_root, &new_chunk->rb_node);
	return NULL;
}

static inline struct vm_chunk* __find_vm_chunk_by_hash(u64 name_hash)
{
	struct rbnode* node = rb_search(&__the_space->rb_root, (void*)name_hash);
	return __get_chunk_by_rbnode(node);
}

static struct vm_chunk* __find_vm_chunk(const char* name)
{
	struct rbnode* node;
	u64 name_hash;
	err_exit(!__the_space, "");
	err_exit(__the_space->qwTag != VM_SPACE_TAG, "");

	name_hash = hash_string(name);

	return __find_vm_chunk_by_hash(name_hash);
error_ret:
	return NULL;
}

void* vm_find_chunk(const char* name)
{
	struct vm_chunk* chunk = __find_vm_chunk(name);
	err_exit_silent(!chunk);

	return chunk->chunk_data;
error_ret:
	return NULL;
}

i32 vm_del_chunk(const char* name)
{
	u64 name_hash;
	struct vm_chunk* chunk;
	err_exit(!__the_space, "");
	err_exit(__the_space->qwTag != VM_SPACE_TAG, "");

	name_hash = hash_string(name);

	chunk = __find_vm_chunk_by_hash(name_hash);
	err_exit(!chunk, "can not find chunk name %s", name);

	vpp_free(__the_space->page_pool, chunk->chunk_data);
	rb_remove(&__the_space->rb_root, (void*)name_hash);

	chunk->chunk_data = NULL;
	chunk->data_size = 0;

	__the_space->nNextChunkIdx = chunk - __the_space->chunk_record;

	return 0;
error_ret:
	return -1;
}

void* vm_alloc_page(u64 require_size)
{
	err_exit_silent(!__the_space);
	err_exit_silent(__the_space->qwTag != VM_SPACE_TAG);
	err_exit(!__the_space->page_pool, "invalid page pool");

	return vpp_alloc(__the_space->page_pool, require_size);
error_ret:
	return NULL;
}

i32 vm_free_page(void* page)
{
	err_exit_silent(!__the_space);
	err_exit_silent(__the_space->qwTag != VM_SPACE_TAG);
	err_exit(!__the_space->page_pool, "invalid page pool");

	return vpp_free(__the_space->page_pool, page);
error_ret:
	return -1;
}

i32 vm_create_common_allocator(u32 min_obj_size, u32 max_obj_size, u32 init_obj_cnt)
{
	err_exit_silent(!__the_space);
	err_exit_silent(__the_space->qwTag != VM_SPACE_TAG);
	err_exit(__the_space->slab_allocator, "common allocator exists.");

	__the_space->slab_allocator = vsa_create(VM_COMMON_ALLOCATOR_NAME, min_obj_size, max_obj_size, init_obj_cnt, 3);
	err_exit(!__the_space->slab_allocator, "create common allocator failed.");

	return 0;
error_ret:
	return -1;
}

void* vm_common_alloc(u32 obj_size)
{
	err_exit_silent(!__the_space);
	err_exit_silent(__the_space->qwTag != VM_SPACE_TAG);
	err_exit(!__the_space->slab_allocator, "invalid common allocator.");

	return vsa_alloc(__the_space->slab_allocator, obj_size);
error_ret:
	return NULL;
}

void vm_common_free(void* obj)
{
	err_exit_silent(!__the_space);
	err_exit_silent(__the_space->qwTag != VM_SPACE_TAG);
	err_exit(!__the_space->slab_allocator, "invalid common allocator.");

	vsa_free(__the_space->slab_allocator, obj);
error_ret:
	return;
}

i32 vm_check_last_success(void)
{
	err_exit(!__the_space, "");
	return __the_space->bLastInitSuccess;
error_ret:
	return 0;
}

i32 vm_reset_last_success(void)
{
	err_exit(!__the_space, "");
	__the_space->bLastInitSuccess = 0;

	return 1;
error_ret:
	return 0;
}

i32 vm_set_last_success(void)
{
	err_exit(!__the_space, "");
	__the_space->bLastInitSuccess = 1;

	return 1;
error_ret:
	return 0;
}

i32 vm_mem_usage(struct VMUsage* pUsageData)
{
	err_exit(!__the_space, "");

	pUsageData->llUsedSize = __the_space->qwUsedSize;
	pUsageData->llTotalSize = __the_space->qwTotalSize;

	return 1;
error_ret:
	return 0;
}

