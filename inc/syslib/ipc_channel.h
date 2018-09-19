#ifndef __ipc_channel_h__
#define __ipc_channel_h__

struct ipc_channel;

struct ipc_channel_buf
{
	char* buf;
	int buf_size;
};

struct ipc_channel_cfg
{
	int cons_service_type;
	int cons_service_index;
	int max_message_size_order;
};

int ipc_channel_create(const struct ipc_channel_cfg* cfg);

struct ipc_channel* ipc_channel_open(int channel_id);
int ipc_channel_close(struct ipc_channel* channel);

int ipc_channel_read_sc(struct ipc_channel* channel, char** buf, int* size);

struct ipc_channel_buf* ipc_channel_fetch_buf(struct ipc_channel* channel, int size);
int ipc_channel_write_sp(struct ipc_channel* channel, struct ipc_channel_buf* channel_buf);
int ipc_channel_write_mp(struct ipc_channel* channel, struct ipc_channel_buf* channel_buf);

#endif

