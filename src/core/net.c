#include "common_types.h"
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

struct _inet_impl
{
	struct net_struct _the_net;
	struct dlist _acc_list;
	struct dlist _ses_list;
	struct objpool* _ses_pool;
	struct objpool* _acc_pool;

	struct
	{
		struct epoll_event* _ep_ev;
		i32 _epoll_fd;
	};
};

struct acceptor
{
	u32 _type_info;
	i32 _sock_fd;
	u32 local_ip;
	u32 local_port;
	struct _inet_impl* _inet;
	struct dlnode _lst_node;
};

struct session 
{
	u32 _type_info;
	i32 _sock_fd;
	u32 remote_ip;
	u32 remote_port;
	void* usr_ptr;

	struct _inet_impl* _inet;
	struct dlnode _lst_node;
	struct session_ops _ops;

	char* _recv_buf;
	char* _send_buf;

	u32 _bytes_recv;
	u32 _recv_buf_len;

	u32 _send_buf_len;
	i32 _r_offset;
	i32 _w_offset;

	i32 _po_idx;
	i32 _state;
	i32 _debug_type;
};

static struct linger __linger_option =
{
	.l_onoff = 1,
	.l_linger = 4,
};

static i32 _internet_init(struct _inet_impl* inet);
static i32 _internet_destroy(struct _inet_impl* inet);

static struct acceptor* _internet_create_acceptor(struct _inet_impl* inet, u32 ip, u16 port);
static i32 _internet_destroy_acceptor(struct acceptor* aci);

static struct session* _internet_create_session(struct _inet_impl* inet, i32 socket_fd);

static i32 _internet_disconn(struct session* sei);

static i32 _internet_run(struct _inet_impl* inet, i32 timeout);

static i32 _internet_on_acc(struct acceptor* aci);
static i32 _net_close(struct session* sei);
static void _net_on_recv(struct session* sei);

static void _net_on_error(struct session* sei);
static i32 _net_try_send_all(struct session* sei);
static void _net_on_send(struct session* sei);
static i32 _net_disconn(struct session* sei);
static i32 _net_destroy_acc(struct acceptor* aci);

static inline struct _inet_impl* _conv_inet_impl(struct net_struct* inet)
{
	return (struct _inet_impl*)((u64)inet - (u64)&((struct _inet_impl*)(0))->_the_net);
}

static inline struct acceptor* _conv_acc_dln(struct dlnode* dln)
{
	return (struct acceptor*)((u64)dln - (u64)&((struct acceptor*)(0))->_lst_node);
}

static inline struct session* _conv_ses_dln(struct dlnode* dln)
{
	return (struct session*)((u64)dln - (u64)&((struct session*)(0))->_lst_node);
}

static inline void _sei_free_buf(struct session* sei)
{
	if(sei->_recv_buf)
		free(sei->_recv_buf);
	if(sei->_send_buf)
		free(sei->_send_buf);

	sei->_recv_buf = 0;
	sei->_send_buf = 0;
}

static inline i32 _sei_alloc_buf(struct session* sei)
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
	struct session* sei = (struct session*)ptr;

	sei->_sock_fd = 0;
	sei->_inet = 0;
	sei->remote_ip = 0;
	sei->remote_port = 0;
	sei->usr_ptr = 0;

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
	struct session* sei = (struct session*)ptr;
	_sei_free_buf(sei);
}

static inline i32 _write_send_buf(struct session* sei, const char* data, i32 datalen)
{
	i32 r_offset, w_offset;
	i32 remain;

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
			i32 remain2 = datalen - remain;
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

static inline u32 _max_fd_count(struct _inet_impl* inet)
{
	return inet->_the_net.cfg.nr_acceptor + inet->_the_net.cfg.nr_session;
}

static struct _inet_impl* _net_create(const struct net_config* cfg, const struct net_ops* ops/*, struct _nt_ops* handler*/)
{
	i32 rslt;
	struct _inet_impl* inet;
	u64 pool_size;
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

	pool_size = objpool_mem_usage(cfg->nr_acceptor, sizeof(struct acceptor));
	pool_acc = malloc(pool_size);
	err_exit(!pool_acc, "malloc acc pool failed.");
	inet->_acc_pool = objpool_create(pool_acc, pool_size, sizeof(struct acceptor), 0, 0);
	err_exit(!inet->_acc_pool, "malloc acc pool failed.");

	pool_size = objpool_mem_usage(cfg->nr_session, sizeof(struct session));
	pool_ses = malloc(pool_size);
	err_exit(!pool_ses, "malloc ses pool failed.");
	inet->_ses_pool = objpool_create(pool_ses, pool_size, sizeof(struct session), _sei_ctor, _sei_dtor);
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

static i32 _internet_init(struct _inet_impl* inet)
{
	u32 nr_fd = _max_fd_count(inet);

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

static i32 _net_destroy(struct _inet_impl* inet)
{
	struct dlnode *dln, *rmv_dln;
	if(!inet) goto error_ret;

	dln = inet->_acc_list.head.next;

	while(dln != &inet->_acc_list.tail)
	{
		struct acceptor* aci = _conv_acc_dln(dln);
		rmv_dln = dln;
		dln = dln->next;

		_internet_destroy_acceptor(aci);
		lst_remove(&inet->_acc_list, rmv_dln);
	}

	dln = inet->_ses_list.head.next;

	while(dln != &inet->_ses_list.tail)
	{
		struct session* sei = _conv_ses_dln(dln);
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

static i32 _internet_destroy(struct _inet_impl* inet)
{
	i32 rslt;
	struct dlnode *dln, *rmv_dln;

	close(inet->_epoll_fd);
	free(inet->_ep_ev);

	rslt = _net_destroy(inet);
	if(rslt < 0) goto error_ret;

	return 0;
error_ret:
	return -1;
}

static struct acceptor* _net_create_acc(struct _inet_impl* inet, u32 ip, u16 port)
{
	i32 rslt;
	i32 sock_opt;
	struct acceptor* aci;
	struct sockaddr_in addr;

	aci = (struct acceptor*)objpool_alloc(inet->_acc_pool);
	if(!aci) goto error_ret;

	aci->_sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	if(aci->_sock_fd < 0) goto error_ret;

	sock_opt = 1;
	rslt = setsockopt(aci->_sock_fd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(i32));
	rslt = setsockopt(aci->_sock_fd, SOL_SOCKET, SO_RCVBUF, &inet->_the_net.cfg.recv_buff_len, sizeof(i32));
	rslt = setsockopt(aci->_sock_fd, SOL_SOCKET, SO_SNDBUF, &inet->_the_net.cfg.send_buff_len, sizeof(i32));

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

static struct acceptor* _internet_create_acceptor(struct _inet_impl* inet, u32 ip, u16 port)
{
	i32 rslt;
	struct acceptor* aci;
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

static i32 _net_destroy_acc(struct acceptor* aci)
{
	i32 rslt;
	struct _inet_impl* inet = aci->_inet;

	close(aci->_sock_fd);
	aci->_type_info = 0;

	objpool_free(inet->_acc_pool, aci);
	return 0;
error_ret:
	return -1;
}


static i32 _internet_destroy_acceptor(struct acceptor* aci)
{
	i32 rslt;
	struct _inet_impl* inet = aci->_inet;

	rslt = epoll_ctl(inet->_epoll_fd, EPOLL_CTL_DEL, aci->_sock_fd, 0);
	if(rslt < 0) goto error_ret;

	return _net_destroy_acc(aci);
error_ret:
	return -1;
}

static struct session* _net_create_session(struct _inet_impl* inet, i32 socket_fd)
{
	i32 rslt;
	i32 sock_opt;
	struct session* sei = 0;
	struct timeval to;

	sei = (struct session*)objpool_alloc(inet->_ses_pool);
	err_exit(!sei, "_net_create_session alloc session error.");

	sei->_sock_fd = socket_fd;
	sei->_inet = inet;

	sock_opt = 1;
	rslt = setsockopt(sei->_sock_fd, SOL_SOCKET, SO_KEEPALIVE, &sock_opt, sizeof(i32));
	rslt = setsockopt(sei->_sock_fd, SOL_SOCKET, SO_RCVBUF, &inet->_the_net.cfg.recv_buff_len, sizeof(i32));
	rslt = setsockopt(sei->_sock_fd, SOL_SOCKET, SO_SNDBUF, &inet->_the_net.cfg.send_buff_len, sizeof(i32));

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

static struct session* _internet_create_session(struct _inet_impl* inet, i32 socket_fd)
{
	i32 rslt;
	struct session* sei;
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

struct net_struct* net_create(const struct net_config* cfg, const struct net_ops* ops)
{
	i32 rslt;
	struct _inet_impl* inet;

	if(!cfg || !ops) goto error_ret;

	inet = _net_create(cfg, ops);
	err_exit(!inet, "create net error.");

	rslt = _internet_init(inet);
	err_exit(rslt < 0, "init net error.");

	signal(SIGPIPE, SIG_IGN);

	return &inet->_the_net;
error_ret:
	return 0;
}

i32 net_destroy(struct net_struct* net)
{
	struct _inet_impl* inet;
	if(!net) goto error_ret;

	inet = _conv_inet_impl(net);

	return _internet_destroy(inet);
error_ret:
	return -1;
}

struct acceptor* net_create_acceptor(struct net_struct* net, u32 ip, u16 port)
{
	struct acceptor* aci;
	struct _inet_impl* inet;

	if(!net) goto error_ret;
	inet = _conv_inet_impl(net);

	aci = _internet_create_acceptor(inet, ip, port);
	err_exit(!aci, "create acceptor error.");

	return aci;
error_ret:
	return 0;
}

i32 net_destroy_acceptor(struct acceptor* aci)
{
	struct _inet_impl* inet;

	if(!aci) goto error_ret;

	inet = aci->_inet;

	return _internet_destroy_acceptor(aci);
error_ret:
	return -1;
}

static i32 _net_on_acc(struct acceptor* aci)
{
	i32 rslt;
	i32 new_sock = 0;
	socklen_t addr_len = 0;
	struct sockaddr_in remote_addr;
	struct session* sei = 0;
	struct _inet_impl* inet = aci->_inet;

	new_sock = accept4(aci->_sock_fd, (struct sockaddr*)&remote_addr, &addr_len, 0);
	if(new_sock < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
		goto do_nothing_ret;

	err_exit(new_sock < 0, "accept error: (%d:%s)", errno, strerror(errno));

	sei = _internet_create_session(inet, new_sock);
	err_exit(!sei, "accept: create session error.");

	sei->_debug_type = 1;

	sei->remote_ip = ntohl(*(u32*)&remote_addr.sin_addr);
	sei->remote_port = ntohs(remote_addr.sin_port);

	if(inet->_the_net.ops.func_acc)
		(*inet->_the_net.ops.func_acc)(aci, sei);

do_nothing_ret:
	return 0;
error_ret:
	if(new_sock > 0)
		close(new_sock);
	return -1;
}

static i32 _net_close(struct session* sei)
{
	i32 rslt;
	struct dlnode* dln;
	struct _inet_impl* inet = sei->_inet;

	if(sei->_state != _SES_INVALID && sei->_state != _SES_CLOSED)
	{
		on_disconn_func df = sei->_ops.func_disconn ? sei->_ops.func_disconn : inet->_the_net.ops.func_disconn;
		if(df) (*df)(sei);
	}

	rslt = lst_remove(&sei->_inet->_ses_list, &sei->_lst_node);
	err_exit(rslt < 0, "_net_close error.");

	if(sei->_sock_fd != 0)
	{
		_internet_disconn(sei);

		close(sei->_sock_fd);
		sei->_sock_fd = 0;
	}

	sei->_state = _SES_CLOSED;

	objpool_free(inet->_ses_pool, sei);

	return 0;
error_ret:
	return -1;
}

static void _net_on_recv(struct session* sei)
{
	i32 rslt;
	i32 recv_len = 0;

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
				(*rf)(sei, sei->_recv_buf, sei->_bytes_recv);

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
			(*rf)(sei, sei->_recv_buf, sei->_bytes_recv);
	}

succ_ret:
	return;
error_ret:
	_net_close(sei);
	return;
}

static inline void _net_on_error(struct session* sei)
{
	if(sei->_sock_fd == 0)
		return;

	_net_close(sei);
}

static i32 _net_try_send_all(struct session* sei)
{
	i32 cnt = 0;
	i32 remain;
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

static inline void _net_on_send(struct session* sei)
{
	struct _inet_impl* inet = sei->_inet;

	if(sei->_state == _SES_ESTABLISHING)
	{
		on_conn_func cf;
		cf = sei->_ops.func_conn ? sei->_ops.func_conn : inet->_the_net.ops.func_conn;

		sei->_state = _SES_NORMAL;

		if(cf)
			(*cf)(sei);

	}
	_net_try_send_all(sei);

error_ret:
	return;
}

i32 net_send(struct session* sei, const char* data, i32 data_len)
{
	i32 rslt;
	char* cur_send_p;

	err_exit(!sei || !data || data_len <= 0, "net_send: invalid args.");
	err_exit(sei->_state != _SES_NORMAL, "net_send: session state error : %d <%p>", sei->_state, sei);

	rslt = _write_send_buf(sei, data, data_len);
	err_exit(rslt < 0, "net_send: send buff full.");

	return _net_try_send_all(sei);
error_ret:
	return -1;
}

static i32 _internet_disconn(struct session* sei)
{
	i32 rslt = epoll_ctl(sei->_inet->_epoll_fd, EPOLL_CTL_DEL, sei->_sock_fd, 0);
	err_exit(rslt < 0, "internet disconn epoll_ctl error[%d : %s], <%p>", errno, strerror(errno), sei);

	return 0;
error_ret:
	return -1;
}

static inline i32 _net_disconn(struct session* sei)
{
	i32 rslt;
	struct _inet_impl* inet = sei->_inet;

	err_exit(sei->_state != _SES_NORMAL && sei->_state != _SES_ESTABLISHING,
			"_net_disconn state error: %d, fd: %d.", sei->_state, sei->_sock_fd);

	shutdown(sei->_sock_fd, SHUT_RD);

	sei->_state = _SES_CLOSING;

	return _net_try_send_all(sei);
error_ret:
	return -1;
}

i32 net_disconnect(struct session* sei)
{
	i32 rslt;
	if(!sei) goto error_ret;

	return _net_disconn(sei);
error_ret:
	return -1;
}

i32 net_run(struct net_struct* net, i32 timeout)
{
	i32 rslt, cnt;
	struct _inet_impl* inet;
	struct session* sei;

	if(!net) goto error_ret;
	inet = _conv_inet_impl(net);

//	return (*inet->_handler->__run_func)(inet, timeout);
	return _internet_run(inet, timeout);
error_ret:
	return -1;
}

static i32 _internet_run(struct _inet_impl* inet, i32 timeout)
{
	i32 rslt, cnt;
	struct session* sei;

	u32 nr_fd = _max_fd_count(inet);

	cnt = epoll_wait(inet->_epoll_fd, inet->_ep_ev, nr_fd, timeout);
	if(cnt < 0) goto error_ret;

	for(i32 i = 0; i < cnt; ++i)
	{
		u32 type_info = *(u32*)(inet->_ep_ev[i].data.ptr);
		if(type_info == ACC_TYPE_INFO)
		{
			_net_on_acc((struct acceptor*)inet->_ep_ev[i].data.ptr);
		}
		else if(type_info == SES_TYPE_INFO)
		{
			sei = (struct session*)inet->_ep_ev[i].data.ptr;

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

struct session* net_connect(struct net_struct* net, u32 ip, u16 port)
{
	i32 rslt;
	i32 new_sock, sock_opt;

	struct _inet_impl* inet;
	struct session* sei = 0;
	struct sockaddr_in addr;

	if(!net) goto error_ret;

	inet = _conv_inet_impl(net);

	new_sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	if(new_sock < 0) goto error_ret;

	sei = _internet_create_session(inet, new_sock);
	if(!sei) goto error_ret;

	sei->_debug_type = 2;

	sock_opt = 1;
	rslt = setsockopt(sei->_sock_fd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(i32));

	addr.sin_family = AF_INET;
	addr.sin_addr = *(struct in_addr*)&ip;
	addr.sin_port = htons(port);

	rslt = connect(sei->_sock_fd, (struct sockaddr*)&addr, sizeof(addr));
	if(rslt < 0 && errno != EINPROGRESS) goto error_ret;

	return sei;

error_ret:
	perror(strerror(errno));
	if(sei)
		_net_close(sei);
	return 0;
}

inline i32 net_bind_session_ops(struct session* sei, const struct session_ops* ops)
{
	if(!sei) goto error_ret;

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

inline i32 net_session_count(struct net_struct* net)
{
	struct _inet_impl* inet;
	if(!net) goto error_ret;

	inet = _conv_inet_impl(net);

	return 0;
error_ret:
	return -1;
}

i32 net_set_user_ptr(struct session* ses, void* usr_ptr)
{
	if(!ses) goto error_ret;
	ses->usr_ptr = usr_ptr;
	return 0;
error_ret:
	return -1;
}

void* net_get_user_ptr(struct session* ses)
{
	if(!ses) goto error_ret;
	return ses->usr_ptr;
error_ret:
	return 0;
}



