#include "common_types.h"
#include "core/net.h"
#include "core/mmspace.h"
#include "core/misc.h"
#include "core/asm.h"

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SVR_SESSION_ZONE_NAME "test_svr_session_zone"
#define USR_TYPE_INFO (0x123123)
#define MAX_LIVE_i32ERVAL (10000)

#define TEST_CONN_COUNT (10)

static struct mmcache* __svr_session_zone = 0;
static i32 __running = 1;
static u64 __start_time = 0;
static u64 __time_val = 0;

static u64 __recv_bytes_server = 0;
static u64 __recv_bytes_client = 0;
static u64 __send_bytes = 0;

enum USR_SESSION_STATE
{
	USS_DISCONNECTED = 0,
	USS_ESTABLISHING,
	USS_RUNNING,
	USS_DISCONNECTING,

	USS_COUNT,
};

static struct net_config __cfg =
{
	.nr_acceptor = 64,
	.nr_session = 64,
	.recv_buff_len = 1024 * 128,
	.send_buff_len = 1024 * 128,
};

struct svr_session
{
	struct session* s;
	u32 state;
};

struct usr_session
{
	struct session* s;
	u32 idx;
	u32 state;
	u64 type_info;
	u64 conn_tick;
	u64 disconn_tick;
	u64 live_i32erval;
	u64 sleep_i32erval;
};

static struct usr_session __conn_session[TEST_CONN_COUNT] =
{
	[0 ... TEST_CONN_COUNT - 1] = {
		.s = 0,
		.idx = 0,
		.state = USS_DISCONNECTED,
		.type_info = USR_TYPE_INFO,
		.conn_tick = 0,
		.disconn_tick = 0,
		.live_i32erval = 0,
		.sleep_i32erval = 0,
	}
};

static void _svr_session_ctor(void* ptr)
{
	struct svr_session* us = (struct svr_session*)ptr;
	us->s = 0;
	us->state = USS_DISCONNECTED;
}

static void _svr_session_dtor(void* ptr)
{
	struct svr_session* us = (struct svr_session*)ptr;
	us->s = 0;
	us->state = USS_DISCONNECTED;

error_ret:
	return;
}

static void _signal_stop(i32 sig, siginfo_t* t, void* usr_data)
{
	__running = 0;
}

static i64 _restore_zone(void)
{
	if(!__svr_session_zone)
		__svr_session_zone = mm_search_zone(SVR_SESSION_ZONE_NAME);

	if(__svr_session_zone && __svr_session_zone->obj_size != sizeof(struct svr_session))
		goto error_ret;

	__svr_session_zone = mm_cache_create(SVR_SESSION_ZONE_NAME, sizeof(struct svr_session), _svr_session_ctor, _svr_session_dtor);

	if(!__svr_session_zone)
		goto error_ret;

	return 0;
error_ret:
	return -1;
}

static i32 on_acc(struct acceptor* acc, struct session* se)
{
	i32 rslt;
	struct svr_session* ss;

	ss = mm_cache_alloc(__svr_session_zone);
	err_exit(!ss, "on_acc alloc session failed.");
	ss->s = se;
	se->usr_ptr = ss;

	return 0;
error_ret:
	return -1;
}

static i32 on_server_disconn(struct session* se)
{
	i32 rslt;
	struct svr_session* ss = (struct svr_session*)se->usr_ptr;

	ss->s = 0;

	rslt = mm_cache_free(__svr_session_zone, ss);
	err_exit(rslt < 0, "on_disconn free session failed.");

	se->usr_ptr = 0;


	return 0;
error_ret:
	return -1;
}

static i32 on_server_recv(struct session* se, const void* buf, i32 len)
{
	__recv_bytes_server += len;
//	if(len < 4)
//		net_disconnect(se);

	net_send(se, buf, len);

	return 0;
error_ret:
	return -1;
}


static i32 on_client_recv(struct session* se, const void* buf, i32 len)
{
	__recv_bytes_client += len;

	return 0;
error_ret:
	return -1;
}

static void set_state(struct usr_session* us, i32 state)
{
	us->state = state;
}

static i32 on_client_conn(struct session* se)
{
	struct usr_session* us = (struct usr_session*)se->usr_ptr;
	err_exit(!us, "strange error in on_client_conn");

	err_exit(us->state != USS_ESTABLISHING, "us state error.");

	if(us->s != se)
	{
		printf("fuck error session.\n");
		goto error_ret;
	}

	set_state(us, USS_RUNNING);
	us->conn_tick = __time_val;
	us->live_i32erval = random() % MAX_LIVE_i32ERVAL;
	us->sleep_i32erval = random() % MAX_LIVE_i32ERVAL;

//	if(us->idx < 100)
//		printf("client session connected [%d]\n", us->idx);

	return 0;
error_ret:
	return -1;
}

static i32 on_client_disconn(struct session* se)
{
	struct usr_session* us = (struct usr_session*)se->usr_ptr;
	err_exit(!us, "strange error in on_client_disconn");

	set_state(us, USS_DISCONNECTED);
	us->s = 0;

	se->usr_ptr = 0;
	us->disconn_tick = __time_val;

//	if(us->idx < 100)
//		printf("client session disconnected [%d]\n", us->idx);

	return 0;
error_ret:
	return -1;
}

static i32 fill_send_data(char* buf, i32 size)
{
	i32 len = random() % size;

	for(i32 i = 0; i < len; i++)
	{
		buf[i] = random() % 255;
	}

	return len;
}

static i32 run_connector(struct net_struct* net)
{
	struct session_ops ops = 
	{
		.func_conn = on_client_conn,
		.func_recv = on_client_recv,
		.func_disconn = on_client_disconn,
	};
	u64 r1 = 0, r2 = 0;
	i32 send_len;
	i32 pending_count = 0;
	u32 ip = inet_addr("10.85.45.212");

	char send_buf[net->cfg.send_buff_len];

	r1 = rdtsc();

	for(i32 i = 0; i < TEST_CONN_COUNT; ++i)
	{
		if(__conn_session[i].state == USS_DISCONNECTED)
		{
			if(__time_val - __conn_session[i].conn_tick < __conn_session[i].sleep_i32erval)
				continue;

			__conn_session[i].idx = i;

			struct session* s = net_connect(net, ip, 7099);
			err_exit(!s, "connect failed [%d]", i);

			net_bind_session_ops(s, &ops);

			set_state(&__conn_session[i], USS_ESTABLISHING);
			__conn_session[i].s = s;
			s->usr_ptr = &__conn_session[i];
			continue;
		}

		if(__conn_session[i].state == USS_RUNNING)
		{
			if(__time_val > __conn_session[i].conn_tick +  __conn_session[i].live_i32erval)
			{
//				if(i < 100) printf("try disconnect [%d]\n", i);
				set_state(&__conn_session[i], USS_DISCONNECTING);
				net_disconnect(__conn_session[i].s);
				continue;
			}

			do
			{
				send_len = fill_send_data(send_buf, net->cfg.send_buff_len);
			} while(send_len <= 0);

			net_send(__conn_session[i].s, send_buf, send_len);
			__send_bytes += (net->cfg.send_buff_len - 1);
			continue;
		}

		if(__conn_session[i].state == USS_DISCONNECTING)
		{
			if(__conn_session[i].sleep_i32erval + __conn_session[i].disconn_tick + 3000 < __time_val)
				++pending_count;
		}
	}

//	if(pending_count > 0)
//		printf("pending count: %d\n", pending_count);

	r2 = rdtsc();

//	printf("run_connector cycle: %lu\n", r2 - r1);

	return 0;
error_ret:
	return -1;
}

i32 net_test_server(i32 silent)
{
	i32 rslt;

	u64 count_tick = 0;

	struct net_ops ops;
	struct net_struct* net;
	struct acceptor* acc;
	FILE* fp = 0;;
	struct timeval tv;

	struct sigaction sa;
	sa.sa_sigaction = _signal_stop;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGINT, &sa, 0);

	rslt = _restore_zone();
	err_exit(rslt < 0, "restore zone failed.");

	ops.func_recv = on_server_recv;
	ops.func_disconn = on_server_disconn;
	ops.func_acc = on_acc;
	ops.func_conn = 0;

	gettimeofday(&tv, 0);
	__start_time = tv.tv_sec * 1000 + tv.tv_usec / 1000;

	if(silent)
	{
		fp = freopen("/dev/null", "w", stderr);
		err_exit(!fp, "redirect stderr failed");
	}

	net = net_create(&__cfg, &ops);
	err_exit(!net, "create net failed.");

	acc = net_create_acceptor(net, 0, 7099);
	err_exit(!acc, "create acceptor failed.");

	while(__running)
	{
		u64 time_diff;
		gettimeofday(&tv, 0);
		__time_val = tv.tv_sec * 1000 + tv.tv_usec / 1000;

		time_diff = __time_val - __start_time;

		rslt = run_connector(net);
		err_exit(rslt < 0, "run_connector failed.");

		rslt = net_run(net, 10);
		err_exit(rslt < 0, "net_run failed.");

		if(__time_val > count_tick + 500)
		{
			count_tick = __time_val;
			if(time_diff > 0)
			{
				printf(">>>>>>>>>>>>>>>>>>>>>> session count: %lu, server recvd bytes: %lu, client recvd bytes: %lu, upload speed: %lu(KB/s), download speed: %lu(KB/s).\n",
						net_session_count(net), __recv_bytes_server, __recv_bytes_client, __recv_bytes_server * 2 / time_diff, __recv_bytes_client * 2 / time_diff);
			}
		}
	}

	net_destroy_acceptor(acc);
	net_destroy(net);

	return 0;
error_ret:
	if(fp)
		fclose(fp);
	return -1;
}
