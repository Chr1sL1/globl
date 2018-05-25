#include "syslib/misc.h"
#include <string.h>


long quick_mmcpy(void* dst, void* src, unsigned long size)
{
	long s1 = size - ((long)align16(size) - 16);
	if(s1 > 0)
	{
		memcpy(dst, src, s1);
		dst += s1;
		src += s1;
		size -= s1;
	}

	if(size <= 0) goto succ_ret;

	if(((long)dst & 0xF) == 0 && ((long)src & 0xF) == 0 && (size & 0xF) == 0)
		return quick_mmcpy_a(dst, src, size);

	if((size & 0xF) == 0)
		return quick_mmcpy_u(dst, src, size);

	// ..... todo .....

succ_ret:
	return 0;
error_ret:
	return -1;
}

unsigned long round_up_2power(unsigned long val)
{
	return 1 << (log_2(val - 1) + 1);
}

unsigned long round_down_2power(unsigned long val)
{
	return 1 << (log_2(val));
}

long is_2power(unsigned long val)
{
	return round_up_2power(val) == round_down_2power(val);
}

unsigned long rdtsc(void)
{
	unsigned long th, tl;

	asm volatile ("rdtsc":"=a"(tl),"=d"(th));

	return (th << 32) + tl;
}


unsigned long log_2(unsigned long val)
{
	unsigned long ret;
	asm("bsrq	%1, %0":"=r"(ret):"r"(val));

	return ret;
}


inline unsigned long align8(unsigned long val)
{
	return ((val + 7) & ~7);

}

inline unsigned long align16(unsigned long val)
{
	return ((val + 15) & ~15);
}

inline unsigned long round_up(unsigned long val, unsigned long boundary)
{
	return ((val + boundary - 1) & ~(boundary - 1));
}

inline unsigned long round_down(unsigned long val, unsigned long boundary)
{
	return (val & ~(boundary - 1));
}


inline long bsf(unsigned long val)
{
	long ret = -1;
	if(val != 0)
		asm("bsfq %1, %0":"=r"(ret):"r"(val));

	return ret;
}

inline long bsr(unsigned long val)
{
	long ret = -1;
	if(val != 0)
		asm("bsrq %1, %0":"=r"(ret):"r"(val));

	return ret;
}

inline void* move_ptr_align8(void* ptr, unsigned long offset)
{
	return (void*)(((unsigned long)(ptr + offset) + 7) & (~7));
}

inline void* move_ptr_align16(void* ptr, unsigned long offset)
{
	return (void*)(((unsigned long)(ptr + offset) + 15) & (~15));
}

inline void* move_ptr_align64(void* ptr, unsigned long offset)
{
	return (void*)(((unsigned long)(ptr + offset) + 63) & (~63));
}

inline void* move_ptr_align128(void* ptr, unsigned long offset)
{
	return (void*)(((unsigned long)(ptr + offset) + 127) & (~127));
}

inline void* move_ptr_roundup(void* ptr, unsigned long offset, unsigned long align)
{
	return (void*)(((unsigned long)(ptr + offset) + (align - 1)) & (~(align - 1)));
}



