#ifndef __ipc_h__
#define __ipc_h__

struct ipc_peer
{
	long channel_id;
	unsigned long buffer_size;
};

struct ipc_peer* ipc_create(int channel_id, unsigned long buffer_size, int use_huge_tlb);
struct ipc_peer* ipc_link(int channel_id);

long ipc_unlink(struct ipc_peer* pr);
long ipc_destroy(struct ipc_peer* pr);

long ipc_write(struct ipc_peer* pr, const void* buff, long size);
long ipc_read(struct ipc_peer* pr, void* buff, long size);


#endif

