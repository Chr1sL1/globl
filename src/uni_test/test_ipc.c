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

#pragma pack(1)
struct test_msg
{
	unsigned char v1;
	unsigned short v2;
	unsigned int v3;
	unsigned long v4;
	unsigned int index;

	char msg[17];
};
#pragma pack()


static long __send_cnt = 0;
static long __recv_cnt = 0;

static long __start_time = 0;

static void __fill_msg(char* write_buf, int index)
{
	struct test_msg* msg = (struct test_msg*)write_buf;

	msg->v1 = 1;
	msg->v2 = 10000;
	msg->v3 = 11240293;
	msg->v4 = 98394853745098;
	msg->index = index;
	strncpy(msg->msg, "hello, world.", sizeof(msg->msg));
}

static void __on_read_msg(const char* buf, unsigned int size, int prod_service_type, int prod_service_index)
{
	struct test_msg* msg = (struct test_msg*)buf;

	printf("[%u]: %s {%d:%d}\n", msg->index, msg->msg, prod_service_type, prod_service_index);

	++__recv_cnt;
}

static int __send_msg(struct ipc_prod_port* prod_port, int count)
{
	int rslt;

	for(int i = 0; i < count; ++i)
	{
		char* write_buf = ipc_alloc_write_buf_mp(prod_port, sizeof(struct test_msg));
		err_exit_silent(!write_buf);

		__fill_msg(write_buf, i);

		rslt = ipc_write_mp(prod_port, write_buf);
		err_exit_silent(rslt < 0);

		__sync_add_and_fetch(&__send_cnt, 1);
	}

	return 0;
error_ret:
	return -1;
}

static int __read_msg(struct ipc_cons_port* cons_port, int count)
{
	int rslt;
	char read_buf[65535];

	for(int i = 0; i < count; ++i)
	{
		rslt = ipc_read_sc(cons_port);
		err_exit_silent(rslt < 0);
	}

	return 0;
error_ret:
	return -1;
}

int test_ipc_channel(void)
{
	int rslt;
	struct ipc_cons_port* cons_port;
	struct ipc_prod_port* prod_port;
	char* write_buf;

	struct ipc_service_key cons_key = 
	{
		.service_type = 1,
		.service_index = 1,
	};

	struct ipc_service_key prod_key = 
	{
		.service_type = 2,
		.service_index = 1,
	};

	struct ipc_channel_cfg cfg = 
	{
		.cons_service_key = { 1, 1 },
		.message_queue_len = 16,
		.message_count[0 ... MSG_POOL_COUNT - 1] = 32,
	};

	rslt = ipc_channel_load(&cfg.cons_service_key);
	if(rslt < 0)
	{
		printf("ipc channel not exist.\n");

		rslt = ipc_channel_create(&cfg);
		err_exit(rslt < 0, "create ipc channel failed.");
	}


	cons_port = ipc_open_cons_port(&cons_key, __on_read_msg);
	err_exit(!cons_port, "open cons port failed.");

	prod_port = ipc_open_prod_port(&cons_key, &prod_key);
	err_exit(!prod_port, "open prod port failed.");

	while(1)
	{
		int msg_cnt = random() % 16 + 1;
		rslt = __send_msg(prod_port, msg_cnt);
//		err_exit(rslt < 0, "send msg failed.");

		rslt = __read_msg(cons_port, msg_cnt);
//		err_exit(rslt < 0, "read msg failed.");
	}

	return 0;
error_ret:
	return -1;
}


//////////////
//

static int __cons_running = 0;
static struct ipc_service_key __cons_key = 
{
	.service_type = 1,
	.service_index = 1,
};

static void* __prod_thread(void* param)
{
	int rslt;
	int index = (int)param;

	struct ipc_prod_port* prod_port;

	struct ipc_service_key prod_key = 
	{
		.service_type = 2,
		.service_index = index,
	};

	while(!__cons_running)
	{
		usleep(1000);
	}

	prod_port = ipc_open_prod_port(&__cons_key, &prod_key);
	err_exit(!prod_port, "open prod port failed: %d.", index);

	while(1)
	{
		int msg_cnt = random() % 32 + 1;

		struct timeval tv;
		gettimeofday(&tv, 0);

		if(tv.tv_sec - __start_time > 10)
			break;

		rslt = __send_msg(prod_port, msg_cnt);
//		if(rslt < 0)
//			printf("send msg failed.\n");

		usleep(1);
		//		err_exit(rslt < 0, "send msg failed: %d.", index);
	}

	ipc_close_prod_port(prod_port);

	return 0;
error_ret:
	return (void*)-1;
}

int test_ipc_channel_multi_prod(int prod_cnt)
{
	int rslt;
	pthread_t trds[prod_cnt];

	struct ipc_channel_cfg cfg = 
	{
		.cons_service_key = __cons_key,
		.message_queue_len = 1024,
		.message_count[0 ... MSG_POOL_COUNT - 1] = 128,
	};

	struct ipc_cons_port* cons_port;
	struct timeval tv;
	gettimeofday(&tv, 0);

	__start_time = tv.tv_sec;

	rslt = ipc_channel_load(&cfg.cons_service_key);
	if(rslt < 0)
	{
		printf("ipc channel not exist.\n");

		rslt = ipc_channel_create(&cfg);
		err_exit(rslt < 0, "create ipc channel failed.");
	}

	cons_port = ipc_open_cons_port(&__cons_key, __on_read_msg);
	err_exit(!cons_port, "open cons port failed.");

	for(int i = 0; i < prod_cnt; ++i)
	{
		cpu_set_t cs;

		rslt = pthread_create(&trds[i], 0, __prod_thread, (void*)i);
		err_exit(rslt < 0, "create thread failed: %d.", i);

		CPU_SET(i, &cs);

		rslt = pthread_setaffinity_np(trds[i], sizeof(cs), &cs);
		err_exit(rslt < 0, "set thread cpu failed: %d.", i);
	}

	__cons_running = 1;

	while(1)
	{
		struct timeval tv;
		gettimeofday(&tv, 0);

		if(tv.tv_sec - __start_time > 11)
			break;

		rslt = __read_msg(cons_port, 32);
		usleep(1);
	}

	printf("sent: %ld, recv: %ld\n", __send_cnt, __recv_cnt);

	ipc_channel_check_state_cons(cons_port);

	ipc_close_cons_port(cons_port);

	ipc_channel_destroy(&__cons_key);

error_ret:
	return -1;
}


