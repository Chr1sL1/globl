#ifndef __mm_h__
#define __mm_h__

struct mm_zone;

struct mm_zone* mm_create_zone(const char* zone_name, u64 zone_size);
void mm_destroy_zone(struct mm_zone* zone);

#endif
