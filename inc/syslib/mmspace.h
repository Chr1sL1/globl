#ifndef __mmspace_h__
#define __mmspace_h__

#include "mmops.h"

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// mmcache: object cache.
// struct mmcache its self is allocated from MM_AREA_PERSIS area,
// while cached objects are allocated from MM_AREA_CACHE area.

struct mmcache
{
	unsigned int obj_size;
	unsigned int padding;
};

typedef void (*mmcache_obj_ctor)(void* obj_ptr);
typedef void (*mmcache_obj_dtor)(void* obj_ptr);

struct mmcache* mm_cache_create(const char* name, unsigned int obj_size, mmcache_obj_ctor ctor, mmcache_obj_dtor dtor);
int mm_cache_destroy(struct mmcache* mmz);

void* mm_cache_alloc(struct mmcache* mmz);
long mm_cache_free(struct mmcache* mmz, void* p);


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// mmspace: a group of shared memory in which the whole process's data lays.

enum MM_AREA_TYPE
{
	MM_AREA_BEGIN = 0,

	MM_AREA_NUBBLE = MM_AREA_BEGIN,		//< small memory block
	MM_AREA_PAGE,						//< page-aligned memory block
	MM_AREA_CACHE,						//< for mmcache object
	MM_AREA_PERSIS,						//< for persistent data
	MM_AREA_STACK,						//< for persistent data

	MM_AREA_COUNT,
};

struct mm_space_config
{
	unsigned long sys_begin_addr;
	int sys_shmm_key;
	int try_huge_page;
	int max_shmm_count;

	struct mm_config mm_cfg[MM_AREA_COUNT];
};

long mm_initialize(struct mm_space_config* cfg);
long mm_reinitialize(struct mm_space_config* cfg);
long mm_uninitialize(void);

void mm_save_globl_data(void* p);
void* mm_load_globl_data(void);

struct mmcache* mm_search_zone(const char* zone_name);

void* mm_alloc(unsigned long size);
void* mm_area_alloc(unsigned long size, int area_type);

const struct mm_space_config* mm_get_cfg(void);

long mm_free(void* p);

#endif

