#ifndef __ipc_channel_h__
#define __ipc_channel_h__

#define MIN_MSG_SIZE_ORDER	(4)		// >= MIN_MSG_SIZE_ORDER
#define MAX_MSG_SIZE_ORDER	(17)	// < MAX_MSG_SIZE_ORDER
#define MSG_POOL_COUNT		(MAX_MSG_SIZE_ORDER - MIN_MSG_SIZE_ORDER)

struct ipc_channel;
struct ipc_local_port;

struct ipc_channel_cfg
{
	int cons_service_type;
	int cons_service_index;
	int message_queue_len;
	int message_count[MSG_POOL_COUNT];
};

int ipc_channel_create(const struct ipc_channel_cfg* cfg);

struct ipc_local_port* ipc_open_prod_port(int service_type, int service_index);
int ipc_open_cons_port(int service_type, int service_index);

int ipc_close_port(struct ipc_local_port* local_port);

int ipc_read_sc(char* buf, unsigned int* size, unsigned int* prod_service_type, unsigned int* prod_service_index);

char* ipc_alloc_write_buf_mp(struct ipc_local_port* local_port, unsigned int size, int to_service_type, int to_service_index);
int ipc_write_mp(struct ipc_local_port* local_port, const char* buf);

#endif

