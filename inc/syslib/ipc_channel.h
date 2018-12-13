#ifndef __ipc_channel_h__
#define __ipc_channel_h__

#define MIN_MSG_SIZE_ORDER	(4)		// >= MIN_MSG_SIZE_ORDER
#define MAX_MSG_SIZE_ORDER	(17)	// < MAX_MSG_SIZE_ORDER
#define MSG_POOL_COUNT		(MAX_MSG_SIZE_ORDER - MIN_MSG_SIZE_ORDER)

struct ipc_channel;
struct ipc_local_port;

typedef void (*ipc_read_func_t)(const char* buf, unsigned int size, int prod_service_type, int prod_service_index);

struct ipc_channel_cfg
{
	int cons_service_type;
	int cons_service_index;
	int message_queue_len;
	int message_count[MSG_POOL_COUNT];
};

int ipc_channel_create(const struct ipc_channel_cfg* cfg);
int ipc_channel_load(int cons_service_type, int cons_service_index);
int ipc_channel_destroy(int cons_service_type, int cons_service_index);

int ipc_open_cons_port(int service_type, int service_index, ipc_read_func_t read_func);
struct ipc_local_port* ipc_open_prod_port(int cons_service_type, int cons_service_index);

int ipc_close_cons_port(void);
int ipc_close_prod_port(struct ipc_local_port* local_port);

int ipc_read_sc(void);

char* ipc_alloc_write_buf_mp(struct ipc_local_port* local_port, unsigned int size, int from_service_type, int from_service_index);
int ipc_write_mp(struct ipc_local_port* local_port, const char* buf);

int ipc_channel_check_state(void);

#endif

