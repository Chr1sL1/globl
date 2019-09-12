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

struct _VMSpace;
struct _VMChunk;

struct _VMSpace
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

struct _VMChunk
{
	u64 qwTag;
	u64 qwChunkDataSize;
	void* pChunkData;
	struct rbnode RBNode;
	char szName[VM_CHUNK_NAME_LEN];
};

static void* __pBrkAddr = 0;
static u64 __qwMinSharedLibAddr = (u64)-1LL;
static u64 __qwMaxSharedLibAddr = 0LL;
static struct _VMSpace* __pVMSpace = NULL;

#ifdef __linux__

static i32 __on_iterate_phdr(struct dl_phdr_info* info, size_t size, void* data)
{
	i32 bFindOverlap = 0;
	i32 nRetCode = 0;
	i32 nPageAreaCount = 0;

	printf("loaded: %s, segments: %d, base addr: %10p", info->dlpi_name, info->dlpi_phnum, info->dlpi_addr);

	for(i32 i = 0; i < info->dlpi_phnum; ++i)
	{
		u64 qwStartAddr = (u64)((info->dlpi_addr + info->dlpi_phdr[i].p_vaddr) & (~(VM_PAGE_SIZE - 1)));
		u64 qwEndAddr = (u64)((info->dlpi_addr + info->dlpi_phdr[i].p_vaddr + info->dlpi_phdr[i].p_memsz + VM_PAGE_SIZE) & (~(VM_PAGE_SIZE - 1)));

		if(qwStartAddr < __qwMinSharedLibAddr && qwStartAddr > (u64)__pBrkAddr)
			__qwMinSharedLibAddr = qwStartAddr;

		if(qwEndAddr > __qwMaxSharedLibAddr && qwEndAddr > (u64)__pBrkAddr)
			__qwMaxSharedLibAddr = qwEndAddr;
	}

	return 0;
error_ret:
	return -1;
}

static inline void __gather_placed_vm_area(void)
{
	__pBrkAddr = sbrk(0);
	dl_iterate_phdr(__on_iterate_phdr, NULL);
}

static inline i32 __check_vm_area_overlap(u64 qwStartAddr, u64 qwEndAddr)
{
	return qwEndAddr <= __qwMinSharedLibAddr || qwStartAddr >= __qwMaxSharedLibAddr;
}

#endif

static inline i32 __destroy_vm_space(struct _VMSpace* pSpace);

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
			err_exit(!bTryHugeTLB, "key: %d, qwTotalSize: %llu, errno: %d", nKey, qwTotalSize, errno);

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

	err_exit(__pVMSpace != NULL, "");

	qwSize = round_up(qwSize + sizeof(struct _VMSpace), VM_PAGE_SIZE);

#ifdef __linux__

	__gather_placed_vm_area();

	nRetCode = vm_open_space(nKey);

	if(nRetCode)
		vm_destroy_space();

	nFd = __shm_get(nKey, bTryHugeTLB, 1, &qwSize, &nTlbType);
	err_exit(nFd <= 0, "");

	pAddr = (void*)round_up((u64)__pBrkAddr + VM_RAND_START_MIN + rand_ex(VM_RAND_SIZE) * 1024ULL * 1024ULL, VM_PAGE_SIZE); 

	__pVMSpace = (struct _VMSpace*)shmat(nFd, pAddr, SHM_RND);
	if (__pVMSpace == (void*)-1)
	{
		shmctl(nFd, IPC_RMID, 0);
		err_exit(1, "");
	}

#else
	__pVMSpace = (struct _VMSpace*)VirtualAlloc(NULL, qwSize, MEM_COMMIT, PAGE_READWRITE);
	if (!__pVMSpace)
	{
		printf("vm create space error code: %u.", GetLastError());
		err_exit(1, "");
	}
#endif

	__pVMSpace->qwTag = VM_SPACE_TAG;
	__pVMSpace->qwTotalSize = qwSize;
	__pVMSpace->pStartAddr = __pVMSpace;
	__pVMSpace->pEndAddr = (char*)__pVMSpace->pStartAddr + qwSize;
	__pVMSpace->nTlbType = nTlbType;
	__pVMSpace->nFd = nFd;
	__pVMSpace->nChunkCount = 0;
	__pVMSpace->bLastInitSuccess = 0;
	__pVMSpace->pFirstChunkAddr = (char*)__pVMSpace + VM_PAGE_SIZE;
	__pVMSpace->pNextChunkAddr = __pVMSpace->pFirstChunkAddr;

	return 0;
error_ret:
	return -1;
}

i32 vm_open_space(i32 nKey)
{
#ifdef __linux__
	i32 nRetCode = 0;
	i32 nFd = 0, nTlbType = 0;
	struct _VMSpace* pTmpSpace = NULL;
	void* pStartAddr = NULL;
	u64 qwSize = 0;
	i32 bTryHugeTLB = 0;

	err_exit(__pVMSpace != NULL, "");

	__gather_placed_vm_area();

	qwSize = sizeof(struct _VMSpace);
	nFd = __shm_get(nKey, 0, 0, &qwSize, &nTlbType);
	err_exit(nFd <= 0, "");

	pTmpSpace = (struct _VMSpace*)shmat(nFd, NULL, 0);
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

	__pVMSpace = (struct _VMSpace*)shmat(nFd, pStartAddr, 0);

	err_exit((void*)__pVMSpace != pStartAddr, "");
	err_exit(__pVMSpace->qwTag != VM_SPACE_TAG, "");

	__pVMSpace->nFd = nFd;

	return 0;
error_ret:
	if(pTmpSpace)
		shmdt(pTmpSpace);
	return -1;
#else
	return -1;
#endif
}

void* vm_new_chunk(const char* szName, u64 qwChunkSize)
{
	i32 nRetCode = 0;
	void* pChunkEndAddr = NULL;
	struct _VMChunk* pChunk = NULL;
	u64 qwHashValue = 0;

	err_exit(!__pVMSpace, "");
	err_exit(!__pVMSpace->pNextChunkAddr, "");

	qwChunkSize = round_up(qwChunkSize + sizeof(struct _VMChunk), VM_PAGE_SIZE);
	pChunkEndAddr = (char*)__pVMSpace->pNextChunkAddr + qwChunkSize;
	err_exit(pChunkEndAddr > __pVMSpace->pEndAddr, "");

	pChunk = (struct _VMChunk*)__pVMSpace->pNextChunkAddr;
	pChunk->RBNode.key = (void*)hash_file_name(szName);

	nRetCode = rb_insert(&__pVMSpace->RBRoot, &pChunk->RBNode);
	err_exit(!nRetCode, "");

	pChunk->qwTag = VM_CHUNK_TAG;
	pChunk->qwChunkDataSize = qwChunkSize;
	strncpy(pChunk->szName, szName, sizeof(pChunk->szName));

//	pChunk->pChunkData = (void*)(pChunk + 1);
	pChunk->pChunkData = move_ptr_align64(pChunk, sizeof(struct _VMChunk));
	__pVMSpace->pNextChunkAddr = (void*)round_up((u64)pChunkEndAddr + 1, VM_PAGE_SIZE);
	++__pVMSpace->nChunkCount;

	__pVMSpace->qwUsedSize += qwChunkSize;

	return pChunk->pChunkData;
error_ret:
	return NULL;
}

void* vm_find_chunk(const char* szName)
{
	i32 nRetCode = 0;
	u64 qwHashValue = 0;

	struct _VMChunk* pChunk = NULL;
	struct rbnode* pRBNode = NULL;
	struct rbnode* pHot = NULL;

	err_exit(!__pVMSpace, "");

	qwHashValue = hash_file_name(szName);

	pRBNode = rb_search((void*)qwHashValue, &__pVMSpace->RBRoot, &pHot);
	err_exit_silent(!pRBNode);

	pChunk = (struct _VMChunk*)((u64)(pRBNode) - (u64)(&((struct _VMChunk*)(0))->RBNode));
	err_exit(pChunk->qwTag != VM_CHUNK_TAG, "");

	return pChunk->pChunkData;
error_ret:
	return NULL;
}

i32 vm_destroy_space(void)
{
	err_exit(!__pVMSpace, "");
	err_exit(__pVMSpace->qwTag != VM_SPACE_TAG, "");

#ifdef __linux__

	shmctl(__pVMSpace->nFd, IPC_RMID, 0);
	shmdt(__pVMSpace);
	__pVMSpace = NULL;
#else
	VirtualFree((LPVOID)__pVMSpace, __pVMSpace->qwTotalSize, MEM_RELEASE);
#endif

	return 0;
error_ret:
	return -1;
}

i32 vm_check_last_success(void)
{
	err_exit(!__pVMSpace, "");
	return __pVMSpace->bLastInitSuccess;
error_ret:
	return 0;
}

i32 vm_reset_last_success(void)
{
	err_exit(!__pVMSpace, "");
	__pVMSpace->bLastInitSuccess = 0;

	return 1;
error_ret:
	return 0;
}

i32 vm_set_last_success(void)
{
	err_exit(!__pVMSpace, "");
	__pVMSpace->bLastInitSuccess = 1;

	return 1;
error_ret:
	return 0;
}

i32 vm_mem_usage(struct VMUsage* pUsageData)
{
	err_exit(!__pVMSpace, "");

	pUsageData->llChunkCnt = __pVMSpace->nChunkCount;
	pUsageData->llUsedSize = __pVMSpace->qwUsedSize;
	pUsageData->llTotalSize = __pVMSpace->qwTotalSize;

	return 1;
error_ret:
	return 0;
}

