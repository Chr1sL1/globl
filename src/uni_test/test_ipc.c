#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

#include "syslib/ipc_channel.h"
#include "syslib/misc.h"

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

static int __send_msg(struct ipc_local_port* prod_port, int count, int prod_service_type, int prod_service_index)
{
	int rslt;

	for(int i = 0; i < count; ++i)
	{
		char* write_buf = ipc_alloc_write_buf_mp(prod_port, sizeof(struct test_msg), prod_service_type, prod_service_index);
		err_exit(!write_buf, "alloc write buf failed: %d.", i);

		__fill_msg(write_buf, i);

		rslt = ipc_write_mp(prod_port, write_buf);
		err_exit(rslt < 0, "write msg failed: %d.", i);

		__sync_add_and_fetch(&__send_cnt, 1);
	}

	return 0;
error_ret:
	return -1;
}

static int __read_msg(int count)
{
	int rslt;
	char read_buf[65535];
	unsigned int read_size, prod_service_type, prod_service_index;

	for(int i = 0; i < count; ++i)
	{
		rslt = ipc_read_sc();
		err_exit(rslt < 0, "read msg failed: %d.", i);
	}

	return 0;
error_ret:
	return -1;
}

int test_ipc_channel(void)
{
	int rslt;
	struct ipc_local_port* prod_port;
	char* write_buf;

	struct ipc_channel_cfg cfg = 
	{
		.cons_service_type = 1,
		.cons_service_index = 1,
		.message_queue_len = 16,
		.message_count[0 ... MSG_POOL_COUNT - 1] = 4,
	};

	rslt = ipc_channel_load(cfg.cons_service_type, cfg.cons_service_index);
	if(rslt < 0)
	{
		printf("ipc channel not exist.\n");

		rslt = ipc_channel_create(&cfg);
		err_exit(rslt < 0, "create ipc channel failed.");
	}


	rslt = ipc_open_cons_port(1, 1, __on_read_msg);
	err_exit(rslt < 0, "open cons port failed.");

	prod_port = ipc_open_prod_port(1, 1);
	err_exit(!prod_port, "open prod port failed.");

	while(1)
	{
		int msg_cnt = random() % 16 + 1;
		rslt = __send_msg(prod_port, msg_cnt, 2, 1);
//		err_exit(rslt < 0, "send msg failed.");

		rslt = __read_msg(4);
//		err_exit(rslt < 0, "read msg failed.");
	}

	return 0;
error_ret:
	return -1;
}


//////////////
//

static int __cons_running = 0;

static void* __prod_thread(void* param)
{
	int rslt;
	int index = (int)param;

	struct ipc_local_port* prod_port;

	while(!__cons_running)
	{
		usleep(1000);
	}

	prod_port = ipc_open_prod_port(1, 1);
	err_exit(!prod_port, "open prod port failed: %d.", index);

	while(1)
	{
		int msg_cnt = random() % 4 + 1;

		struct timeval tv;
		gettimeofday(&tv, 0);

		if(tv.tv_sec - __start_time > 10)
			break;

		rslt = __send_msg(prod_port, msg_cnt, 10, index);
		if(rslt < 0)
			printf("send msg failed.\n");

		usleep(100);
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
		.cons_service_type = 1,
		.cons_service_index = 1,
		.message_queue_len = 1024,
		.message_count[0 ... MSG_POOL_COUNT - 1] = 128,
	};

	struct timeval tv;
	gettimeofday(&tv, 0);

	__start_time = tv.tv_sec;

	rslt = ipc_channel_load(cfg.cons_service_type, cfg.cons_service_index);
	if(rslt < 0)
	{
		printf("ipc channel not exist.\n");

		rslt = ipc_channel_create(&cfg);
		err_exit(rslt < 0, "create ipc channel failed.");
	}

	rslt = ipc_open_cons_port(1, 1, __on_read_msg);
	err_exit(rslt < 0, "open cons port failed.");

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

		if(tv.tv_sec - __start_time > 13)
			break;

		rslt = __read_msg(32);
		usleep(100);
	}

	printf("sent: %d, recv: %d\n", __send_cnt, __recv_cnt);

	return ipc_channel_check_state();

error_ret:
	return -1;
}


