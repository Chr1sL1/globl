#include "common_types.h"
#include "core/vm_space.h"
#include "core/rbtree.h"
#include "core/hash.h"
#include "core/misc.h"
#include "core/asm.h"
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

struct vm_space;
struct vm_chunk;

struct vm_space
{
	u64 qwTag;
	void* pStartAddr;
	void* pEndAddr;
	u64 qwTotalSize;
	u64 qwUsedSize;
	i32 nChunkCount;
	i32 nTlbType;
	i32 nFd;
	i32 bLastInitSuccess;
	void* pFirstChunkAddr;
	void* pNextChunkAddr;
	struct rbtree RBRoot;
};

struct vm_chunk
{
	u64 qwTag;
	u64 qwChunkDataSize;
	void* pChunkData;
	void* pNextChunk;
	struct rbnode RBNode;
};

static void* __brk_addr = 0;
static u64 __min_so_addr = (u64)-1LL;
static u64 __max_so_addr = 0LL;
static struct vm_space* __the_space = NULL;

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

	err_exit(nKey != IPC_PRIVATE && nKey > 0, "");

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

i32 vm_create_space(i32 nKey, u64 qwSize, i32 bTryHugeTLB)
{
	i32 nRetCode = 0;
	i32 nFd = 0, nTlbType = 0;
	void* pAddr = NULL;

	err_exit(__the_space != NULL, "");

	qwSize = round_up(qwSize + sizeof(struct vm_space), VM_PAGE_SIZE);

#ifdef __linux__

	__gather_placed_vm_area();

	nRetCode = vm_open_space(nKey);

	if(nRetCode)
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
	__the_space->nChunkCount = 0;
	__the_space->bLastInitSuccess = 0;
	__the_space->pFirstChunkAddr = (char*)__the_space + VM_PAGE_SIZE;
	__the_space->pNextChunkAddr = __the_space->pFirstChunkAddr;

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

	return 0;
error_ret:
	if(pTmpSpace)
		shmdt(pTmpSpace);
	return -1;
#else
	return -1;
#endif
}

static inline struct vm_chunk* __do_new_chunk(u64 qwChunkSize)
{
	struct vm_chunk* pChunk;
	void* pChunkEndAddr;

	err_exit(!__the_space, "");
	err_exit(!__the_space->pNextChunkAddr, "");

	qwChunkSize = round_up(qwChunkSize + sizeof(struct vm_chunk), VM_PAGE_SIZE);
	pChunkEndAddr = (char*)__the_space->pNextChunkAddr + qwChunkSize;
	err_exit(pChunkEndAddr > __the_space->pEndAddr, "oom");

	pChunk = (struct vm_chunk*)__the_space->pNextChunkAddr;
	pChunk->qwTag = VM_CHUNK_TAG;
	pChunk->qwChunkDataSize = qwChunkSize;
	pChunk->pChunkData = move_ptr_align64(pChunk, sizeof(struct vm_chunk));
	pChunk->pNextChunk = NULL;
	__the_space->pNextChunkAddr = (void*)round_up((u64)pChunkEndAddr + 1, VM_PAGE_SIZE);
	++__the_space->nChunkCount;

	__the_space->qwUsedSize += qwChunkSize;

	return pChunk;
error_ret:
	return NULL;
}

void* vm_new_chunk(const char* szName, u64 qwChunkSize)
{
	i32 nRetCode = 0;
	u64 qwHashValue;
	struct vm_chunk* pChunk = NULL;

	err_exit(!__the_space, "vm space not initialized.");
	err_exit(!__the_space->pNextChunkAddr, "");

	qwHashValue = hash_file_name(szName);

	pChunk = vm_find_chunk(szName);
	err_exit(pChunk != NULL, "chunk exists.");

	pChunk = __do_new_chunk(qwChunkSize);
	err_exit(pChunk == NULL, "new chunk failed.");

	pChunk->RBNode.key = (void*)hash_file_name(szName);

	nRetCode = rb_insert(&__the_space->RBRoot, &pChunk->RBNode);
	err_exit(!nRetCode, "");

	return pChunk->pChunkData;
error_ret:
	return NULL;
}

void* vm_find_chunk(const char* szName)
{
	i32 nRetCode = 0;
	u64 qwHashValue = 0;

	struct vm_chunk* pChunk = NULL;
	struct rbnode* pRBNode = NULL;

	err_exit(!__the_space, "");

	qwHashValue = hash_file_name(szName);

	pRBNode = rb_search((void*)qwHashValue, &__the_space->RBRoot);
	err_exit_silent(!pRBNode);

	pChunk = (struct vm_chunk*)((u64)(pRBNode) - (u64)(&((struct vm_chunk*)(0))->RBNode));
	err_exit(pChunk->qwTag != VM_CHUNK_TAG, "");

	return pChunk->pChunkData;
error_ret:
	return NULL;
}

static inline struct vm_chunk* __get_vm_chunk(void* pInChunk)
{
	struct vm_chunk* pVMChunk = (struct vm_chunk*)(pInChunk - round_up(sizeof(struct vm_chunk), 64));
	err_exit(pVMChunk->qwTag != VM_CHUNK_TAG, "invalid chunk.");

	return pVMChunk;
error_ret:
	return NULL;
}

void* vm_link_chunk(void* pChunk, u64 qwChunkSize)
{
	struct vm_chunk* pRetChunk;
	struct vm_chunk* pPrevChunk = __get_vm_chunk(pChunk);
	err_exit_silent(pPrevChunk == NULL);
	err_exit(pPrevChunk->pNextChunk != NULL, "chunk linked.");

	pRetChunk = __do_new_chunk(qwChunkSize);
	err_exit(pRetChunk == NULL, "new chunk failed.");

	pPrevChunk->pNextChunk = pRetChunk;

	return pRetChunk->pChunkData;
error_ret:
	return NULL;
}

void* vm_next_chunk(void* pInChunk)
{
	struct vm_chunk* pChunk;
	err_exit_silent(pInChunk == NULL);

	pChunk = __get_vm_chunk(pInChunk);
	err_exit_silent(pChunk == NULL);

	return pChunk->pChunkData;
error_ret:
	return NULL;
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

	pUsageData->llChunkCnt = __the_space->nChunkCount;
	pUsageData->llUsedSize = __the_space->qwUsedSize;
	pUsageData->llTotalSize = __the_space->qwTotalSize;

	return 1;
error_ret:
	return 0;
}

