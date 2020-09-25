#include "common_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

#include "core/ipc_channel.h"
#include "core/misc.h"
#include "core/asm.h"
#include "core/raft.h"

#define MAX_CONN_PROD_PORT_CNT	(8)

enum RaftMsgType
{
	rmtInvalid = 0,
	rmtRequestVote,
	rmtVote,
	rmtKeepAlive,
};

#pragma pack(1)
struct raft_header
{
	i8 msg_id;
};

struct raft_request_vote
{
	struct raft_header hdr;
	i32 service_id;
	i32 term;
};

struct raft_vote
{
	struct raft_header hdr;
	i32 vote_term;
};

struct raft_keepalive
{
	struct raft_header hdr;
	i32 term;
	i32 master_service_id;
	i32 last_log_id;
};
#pragma pack()

struct worker_context
{
	i32 prod_cnt;
	struct ipc_service_key my_key;
	struct ipc_cons_port* cons_port;
	struct ipc_prod_port** prod_port;
	struct raft_node* rn;
};

static inline struct ipc_prod_port* __get_port(struct worker_context* ctx, i32 service_id)
{
	if(service_id < 0 || service_id >= ctx->prod_cnt)
		return NULL;

	return ctx->prod_port[service_id];
}

static i32 __send_request_vote(struct raft_node* node, i32 target_service_id, i32 term_id)
{
	struct worker_context* ctx = (struct worker_context*)rf_usr_ptr(node);
	struct ipc_prod_port* p = __get_port(ctx, target_service_id);
	if(!p) return -1;

	struct raft_request_vote* msg = (struct raft_request_vote*)ipc_alloc_write_buf_mp(p, sizeof(struct raft_request_vote));
	if(!msg) return -1;

	msg->hdr.msg_id = rmtRequestVote;
	msg->service_id = target_service_id;
	msg->term = term_id;

	printf("request vote from %d, to %d, term: %d\n", ctx->my_key.service_index, target_service_id, term_id);

	return ipc_write_mp(p, msg);
}

static i32 __send_vote(struct raft_node* node, i32 target_service_id, i32 term_id)
{
	struct worker_context* ctx = (struct worker_context*)rf_usr_ptr(node);
	struct ipc_prod_port* p = __get_port(ctx, target_service_id);
	if(!p) return -1;

	struct raft_vote* msg = (struct raft_vote*)ipc_alloc_write_buf_mp(p, sizeof(struct raft_vote));
	if(!msg) return -1;

	msg->hdr.msg_id = rmtVote;
	msg->vote_term = term_id;

	printf("vote for %d, term: %d\n", target_service_id, term_id);

	return ipc_write_mp(p, msg);
}

static i32 __send_keepalive(struct raft_node* node, i32 target_service_id, i32 term_id, const char* log_update, i32 log_size)
{
	struct worker_context* ctx = (struct worker_context*)rf_usr_ptr(node);
	struct ipc_prod_port* p = __get_port(ctx, target_service_id);
	if(!p) return -1;

	struct raft_keepalive* msg = (struct raft_keepalive*)ipc_alloc_write_buf_mp(p, sizeof(struct raft_keepalive));
	if(!msg) return -1;

	msg->hdr.msg_id = rmtKeepAlive;
	msg->master_service_id = rf_service_id(node);
	msg->term = term_id;

	return ipc_write_mp(p, msg);
}

void ipc_on_recv(const char* buf, u32 size, i32 prod_service_type, i32 prod_service_index, void* usr_ptr)
{
	struct raft_header* hdr = (struct raft_header*)buf;
	struct worker_context* ctx = (struct worker_context*)usr_ptr;

	printf("ipc_on_recv cons services: %d\n", ctx->my_key.service_index);

	switch(hdr->msg_id)
	{
	case rmtRequestVote: {
		struct raft_request_vote* msg = (struct raft_request_vote*)(buf);
		rf_on_request_vote(ctx->rn, prod_service_index, msg->term);
	}
		break;
	case rmtVote: {
		struct raft_vote* msg = (struct raft_vote*)(buf);
		rf_on_vote_for_me(ctx->rn, prod_service_index, msg->vote_term);
	}
		break;
	case rmtKeepAlive: {
		struct raft_keepalive* msg = (struct raft_keepalive*)(buf);
		err_exit(msg->master_service_id != prod_service_index, "critical error on master service id.");

		rf_on_keep_alive(ctx->rn, msg->master_service_id, msg->term);
	}
		break;
	default:
		break;
	}

	return;
error_ret:
	return;
}

static void* __worker_thread(void* p)
{
	i32 ret_code;
	struct worker_context* ctx = (struct worker_context*)p;
	struct ipc_prod_port* prod_port[ctx->prod_cnt];

	struct raft_msg_sync_op ops = {
		._request_vote_func = __send_request_vote,
		._vote_func = __send_vote,
		._keep_alive_func = __send_keepalive,
	};

	ctx->rn = rf_create_node(ctx->my_key.service_index, &ops, ctx);
	err_exit(!ctx->rn, "create raft node failed.");

	for(i32 i = 0; i < ctx->prod_cnt; ++i)
	{
		rf_add_remote_node(ctx->rn, i);
	}

	rf_start_service(ctx->rn);

	while(1) {

		rf_update(ctx->rn);
		ret_code = ipc_read_sc(ctx->cons_port);
		if(ret_code < 0)
			usleep(1);
	}

	printf("thread ended\n");
	return 0;
error_ret:
	printf("thread ended with error.\n");
	return (void*)-1;
}

static struct worker_context* __ctx = NULL;

static i32 __open_ports(struct worker_context* ctx)
{
	ctx->cons_port = ipc_open_cons_port(&ctx->my_key, ipc_on_recv, ctx);
	err_exit(!ctx->cons_port, "open cons port failed.");

	ctx->prod_port = (struct ipc_prod_port**)malloc(sizeof(struct ipc_prod_port*) * ctx->prod_cnt);

	for(i32 i = 0; i < ctx->prod_cnt; ++i)
	{
		if(i == ctx->my_key.service_index)
			continue;

		struct ipc_service_key remote_key = {
			.service_type = 0,
			.service_index = i,
		};

		ctx->prod_port[i] = ipc_open_prod_port(&remote_key, &ctx->my_key);
		err_exit(ctx->prod_port[i] == NULL, "failed to open prod port.");
	}

	return 0;
error_ret:
	return -1;
}

void test_raft(i32 node_cnt)
{
	i32 ret_code;
	pthread_t trds[node_cnt];

	__ctx = (struct worker_context*)malloc(sizeof(struct worker_context));

	for(i32 i = 0; i < node_cnt; ++i) {
		struct ipc_channel_cfg cfg = {
			.cons_service_key = { .service_type = 0, .service_index = i },
			.message_queue_len = 64,
			.message_count[0 ... MSG_POOL_COUNT - 1] = 64,
		};

		ret_code = ipc_channel_create(&cfg);
		if(ret_code < 0)
			ret_code = ipc_channel_load(&cfg.cons_service_key);

		err_exit(ret_code < 0, "create ipc channel failed [%d].", i);

		__ctx[i].my_key = cfg.cons_service_key;
		__ctx[i].prod_cnt = node_cnt;

	}

	for(i32 i = 0; i < node_cnt; ++i)
	{
		ret_code = __open_ports(&__ctx[i]);
		err_exit(ret_code < 0, "open ports failed: %d", i);
	}

	for(i32 i = 0; i < node_cnt; ++i)
	{
		cpu_set_t cs;
		ret_code = pthread_create(&trds[i], 0, __worker_thread, (void*)&__ctx[i]);
		err_exit(ret_code < 0, "create thread failed: %d.", i);

		CPU_SET(i, &cs);

		ret_code = pthread_setaffinity_np(trds[i], sizeof(cs), &cs);
		err_exit(ret_code < 0, "set thread cpu failed: %d.", i);
	}

	while(1)
		sleep(1);


error_ret:
	return;
}
