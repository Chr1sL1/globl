#ifndef _HASH_H_
#define _HASH_H_

u64 hash_file_name(const char* pcszFileName);
u32 hash_string(const char* pcszString);
i32 jump_consist_hash(u64 hash_key, i32 bucket_size);

#endif	// _HASH_H_
