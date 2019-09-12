#ifndef _ASM_H_
#define _ASM_H_

#if defined(WIN32) | defined(WIN64)

#include <intrin.h>


inline u64 rdtsc(void)
{
    return __rdtsc();
}

inline void INT3(void)
{
	__debugbreak();
}

inline u32 POPCNT32(u32 n)
{
	return _mm_popcnt_u32(n);
}

inline u64 POPCNT64(u32 n)
{
	return _mm_popcnt_u64(n);
}

inline i64 bsf(u64 val)
{
	i32 ret = -1;

	if(val != 0)
		_BitScanForward64((unsigned long*)&ret, val);

	return ret;
}

inline i64 bsr(u64 val)
{
	i32 ret = -1;

	if(val != 0)
		_BitScanReverse64((unsigned long*)&ret, val);

	return ret;
}

#else

static inline u64 rdtsc(void)
{
    u64 l, h;
    __asm__ volatile ("rdtsc" : "=a" (l), "=d" (h));

	return (h << 32) + l;
}

static inline void int3(void)
{
	__asm("int3");
}

static inline u32 popcnt32(u32 n)
{
	u32 ret;
	asm volatile ("popcnt %1, %0" : "=r"(ret) : "r"(n));
	return ret;
}

static inline u32 popcnt64(u64 n)
{
	u32 ret;
	asm volatile ("popcnt %1, %0" : "=r"(ret) : "r"(n));
	return ret;
}

static inline i64 bsf(u64 val)
{
	long ret = -1;
	if(val != 0)
		asm("bsfq %1, %0":"=r"(ret):"r"(val));

	return ret;
}

static inline i64 bsr(u64 val)
{
	long ret = -1;
	if(val != 0)
		asm("bsrq %1, %0":"=r"(ret):"r"(val));

	return ret;
}

#endif	// WIN32

static inline u64 log_2(u64 val)
{
	return bsr(val);
}

static inline u64 round_up_2power(u64 val)
{
	return 1 << (log_2(val - 1) + 1);
}

static inline u64 round_down_2power(u64 val)
{
	return 1 << (log_2(val));
}

static inline i32 is_2power(u64 val)
{
	return (val & (val - 1)) == 0;
//	return round_up_2power(val) == round_down_2power(val);
}

static inline u64 round_up(u64 val, i32 alignment)
{
	return ((val + (alignment - 1)) & (~(alignment - 1)));
}

static inline u64 round_down(u64 val, i32 alignment)
{
	return ((val) & (~(alignment - 1)));
}


static inline u64 roundup_order(u64 val, i32 alignment_order)
{
	return round_up(val, (1 << alignment_order));
}

static inline void* move_ptr_align8(void* ptr, u64 offset)
{
	return (void*)((((u64)ptr + offset) + 7) & (~7));
}

static inline void* move_ptr_align16(void* ptr, u64 offset)
{
	return (void*)((((u64)ptr + offset) + 15) & (~15));
}

static inline void* move_ptr_align64(void* ptr, u64 offset)
{
	return (void*)((((u64)ptr + offset) + 63) & (~63));
}

static inline void* move_ptr_align128(void* ptr, u64 offset)
{
	return (void*)((((u64)ptr + offset) + 127) & (~127));
}

static inline void* move_ptr_roundup(void* ptr, u64 offset, u64 align)
{
	return (void*)((((u64)ptr + offset) + (align - 1)) & (~(align - 1)));
}

#endif	// _ASM_H_
