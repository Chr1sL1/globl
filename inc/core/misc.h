#ifndef __misc_h__
#define __misc_h__

#ifdef __cplusplus
extern "C" {
#endif

#define likely(x)		__builtin_expect(!!(x), 1)
#define unlikely(x)		__builtin_expect(!!(x), 0)

#define err_exit(stmt, msg, ...)\
	do {\
		if(unlikely(stmt)){\
			fprintf(stderr, "<%s:%d:%s> ", __FILE__, __LINE__, __FUNCTION__);\
			fprintf(stderr, msg, ##__VA_ARGS__);\
			fprintf(stderr, "\n");\
		goto error_ret;\
		}\
	} while(0);

#define err_exit_silent(stmt)\
	do {\
		if(unlikely(stmt)){\
		goto error_ret;\
		}\
	} while(0);

#define cache_line_size	64
#define __cache_aligned__	__attribute__((aligned(cache_line_size)))

#define mfence	asm volatile ("mfence")
#define lfence	asm volatile ("lfence")
#define sfence	asm volatile ("sfence")
#define spin_wait asm volatile ("pause")

#define node_cast(out_type, ptr, m_var)\
	(struct out_type*)((void*)ptr - (size_t)&(((struct out_type*)0)->m_var))

static inline i64 get_max(i64 a, i64 b)
{
	return a - ((a-b) & (a-b) >> 63);
}

static inline i64 get_min(i64 a, i64 b)
{
	return b + ((a-b) & (a-b) >> 63);
}

void rand_init(u32 seed);
i64 rand_ex(u64 range);

#ifdef __cplusplus
}
#endif

#endif
