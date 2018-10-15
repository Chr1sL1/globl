#ifndef __ipc_channel_h__
#define __ipc_channel_h__

#define MIN_MSG_SIZE_ORDER	(4)		// >= MIN_MSG_SIZE_ORDER
#define MAX_MSG_SIZE_ORDER	(17)	// < MAX_MSG_SIZE_ORDER
#define MSG_POOL_COUNT		(MAX_MSG_SIZE_ORDER - MIN_MSG_SIZE_ORDER)

struct ipc_channel;
struct ipc_local_port;

struct ipc_channel_buf
{
	char* buf;
	int buf_size;
};

struct ipc_channel_cfg
{
	int cons_service_type;
	int cons_service_index;
	int message_queue_len;
	int message_count[MSG_POOL_COUNT];
};

int ipc_channel_create(const struct ipc_channel_cfg* cfg);

struct ipc_local_port* ipc_open_port(int service_type, int service_index);
int ipc_close_port(struct ipc_local_port* local_port);

int ipc_read_sc(struct ipc_local_port* local_port, struct ipc_channel_buf* channel_buf);

struct ipc_channel_buf* ipc_channel_fetch_buf(struct ipc_local_port* local_port, int size);
int ipc_write_sp(struct ipc_local_port* local_port, struct ipc_channel_buf* channel_buf);
int ipc_write_mp(struct ipc_local_port* local_port, struct ipc_channel_buf* channel_buf);

#endif

