#include "common_types.h"
#include "core/misc.h"
#include "core/asm.h"

static u32 __rand_seed = 0;


void rand_init(u32 seed)
{
	__rand_seed = seed;
}

i64 rand_ex(u64 range)
{
	if(range > 0)
	{
		__rand_seed = (u32)(__rand_seed * 0x08088405) + 1;
		return (i64)((__rand_seed * range) >> 32);
	}

	return 0;
}

