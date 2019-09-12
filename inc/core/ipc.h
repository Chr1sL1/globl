#ifndef __ipc_h__
#define __ipc_h__

struct ipc_peer
{
	i32 channel_id;
	u64 buffer_size;
};

struct ipc_peer* ipc_create(i32 channel_id, u64 buffer_size, i32 use_huge_tlb);
struct ipc_peer* ipc_link(i32 channel_id);

i32 ipc_unlink(struct ipc_peer* pr);
i32 ipc_destroy(struct ipc_peer* pr);

i32 ipc_write(struct ipc_peer* pr, const void* buff, i32 size);
i32 ipc_read(struct ipc_peer* pr, void* buff, i32 size);


#endif

