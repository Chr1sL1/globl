#include "common_types.h"
#include "core/misc.h"
#include "core/asm.h"
#include <sys/time.h>

static u64 __rand_seed = 0;


void rand_init(u32 seed)
{
	__rand_seed = seed;
}

u64 rand_ex(u64 range)
{
	if(range > 0)
	{
		__rand_seed = (__rand_seed * 2862933555777941757ULL) + 1;
		return __rand_seed % range;
	}

	return 0;
}

u64 rand_ex_min_max(u64 min, u64 max)
{
	return min + rand_ex(max - min);
}

u64 sys_time_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, 0);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
