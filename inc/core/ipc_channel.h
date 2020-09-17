#ifndef __ipc_channel_h__
#define __ipc_channel_h__

// multi producer(prod), single consumer(cons), lock-free ipc channel.

#ifdef __cplusplus
extern "C" {
#endif

#define MIN_MSG_SIZE_ORDER	(4)		// >= MIN_MSG_SIZE_ORDER
#define MAX_MSG_SIZE_ORDER	(17)	// < MAX_MSG_SIZE_ORDER
#define MSG_POOL_COUNT		(MAX_MSG_SIZE_ORDER - MIN_MSG_SIZE_ORDER)

struct ipc_channel;
struct ipc_prod_port;
struct ipc_cons_port;

typedef void (*ipc_read_func_t)(const char* buf, u32 size, i32 prod_service_type, i32 prod_service_index);

struct ipc_service_key
{
	u32 service_type;
	u32 service_index;
};

struct ipc_channel_cfg
{
	struct ipc_service_key cons_service_key;
	i32 message_queue_len;
	i32 message_count[MSG_POOL_COUNT];
};


i32 ipc_channel_create(const struct ipc_channel_cfg* cfg);
i32 ipc_channel_load(struct ipc_service_key* cons_service_key);
i32 ipc_channel_destroy(struct ipc_service_key* cons_service_key);

struct ipc_cons_port* ipc_open_cons_port(struct ipc_service_key* cons_service_key, ipc_read_func_t read_func);
struct ipc_prod_port* ipc_open_prod_port(struct ipc_service_key* cons_service_key, struct ipc_service_key* prod_service_key);

i32 ipc_close_cons_port(struct ipc_cons_port* cons_port);
i32 ipc_close_prod_port(struct ipc_prod_port* prod_port);

i32 ipc_read_sc(struct ipc_cons_port* cons_port);

char* ipc_alloc_write_buf_mp(struct ipc_prod_port* prod_port, u32 size);
i32 ipc_write_mp(struct ipc_prod_port* prod_port, const char* buf);

i32 ipc_channel_check_state_cons(struct ipc_cons_port* cons_port);
i32 ipc_channel_check_state_prod(struct ipc_prod_port* prod_port);

#ifdef __cplusplus
}
#endif

#endif

