#ifndef __net_h__
#define __net_h__

struct acceptor 
{
	unsigned int local_ip;
	unsigned int local_port;
};

struct session
{
	unsigned int remote_ip;
	unsigned int remote_port;
	void* usr_ptr;
};

// return 0 if succeed, return -1 if any error occurs.
typedef long (*on_acc_func)(struct acceptor* acc, struct session* se);
typedef long (*on_conn_func)(struct session* se);
typedef long (*on_disconn_func)(struct session* se);
typedef long (*on_recv_func)(struct session* se, const void* buf, long len);

//enum NET_TYPE
//{
//	NT_INTERNET,
//	NT_INTRANET,
//
//	NT_COUNT,
//};

struct net_config
{
	unsigned int send_buff_len;
	unsigned int recv_buff_len;
	unsigned int nr_acceptor;
	unsigned int nr_session;
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

struct net_struct 
{
	struct net_config cfg;
	struct net_ops ops;
};

struct net_struct* net_create(const struct net_config* cfg, const struct net_ops* ops);
long net_destroy(struct net_struct* net);

struct acceptor* net_create_acceptor(struct net_struct* net, unsigned int ip, unsigned short port);
long net_destroy_acceptor(struct acceptor* acc);

struct session* net_connect(struct net_struct* net, unsigned int ip, unsigned short port);
long net_disconnect(struct session* ses);

long net_send(struct session* ses, const char* data, int data_len);
long net_run(struct net_struct* net, int timeout);

long net_bind_session_ops(struct session* ses, const struct session_ops* ops);
long net_session_count(struct net_struct* net);


#endif

