#include "core/net.h"
#include "core/mmpool.h"
#include "core/dlist.h"
#include "core/rbtree.h"
#include "core/misc.h"
#include "core/objpool.h"

#ifdef __linux__
#define _GNU_SOURCE
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#define NET_BACKLOG_COUNT 1024
#define MSG_SIZE_LEN (2)

#define ACC_TYPE_INFO (0x11335577)
#define SES_TYPE_INFO (0x22446688)

enum _SESSION_STATE
{
	_SES_INVALID,
	_SES_ESTABLISHING,
	_SES_NORMAL,
	_SES_CLOSING,
	_SES_CLOSED,
};

struct _inet_impl;
struct _acc_impl;
struct _ses_impl;

//typedef long (*_nt_init_func)(struct _inet_impl* inet);
//typedef long (*_nt_destroy_func)(struct _inet_impl* inet);
//
//typedef struct _acc_impl* (*_nt_create_acc_func)(struct _inet_impl* inet, unsigned int ip, unsigned short port);
//typedef long (*_nt_destroy_acc_func)(struct _acc_impl* aci);
//
//typedef struct _ses_impl* (*_nt_create_session_func)(struct _inet_impl* inet, int socket_fd);
//typedef long (*_nt_disconn_func)(struct _ses_impl* sei);
//
//typedef long (*_nt_run_func)(struct _inet_impl* inet, int timeout);
//
//typedef void (*_nt_set_outbit_func)(struct _inet_impl* inet, struct _ses_impl* sei);
//typedef void (*_nt_clr_outbit_func)(struct _inet_impl* inet, struct _ses_impl* sei);

//struct _nt_ops
//{
//	_nt_init_func __init_func;
//	_nt_destroy_func __destroy_func;
//
//	_nt_create_acc_func __create_acc_func;
//	_nt_destroy_acc_func __destroy_acc_func;
//
//	_nt_create_session_func __create_ses_func;
//	_nt_disconn_func __disconn_func;
//
//	_nt_run_func __run_func;
//};

//struct _poll_obj
//{
//	struct rbnode _rbn;
//	void* _obj;
//};

struct _inet_impl
{
	struct net_struct _the_net;
	struct dlist _acc_list;
	struct dlist _ses_list;
//	struct _nt_ops* _handler;
	struct objpool* _ses_pool;
	struct objpool* _acc_pool;

	struct
	{
		struct epoll_event* _ep_ev;
		int _epoll_fd;
	};

//	union
//	{
//		struct
//		{
//			struct epoll_event* _ep_ev;
//			int _epoll_fd;
//		};

//		struct
//		{
//			struct rbtree _po_rbt;
//			int _po_cnt;
//		};
//	};
};

struct _acc_impl
{
	unsigned int _type_info;
	int _sock_fd;

	struct _inet_impl* _inet;
	struct acceptor _the_acc;
	struct dlnode _lst_node;

//	struct _poll_obj* _obj;
};

struct _ses_impl
{
	unsigned int _type_info;
	int _sock_fd;

	struct _inet_impl* _inet;
	struct session _the_ses;
	struct dlnode _lst_node;
	struct session_ops _ops;

//	struct _poll_obj* _obj;

	char* _recv_buf;
	char* _send_buf;

	unsigned int _bytes_recv;
	unsigned int _recv_buf_len;

	unsigned int _send_buf_len;
	int _r_offset;
	int _w_offset;

	int _po_idx;
	int _state;
	int _debug_type;
};

static struct linger __linger_option =
{
	.l_onoff = 1,
	.l_linger = 4,
};

//static struct mmcache* __the_acc_zone = 0;
//static struct mmcache* __the_ses_zone = 0;
//static struct mmcache* __the_poll_obj_zone = 0;

static long _internet_init(struct _inet_impl* inet);
//static long _intranet_init(struct _inet_impl* inet);

static long _internet_destroy(struct _inet_impl* inet);
//static long _intranet_destroy(struct _inet_impl* inet);

static struct _acc_impl* _internet_create_acceptor(struct _inet_impl* inet, unsigned int ip, unsigned short port);
//static struct _acc_impl* _intranet_create_acceptor(struct _inet_impl* inet, unsigned int ip, unsigned short port);

static long _internet_destroy_acceptor(struct _acc_impl* aci);
//static long _intranet_destroy_acceptor(struct _acc_impl* aci);

static struct _ses_impl* _internet_create_session(struct _inet_impl* inet, int socket_fd);
//static struct _ses_impl* _intranet_create_session(struct _inet_impl* inet, int socket_fd);

static long _internet_disconn(struct _ses_impl* sei);
//static long _intranet_disconn(struct _ses_impl* sei);

static long _internet_run(struct _inet_impl* inet, int timeout);
//static long _intranet_run(struct _inet_impl* inet, int timeout);

static long _internet_on_acc(struct _acc_impl* aci);
static long _net_close(struct _ses_impl* sei);
static void _net_on_recv(struct _ses_impl* sei);

static void _net_on_error(struct _ses_impl* sei);
static long _net_try_send_all(struct _ses_impl* sei);
static void _net_on_send(struct _ses_impl* sei);
static long _net_disconn(struct _ses_impl* sei);
static long _net_destroy_acc(struct _acc_impl* aci);

//struct _nt_ops __net_ops[NT_COUNT] =
//{
//	[NT_INTERNET] = {
//		.__init_func = _internet_init,
//		.__destroy_func = _internet_destroy,
//
//		.__create_acc_func = _internet_create_acceptor,
//		.__destroy_acc_func = _internet_destroy_acceptor,
//
//		.__create_ses_func = _internet_create_session,
//		.__disconn_func = _internet_disconn,
//		.__run_func = _internet_run,
//
//	},
//
//	[NT_INTRANET] = {
//		.__init_func = _intranet_init,
//		.__destroy_func = _intranet_destroy,
//
//		.__create_acc_func = _intranet_create_acceptor,
//		.__destroy_acc_func = _intranet_destroy_acceptor,
//
//		.__create_ses_func = _intranet_create_session,
//		.__disconn_func = _intranet_disconn,
//		.__run_func = _intranet_run,
//
//	},
//};


static inline struct _inet_impl* _conv_inet_impl(struct net_struct* inet)
{
	return (struct _inet_impl*)((unsigned long)inet - (unsigned long)&((struct _inet_impl*)(0))->_the_net);
}

static inline struct _acc_impl* _conv_acc_impl(struct acceptor* acc)
{
	return (struct _acc_impl*)((unsigned long)acc - (unsigned long)&((struct _acc_impl*)(0))->_the_acc);
}

static inline struct _ses_impl* _conv_ses_impl(struct session* ses)
{
	return (struct _ses_impl*)((unsigned long)ses - (unsigned long)&((struct _ses_impl*)(0))->_the_ses);
}

static inline struct _acc_impl* _conv_acc_dln(struct dlnode* dln)
{
	return (struct _acc_impl*)((unsigned long)dln - (unsigned long)&((struct _acc_impl*)(0))->_lst_node);
}

static inline struct _ses_impl* _conv_ses_dln(struct dlnode* dln)
{
	return (struct _ses_impl*)((unsigned long)dln - (unsigned long)&((struct _ses_impl*)(0))->_lst_node);
}

//static inline struct _poll_obj* _conv_poll_obj_rbn(struct rbnode* rbn)
//{
//	return (struct _poll_obj*)((unsigned long)rbn - (unsigned long)&((struct _poll_obj*)(0))->_rbn);
//}

static inline void _sei_free_buf(struct _ses_impl* sei)
{
	if(sei->_recv_buf)
		free(sei->_recv_buf);
	if(sei->_send_buf)
		free(sei->_send_buf);

	sei->_recv_buf = 0;
	sei->_send_buf = 0;
}

static inline int _sei_alloc_buf(struct _ses_impl* sei)
{
	struct _inet_impl* inet = sei->_inet;

	sei->_recv_buf = malloc(sei->_inet->_the_net.cfg.recv_buff_len);
	if(!sei->_recv_buf) goto error_ret;

	sei->_send_buf = malloc(sei->_inet->_the_net.cfg.send_buff_len);
	if(!sei->_send_buf) goto error_ret;

	return 0;
error_ret:
	_sei_free_buf(sei);
	return -1;
}

static void _sei_ctor(void* ptr)
{
	struct _ses_impl* sei = (struct _ses_impl*)ptr;

	sei->_sock_fd = 0;
	sei->_inet = 0;
	sei->_the_ses.remote_ip = 0;
	sei->_the_ses.remote_port = 0;
	sei->_the_ses.usr_ptr = 0;

	lst_clr(&sei->_lst_node);

	sei->_ops.func_conn = 0;
	sei->_ops.func_recv = 0;
	sei->_ops.func_disconn = 0;

	sei->_recv_buf_len = 0;
	sei->_bytes_recv = 0;

	sei->_send_buf_len = 0;

	sei->_po_idx = 0;
	sei->_state = _SES_INVALID;
	sei->_type_info = SES_TYPE_INFO;

	sei->_r_offset = 0;
	sei->_w_offset = 0;
}

static void _sei_dtor(void* ptr)
{
	struct _ses_impl* sei = (struct _ses_impl*)ptr;
	_sei_free_buf(sei);
}

//static void _poll_obj_ctor(void* ptr)
//{
//	struct _poll_obj* obj = (struct _poll_obj*)ptr;
//
//	obj->_obj = 0;
//	rb_fillnew(&obj->_rbn);
//}

static inline long _write_send_buf(struct _ses_impl* sei, const char* data, int datalen)
{
	int r_offset, w_offset;
	int remain;

	r_offset = sei->_r_offset;
	w_offset = sei->_w_offset;

	if(w_offset < r_offset)
	{
		remain = r_offset - w_offset;
		if(remain < datalen) goto error_ret;

		memcpy(sei->_send_buf + w_offset, data, datalen);
		w_offset += datalen;

		goto succ_ret;
	}
	else if(w_offset + datalen < r_offset + sei->_send_buf_len)
	{
		remain = sei->_send_buf_len - w_offset;
		if(remain >= datalen)
		{
			memcpy(sei->_send_buf + w_offset, data, datalen);
			w_offset += datalen;
		}
		else
		{
			int remain2 = datalen - remain;
			memcpy(sei->_send_buf + w_offset, data, remain);
			memcpy(sei->_send_buf, data + remain, remain2);
			w_offset = remain2;
		}

		goto succ_ret;
	}

	goto error_ret;

succ_ret:
	sei->_w_offset = w_offset;
	return 0;
error_ret:
	return -1;
}

static inline unsigned int _max_fd_count(struct _inet_impl* inet)
{
	return inet->_the_net.cfg.nr_acceptor + inet->_the_net.cfg.nr_session;
}

static struct _inet_impl* _net_create(const struct net_config* cfg, const struct net_ops* ops/*, struct _nt_ops* handler*/)
{
	long rslt;
	struct _inet_impl* inet;
	unsigned long pool_size;
	void* pool_acc;
	void* pool_ses;

	inet = (struct _inet_impl*)malloc(sizeof(struct _inet_impl));
	if(!inet) goto error_ret;

	lst_new(&inet->_acc_list);
	lst_new(&inet->_ses_list);

	inet->_the_net.cfg.send_buff_len = cfg->send_buff_len;
	inet->_the_net.cfg.recv_buff_len = cfg->recv_buff_len;

	inet->_the_net.cfg.nr_acceptor = cfg->nr_acceptor;
	inet->_the_net.cfg.nr_session = cfg->nr_session;

	inet->_the_net.ops.func_acc = ops->func_acc;
	inet->_the_net.ops.func_conn = ops->func_conn;
	inet->_the_net.ops.func_recv = ops->func_recv;
	inet->_the_net.ops.func_disconn = ops->func_disconn;

//	inet->_handler = handler;

	pool_size = objpool_mem_usage(cfg->nr_acceptor, sizeof(struct _acc_impl));
	pool_acc = malloc(pool_size);
	err_exit(!pool_acc, "malloc acc pool failed.");
	inet->_acc_pool = objpool_create(pool_acc, pool_size, sizeof(struct _acc_impl), 0, 0);
	err_exit(!inet->_acc_pool, "malloc acc pool failed.");

	pool_size = objpool_mem_usage(cfg->nr_session, sizeof(struct _ses_impl));
	pool_ses = malloc(pool_size);
	err_exit(!pool_ses, "malloc ses pool failed.");
	inet->_ses_pool = objpool_create(pool_ses, pool_size, sizeof(struct _ses_impl), _sei_ctor, _sei_dtor);
	err_exit(!inet->_ses_pool, "malloc ses pool failed.");

	return inet;
error_ret:
	if(inet->_acc_pool)
		objpool_destroy(inet->_acc_pool);
	if(inet->_ses_pool)
		objpool_destroy(inet->_ses_pool);

	if(pool_acc)
		free(pool_acc);
	if(pool_ses)
		free(pool_ses);
	if(inet)
		free(inet);
	return 0;
}

static long _internet_init(struct _inet_impl* inet)
{
	unsigned int nr_fd = _max_fd_count(inet);

	inet->_ep_ev = (struct epoll_event*)malloc(nr_fd * sizeof(struct epoll_event));
	if(!inet->_ep_ev) goto error_ret;

	inet->_epoll_fd = epoll_create(nr_fd);
	if(inet->_epoll_fd < 0) goto error_ret;

	return 0;
error_ret:
	if(inet->_epoll_fd > 0)
		close(inet->_epoll_fd);
	if(inet->_ep_ev)
		free(inet->_ep_ev);
	return -1;
}

//static long _intranet_init(struct _inet_impl* inet)
//{
//	rb_init(&inet->_po_rbt, 0);
//	inet->_po_cnt = 0;
//	return 0;
//}

static long _net_destroy(struct _inet_impl* inet)
{
	struct dlnode *dln, *rmv_dln;
	if(!inet) goto error_ret;

	dln = inet->_acc_list.head.next;

	while(dln != &inet->_acc_list.tail)
	{
		struct _acc_impl* aci = _conv_acc_dln(dln);
		rmv_dln = dln;
		dln = dln->next;

//		(*inet->_handler->__destroy_acc_func)(aci);
		_internet_destroy_acceptor(aci);
		lst_remove(&inet->_acc_list, rmv_dln);
	}

	dln = inet->_ses_list.head.next;

	while(dln != &inet->_ses_list.tail)
	{
		struct _ses_impl* sei = _conv_ses_dln(dln);
		rmv_dln = dln;
		dln = dln->next;

		_net_close(sei);

		lst_remove(&inet->_acc_list, rmv_dln);
	}

	free(inet);

	return 0;
error_ret:
	return -1;

}

static long _internet_destroy(struct _inet_impl* inet)
{
	long rslt;
	struct dlnode *dln, *rmv_dln;

	close(inet->_epoll_fd);
	free(inet->_ep_ev);

	rslt = _net_destroy(inet);
	if(rslt < 0) goto error_ret;

	return 0;
error_ret:
	return -1;
}

//static long _intranet_destroy(struct _inet_impl* inet)
//{
//	long rslt;
//	struct dlnode *dln, *rmv_dln;
//
//	rslt = _net_destroy(inet);
//	if(rslt < 0) goto error_ret;
//
//	return 0;
//error_ret:
//	return -1;
//}

static struct _acc_impl* _net_create_acc(struct _inet_impl* inet, unsigned int ip, unsigned short port)
{
	long rslt;
	int sock_opt;
	struct _acc_impl* aci;
	struct sockaddr_in addr;

	aci = (struct _acc_impl*)objpool_alloc(inet->_acc_pool);
	if(!aci) goto error_ret;

	aci->_sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	if(aci->_sock_fd < 0) goto error_ret;

	sock_opt = 1;
	rslt = setsockopt(aci->_sock_fd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(int));
	rslt = setsockopt(aci->_sock_fd, SOL_SOCKET, SO_RCVBUF, &inet->_the_net.cfg.recv_buff_len, sizeof(int));
	rslt = setsockopt(aci->_sock_fd, SOL_SOCKET, SO_SNDBUF, &inet->_the_net.cfg.send_buff_len, sizeof(int));

	ip = htonl(ip);

	memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_addr = *(struct in_addr*)&ip;
	addr.sin_port = htons(port);

	rslt = bind(aci->_sock_fd, (struct sockaddr*)&addr, sizeof(addr));
	if(rslt < 0) goto error_ret;

	rslt = listen(aci->_sock_fd, NET_BACKLOG_COUNT);
	if(rslt < 0) goto error_ret;

	aci->_type_info = ACC_TYPE_INFO;

	aci->_inet = inet;

	lst_clr(&aci->_lst_node);
	lst_push_back(&inet->_acc_list, &aci->_lst_node);

	return aci;
error_ret:
	if(aci)
		_net_destroy_acc(aci);
	return 0;

}

static struct _acc_impl* _internet_create_acceptor(struct _inet_impl* inet, unsigned int ip, unsigned short port)
{
	long rslt;
	struct _acc_impl* aci;
	struct epoll_event ev;

	aci = _net_create_acc(inet, ip, port);
	if(!aci) goto error_ret;

	ev.events = EPOLLIN;
	ev.data.ptr = aci;

	rslt = epoll_ctl(inet->_epoll_fd, EPOLL_CTL_ADD, aci->_sock_fd, &ev);
	if(rslt < 0) goto error_ret;

	return aci;
error_ret:
	_internet_destroy_acceptor(aci);
	return 0;
}

//static struct _acc_impl* _intranet_create_acceptor(struct _inet_impl* inet, unsigned int ip, unsigned short port)
//{
//	long rslt;
//	struct _acc_impl* aci;
//	struct _poll_obj* poll_obj;
//
//	aci = _net_create_acc(inet, ip, port);
//	err_exit(!aci, "intranet: net create acc failed.");
//
////	poll_obj = mm_cache_alloc(__the_poll_obj_zone);
//	poll_obj = malloc(sizeof(struct _poll_obj));
//	err_exit(!poll_obj, "intranet: create poll obj failed.");
//
//	aci->_obj = poll_obj;
//	poll_obj->_obj = aci;
//	poll_obj->_rbn.key = (void*)(long)aci->_sock_fd;
//
//	rslt = rb_insert(&inet->_po_rbt, &poll_obj->_rbn);
//	err_exit(rslt < 0, "intranet: insert poll obj failed");
//
//	++inet->_po_cnt;
//
//	return aci;
//error_ret:
//	if(poll_obj)
//		free(poll_obj);
////		mm_cache_free(__the_poll_obj_zone, poll_obj);
//
//	_intranet_destroy_acceptor(aci);
//	return 0;
//}

static long _net_destroy_acc(struct _acc_impl* aci)
{
	long rslt;
	struct _inet_impl* inet = aci->_inet;

	close(aci->_sock_fd);
	aci->_type_info = 0;

	objpool_free(inet->_acc_pool, aci);
	return 0;
error_ret:
	return -1;
}


static long _internet_destroy_acceptor(struct _acc_impl* aci)
{
	long rslt;
	struct _inet_impl* inet = aci->_inet;

	rslt = epoll_ctl(inet->_epoll_fd, EPOLL_CTL_DEL, aci->_sock_fd, 0);
	if(rslt < 0) goto error_ret;

	return _net_destroy_acc(aci);
error_ret:
	return -1;
}

//static long _intranet_destroy_acceptor(struct _acc_impl* aci)
//{
//	long rslt;
//	struct _inet_impl* inet = aci->_inet;
//	struct rbnode* rbn;
//
//	rbn = rb_remove(&inet->_po_rbt, &aci->_obj->_rbn);
//	err_exit(!rbn, "intranet destoy acc failed: remove rb node");
//
////	mm_cache_free(__the_poll_obj_zone, aci->_obj);
//	free(aci->_obj);
//	aci->_obj = 0;
//
//	return _net_destroy_acc(aci);
//error_ret:
//	return -1;
//}

static struct _ses_impl* _net_create_session(struct _inet_impl* inet, int socket_fd)
{
	long rslt;
	int sock_opt;
	struct _ses_impl* sei = 0;
	struct timeval to;

//	sei = mm_cache_alloc(__the_ses_zone);
	sei = (struct _ses_impl*)objpool_alloc(inet->_ses_pool);
	err_exit(!sei, "_net_create_session alloc session error.");

	sei->_sock_fd = socket_fd;
	sei->_inet = inet;

	sock_opt = 1;
	rslt = setsockopt(sei->_sock_fd, SOL_SOCKET, SO_KEEPALIVE, &sock_opt, sizeof(int));
	rslt = setsockopt(sei->_sock_fd, SOL_SOCKET, SO_RCVBUF, &inet->_the_net.cfg.recv_buff_len, sizeof(int));
	rslt = setsockopt(sei->_sock_fd, SOL_SOCKET, SO_SNDBUF, &inet->_the_net.cfg.send_buff_len, sizeof(int));

	to.tv_sec = 8;
	to.tv_usec = 0;

	rslt = setsockopt(sei->_sock_fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(struct timeval));
	rslt = setsockopt(sei->_sock_fd, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof(struct timeval));

	sei->_recv_buf_len = inet->_the_net.cfg.recv_buff_len;
	sei->_send_buf_len = inet->_the_net.cfg.send_buff_len;

	rslt = _sei_alloc_buf(sei);
	err_exit(rslt < 0, "_net_create_session alloc buff failed.");

	sei->_state = _SES_ESTABLISHING;
	lst_push_back(&inet->_ses_list, &sei->_lst_node);

	return sei;
error_ret:
	if(sei)
		_net_close(sei);
	return 0;
}

static struct _ses_impl* _internet_create_session(struct _inet_impl* inet, int socket_fd)
{
	long rslt;
	struct _ses_impl* sei;
	struct epoll_event ev;

	sei = _net_create_session(inet, socket_fd);
	err_exit(!sei, "internet create session error.");

	ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
	ev.data.ptr = sei;

	rslt = epoll_ctl(inet->_epoll_fd, EPOLL_CTL_ADD, sei->_sock_fd, &ev);
	err_exit(rslt < 0, "internet epoll_ctl error.");

	return sei;
error_ret:
	if(sei)
		_net_close(sei);
	return 0;
}

//static struct _ses_impl* _intranet_create_session(struct _inet_impl* inet, int socket_fd)
//{
//	long rslt;
//	struct _ses_impl* sei;
//	struct _poll_obj* poll_obj;
//
//	sei = _net_create_session(inet, socket_fd);
//	err_exit(!sei, "intranet create session error.");
//
////	poll_obj = mm_cache_alloc(__the_poll_obj_zone);
//	poll_obj = malloc(sizeof(struct _poll_obj));
//	err_exit(!poll_obj, "intranet: create session  poll obj failed.");
//
//	sei->_obj = poll_obj;
//	poll_obj->_obj = sei;
//	poll_obj->_rbn.key = (void*)(long)sei->_sock_fd;
//
//	rslt = rb_insert(&inet->_po_rbt, &poll_obj->_rbn);
//	err_exit(rslt < 0, "intranet: insert session poll obj failed");
//
//	++inet->_po_cnt;
//
//	return sei;
//error_ret:
//	if(poll_obj)
//	{
//		struct rbnode* hot;
//		struct rbnode* rbn = rb_search(&inet->_po_rbt, (void*)(long)socket_fd, &hot);
//		struct _ses_impl* si = (struct _ses_impl*)((struct _poll_obj*)_conv_poll_obj_rbn(rbn))->_obj;
//		printf("si state: %d\n", si->_state);
//
//		free(poll_obj);
////		mm_cache_free(__the_poll_obj_zone, poll_obj);
//	}
//	if(sei)
//		_net_close(sei);
//	return 0;
//}

struct net_struct* net_create(const struct net_config* cfg, const struct net_ops* ops)
{
	long rslt;
	struct _inet_impl* inet;

	if(!cfg || !ops) goto error_ret;

	inet = _net_create(cfg, ops);
	err_exit(!inet, "create net error.");

//	rslt = (*inet->_handler->__init_func)(inet);
	rslt = _internet_init(inet);
	err_exit(rslt < 0, "init net error.");

	signal(SIGPIPE, SIG_IGN);

	return &inet->_the_net;
error_ret:
	return 0;
}

long net_destroy(struct net_struct* net)
{
	struct _inet_impl* inet;
	if(!net) goto error_ret;

	inet = _conv_inet_impl(net);

//	return (*inet->_handler->__destroy_func)(inet);
	return _internet_destroy(inet);
error_ret:
	return -1;
}

struct acceptor* net_create_acceptor(struct net_struct* net, unsigned int ip, unsigned short port)
{
	struct _acc_impl* aci;
	struct _inet_impl* inet;

	if(!net) goto error_ret;
	inet = _conv_inet_impl(net);

//	aci = (*inet->_handler->__create_acc_func)(inet, ip, port);
	aci = _internet_create_acceptor(inet, ip, port);
	err_exit(!aci, "create acceptor error.");

	return &aci->_the_acc;
error_ret:
	return 0;
}

long net_destroy_acceptor(struct acceptor* acc)
{
	struct _acc_impl* aci;
	struct _inet_impl* inet;

	if(!acc) goto error_ret;

	aci = _conv_acc_impl(acc);
	inet = aci->_inet;

//	return (*inet->_handler->__destroy_acc_func)(aci);
	return _internet_destroy_acceptor(aci);
error_ret:
	return -1;
}

static long _net_on_acc(struct _acc_impl* aci)
{
	long rslt;
	int new_sock = 0;
	socklen_t addr_len = 0;
	struct sockaddr_in remote_addr;
	struct _ses_impl* sei = 0;
	struct _inet_impl* inet = aci->_inet;

	new_sock = accept4(aci->_sock_fd, (struct sockaddr*)&remote_addr, &addr_len, 0);
	if(new_sock < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
		goto do_nothing_ret;

	err_exit(new_sock < 0, "accept error: (%d:%s)", errno, strerror(errno));

//	sei = (*inet->_handler->__create_ses_func)(inet, new_sock);
	sei = _internet_create_session(inet, new_sock);
	err_exit(!sei, "accept: create session error.");

	sei->_debug_type = 1;

	sei->_the_ses.remote_ip = ntohl(*(unsigned int*)&remote_addr.sin_addr);
	sei->_the_ses.remote_port = ntohs(remote_addr.sin_port);

	if(inet->_the_net.ops.func_acc)
		(*inet->_the_net.ops.func_acc)(&aci->_the_acc, &sei->_the_ses);

do_nothing_ret:
	return 0;
error_ret:
	if(new_sock > 0)
		close(new_sock);
	return -1;
}

static long _net_close(struct _ses_impl* sei)
{
	long rslt;
	struct dlnode* dln;
	struct _inet_impl* inet = sei->_inet;

//	_sei_free_buf(sei);

	if(sei->_state != _SES_INVALID && sei->_state != _SES_CLOSED)
	{
		on_disconn_func df = sei->_ops.func_disconn ? sei->_ops.func_disconn : inet->_the_net.ops.func_disconn;
		if(df) (*df)(&sei->_the_ses);
	}

	rslt = lst_remove(&sei->_inet->_ses_list, &sei->_lst_node);
	err_exit(rslt < 0, "_net_close error.");

	if(sei->_sock_fd != 0)
	{
//		if(inet->_handler->__disconn_func)
//			(*inet->_handler->__disconn_func)(sei);
		_internet_disconn(sei);

		close(sei->_sock_fd);
		sei->_sock_fd = 0;
	}

	sei->_state = _SES_CLOSED;

	objpool_free(inet->_ses_pool, sei);

//	free(sei);
	return 0;
//	return mm_cache_free(__the_ses_zone, sei);
error_ret:
	return -1;
}

static void _net_on_recv(struct _ses_impl* sei)
{
	long rslt;
	int recv_len = 0;

	char* p = sei->_recv_buf;
	sei->_bytes_recv = 0;
	on_recv_func rf;
	struct _inet_impl* inet = sei->_inet;

	err_exit(!sei->_recv_buf || sei->_recv_buf_len <= 0, "_net_on_recv(%d): recv buffer error <%p>", sei->_debug_type, sei);

	rf = sei->_ops.func_recv ? sei->_ops.func_recv : inet->_the_net.ops.func_recv;

	do
	{
		recv_len = recv(sei->_sock_fd, p, sei->_recv_buf_len - sei->_bytes_recv, MSG_DONTWAIT);
		if(recv_len <= 0) break;

		p += recv_len;
		sei->_bytes_recv += recv_len;

		if(sei->_bytes_recv >= sei->_recv_buf_len)
		{
			if(rf)
				(*rf)(&sei->_the_ses, sei->_recv_buf, sei->_bytes_recv);

			p = sei->_recv_buf;
			sei->_bytes_recv = 0;
		}
	}
	while(recv_len > 0);

	if(recv_len <= 0)
	{
		err_exit(errno != EWOULDBLOCK && errno != EAGAIN, "_net_on_recv(%d) [%d : %s], <%p>",
				sei->_debug_type, errno, strerror(errno), sei);

		if(rf && sei->_bytes_recv > 0)
			(*rf)(&sei->_the_ses, sei->_recv_buf, sei->_bytes_recv);
	}

succ_ret:
	return;
error_ret:
	_net_close(sei);
	return;
}

static inline void _net_on_error(struct _ses_impl* sei)
{
	if(sei->_sock_fd == 0)
		return;

	_net_close(sei);
}

static long _net_try_send_all(struct _ses_impl* sei)
{
	int cnt = 0;
	int remain;
	struct _inet_impl* inet = sei->_inet;

	if(sei->_r_offset <= sei->_w_offset)
	{
send_from_start:
		remain = sei->_w_offset - sei->_r_offset;
		if(remain > 0)
		{
			cnt = send(sei->_sock_fd, sei->_send_buf + sei->_r_offset, remain, MSG_DONTWAIT);
			if(cnt <= 0) goto send_finish;

			sei->_r_offset += cnt;
		}
	}
	else
	{
		remain = sei->_send_buf_len - sei->_r_offset;

		while(remain > 0)
		{
			cnt = send(sei->_sock_fd, sei->_send_buf + sei->_r_offset, remain, MSG_DONTWAIT);
			if(cnt <= 0) goto send_finish;

			sei->_r_offset += cnt;
			remain = sei->_send_buf_len - sei->_r_offset;
		}

		sei->_r_offset = 0;

		goto send_from_start;
	}

	if(sei->_state == _SES_CLOSING)
	{
		if(sei->_r_offset >= sei->_w_offset)
			_net_close(sei);
	}
	else
	{
send_finish:
		if(cnt < 0)
		{
			err_exit(errno != EWOULDBLOCK && errno != EAGAIN, "send error [%d : %s] <%p>", errno, strerror(errno), sei);

		}
	}

send_succ:
	return 0;
error_ret:
	_net_close(sei);
	return -1;
}

static inline void _net_on_send(struct _ses_impl* sei)
{
	struct _inet_impl* inet = sei->_inet;

	if(sei->_state == _SES_ESTABLISHING)
	{
		on_conn_func cf;
		cf = sei->_ops.func_conn ? sei->_ops.func_conn : inet->_the_net.ops.func_conn;

		sei->_state = _SES_NORMAL;

		if(cf)
			(*cf)(&sei->_the_ses);

	}
	_net_try_send_all(sei);

error_ret:
	return;
}

long net_send(struct session* ses, const char* data, int data_len)
{
	long rslt;
	struct _ses_impl* sei;
	char* cur_send_p;

	err_exit(!ses || !data || data_len <= 0, "net_send: invalid args.");

	sei = _conv_ses_impl(ses);

	err_exit(sei->_state != _SES_NORMAL, "net_send: session state error : %d <%p>", sei->_state, sei);

	rslt = _write_send_buf(sei, data, data_len);
	err_exit(rslt < 0, "net_send: send buff full.");

	return _net_try_send_all(sei);
error_ret:
	return -1;
}

static long _internet_disconn(struct _ses_impl* sei)
{
	long rslt = epoll_ctl(sei->_inet->_epoll_fd, EPOLL_CTL_DEL, sei->_sock_fd, 0);
	err_exit(rslt < 0, "internet disconn epoll_ctl error[%d : %s], <%p>", errno, strerror(errno), sei);

	return 0;
error_ret:
	return -1;
}

//static long _intranet_disconn(struct _ses_impl* sei)
//{
//	long rslt;
//	struct _inet_impl* inet = sei->_inet;
//	struct rbnode* rbn;
//
//	rbn = rb_remove(&inet->_po_rbt, sei->_obj->_rbn.key);
//	err_exit(!rbn, "intranet disconn: remove rb failed");
//
////	mm_cache_free(__the_poll_obj_zone, sei->_obj);
//	free(sei->_obj);
//	sei->_obj = 0;
//
//	return 0;
//error_ret:
//	return -1;
//}

static inline long _net_disconn(struct _ses_impl* sei)
{
	long rslt;
	struct _inet_impl* inet = sei->_inet;

	err_exit(sei->_state != _SES_NORMAL && sei->_state != _SES_ESTABLISHING,
			"_net_disconn state error: %d, fd: %d.", sei->_state, sei->_sock_fd);

	shutdown(sei->_sock_fd, SHUT_RD);

	sei->_state = _SES_CLOSING;

	return _net_try_send_all(sei);
error_ret:
	return -1;
}

long net_disconnect(struct session* ses)
{
	long rslt;
	struct _ses_impl* sei;

	if(!ses) goto error_ret;
	sei = _conv_ses_impl(ses);

	return _net_disconn(sei);
error_ret:
	return -1;
}

long net_run(struct net_struct* net, int timeout)
{
	long rslt, cnt;
	struct _inet_impl* inet;
	struct _ses_impl* sei;

	if(!net) goto error_ret;
	inet = _conv_inet_impl(net);

//	return (*inet->_handler->__run_func)(inet, timeout);
	return _internet_run(inet, timeout);
error_ret:
	return -1;
}

static long _internet_run(struct _inet_impl* inet, int timeout)
{
	long rslt, cnt;
	struct _ses_impl* sei;

	unsigned int nr_fd = _max_fd_count(inet);

	cnt = epoll_wait(inet->_epoll_fd, inet->_ep_ev, nr_fd, timeout);
	if(cnt < 0) goto error_ret;

	for(long i = 0; i < cnt; ++i)
	{
		unsigned int type_info = *(unsigned int*)(inet->_ep_ev[i].data.ptr);
		if(type_info == ACC_TYPE_INFO)
		{
			_net_on_acc((struct _acc_impl*)inet->_ep_ev[i].data.ptr);
		}
		else if(type_info == SES_TYPE_INFO)
		{
			sei = (struct _ses_impl*)inet->_ep_ev[i].data.ptr;

			if(inet->_ep_ev[i].events & EPOLLOUT)
				_net_on_send(sei);
			if(inet->_ep_ev[i].events & EPOLLIN)
				_net_on_recv(sei);
			if(inet->_ep_ev[i].events & EPOLLERR)
				_net_on_error(sei);
		}
	}

	return 0;
error_ret:
	return -1;

}

//static long _intranet_run(struct _inet_impl* inet, int timeout)
//{
//	long rslt, cnt = 0;
//	struct pollfd pofds[inet->_po_cnt];
//	struct dlnode* dln;
//	struct _acc_impl* aci;
//	struct _ses_impl* sei;
//	struct rbnode* hot, *rbn;
//	struct _poll_obj* poll_obj;
//
//	dln = inet->_acc_list.head.next;
//	while(dln != &inet->_acc_list.tail)
//	{
//		aci = _conv_acc_dln(dln);
//
//		pofds[cnt].fd = aci->_sock_fd;
//		pofds[cnt].events = POLLIN;
//		pofds[cnt].revents = 0;
//
//		++cnt;
//
//		dln = dln->next;
//	}
//
//	dln = inet->_ses_list.head.next;
//	while(dln != &inet->_ses_list.tail)
//	{
//		sei = _conv_ses_dln(dln);
//
//		pofds[cnt].fd = sei->_sock_fd;
//		pofds[cnt].events = POLLIN | POLLOUT | POLLRDHUP | POLLERR;
//		pofds[cnt].revents = 0;
//
//		++cnt;
//
//		dln = dln->next;
//	}
//
//	err_exit(cnt > inet->_po_cnt, "intranet run: overflow.");
//
//	cnt = poll(pofds, cnt, timeout);
//	if(cnt < 0) goto error_ret;
//
//	for(int i = 0; i < cnt; ++i)
//	{
//		unsigned int type_info;
//
//		rbn = rb_search(&inet->_po_rbt, (void*)(long)pofds[i].fd, &hot);
//		err_exit(!rbn, "intranet run: can not find fd: %d.", pofds[i].fd);
//
//		poll_obj = _conv_poll_obj_rbn(rbn);
//
//		type_info = *(unsigned int*)(poll_obj->_obj);
//
//		if(type_info == ACC_TYPE_INFO)
//		{
//			_net_on_acc((struct _acc_impl*)poll_obj->_obj);
//		}
//		else if(type_info == SES_TYPE_INFO)
//		{
//			struct _ses_impl* sei = (struct _ses_impl*)poll_obj->_obj;
//
//			if(pofds[i].revents & POLLOUT)
//				_net_on_send(sei);
//			if(pofds[i].revents & POLLIN)
//				_net_on_recv(sei);
//			if(pofds[i].revents & POLLERR)
//				_net_on_error(sei);
//		}
//
//	}
//
//	return 0;
//error_ret:
//	return -1;
//}

struct session* net_connect(struct net_struct* net, unsigned int ip, unsigned short port)
{
	long rslt;
	int new_sock, sock_opt;

	struct _inet_impl* inet;
	struct _ses_impl* sei = 0;
	struct sockaddr_in addr;

	if(!net) goto error_ret;

	inet = _conv_inet_impl(net);

	new_sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	if(new_sock < 0) goto error_ret;

//	sei = (*inet->_handler->__create_ses_func)(inet, new_sock);
	sei = _internet_create_session(inet, new_sock);
	if(!sei) goto error_ret;

	sei->_debug_type = 2;

	sock_opt = 1;
	rslt = setsockopt(sei->_sock_fd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(int));

	addr.sin_family = AF_INET;
	addr.sin_addr = *(struct in_addr*)&ip;
	addr.sin_port = htons(port);

	rslt = connect(sei->_sock_fd, (struct sockaddr*)&addr, sizeof(addr));
	if(rslt < 0 && errno != EINPROGRESS) goto error_ret;

	return &sei->_the_ses;

error_ret:
	perror(strerror(errno));
	if(sei)
		_net_close(sei);
	return 0;
}

inline long net_bind_session_ops(struct session* ses, const struct session_ops* ops)
{
	struct _ses_impl* sei;

	if(!ses) goto error_ret;
	sei = _conv_ses_impl(ses);

	if(ops)
	{
		sei->_ops.func_conn = ops->func_conn;
		sei->_ops.func_recv = ops->func_recv;
		sei->_ops.func_disconn = ops->func_disconn;
	}
	else
	{
		sei->_ops.func_conn = 0;
		sei->_ops.func_recv = 0;
		sei->_ops.func_disconn = 0;
	}

	return 0;
error_ret:
	return -1;
}

inline long net_session_count(struct net_struct* net)
{
	struct _inet_impl* inet;
	if(!net) goto error_ret;

	inet = _conv_inet_impl(net);

	return 0;
//	return inet->_ses_list.size;
error_ret:
	return -1;
}

