#ifndef __net_h__
#define __net_h__

struct net_struct;
struct acceptor;
struct session;

// return 0 if succeed, return -1 if any error occurs.
typedef i32 (*on_acc_func)(struct acceptor* acc, struct session* se);
typedef i32 (*on_conn_func)(struct session* se);
typedef i32 (*on_disconn_func)(struct session* se);
typedef i32 (*on_recv_func)(struct session* se, const void* buf, i32 len);

struct net_config
{
	u32 send_buff_len;
	u32 recv_buff_len;
	u32 nr_acceptor;
	u32 nr_session;
};

struct net_ops
{
	on_acc_func func_acc;
	on_conn_func func_conn;
	on_recv_func func_recv;
	on_disconn_func func_disconn;
};

struct session_ops
{
	on_conn_func func_conn;
	on_recv_func func_recv;
	on_disconn_func func_disconn;
};

struct net_struct* net_create(const struct net_config* cfg, const struct net_ops* ops);
i32 net_destroy(struct net_struct* net);

struct acceptor* net_create_acceptor(struct net_struct* net, u32 ip, u16 port);
i32 net_destroy_acceptor(struct acceptor* acc);

struct session* net_connect(struct net_struct* net, u32 ip, u16 port);
i32 net_disconnect(struct session* ses);

i32 net_send(struct session* ses, const char* data, i32 data_len);
i32 net_run(struct net_struct* net, i32 timeout);

i32 net_bind_session_ops(struct session* ses, const struct session_ops* ops);
i32 net_session_count(struct net_struct* net);

i32 net_set_user_ptr(struct session* ses, void* usr_ptr);
void* net_get_user_ptr(struct session* ses);



#endif

