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

static int *__test_arr = 0;
static int __capacity = 0;

struct _test_thread_info
{
	int thread_idx;
	int thread_cnt;
	int send_count;
};

static void __fill_msg(char *write_buf, int index)
{
	struct test_msg* msg = (struct test_msg*)write_buf;

	msg->v1 = 1;
	msg->v2 = 10000;
	msg->v3 = 11240293;
	msg->v4 = 98394853745098;
	msg->index = index;
	strncpy(msg->msg, "hello, world.", sizeof(msg->msg));
}

void swap_int(int *x, int *y) {
    int t = *x;
    *x = *y;
    *y = t;
}
void quick_sort_recursive(int* arr, int start, int end) {
    if (start >= end)
        return;
    int mid = arr[end];
    int left = start, right = end - 1;
    while (left < right) {
        while (arr[left] < mid && left < right)
            left++;
        while (arr[right] >= mid && left < right)
            right--;
        swap_int(&arr[left], &arr[right]);
    }
    if (arr[left] >= arr[end])
        swap_int(&arr[left], &arr[end]);
    else
        left++;
    if (left)
        quick_sort_recursive(arr, start, left - 1);
    quick_sort_recursive(arr, left + 1, end);
}

void quick_sort(int* arr, int len) {
    quick_sort_recursive(arr, 0, len - 1);
}

static void __on_read_msg(const char* buf, unsigned int size, int prod_service_type, int prod_service_index)
{
	struct test_msg* msg = (struct test_msg*)buf;

//	printf("[%u]: %s {%d:%d}\n", msg->index, msg->msg, prod_service_type, prod_service_index);

	++__recv_cnt;
}

static void __on_read_msg_ex(const char* buf, unsigned int size, int prod_service_type, int prod_service_index)
{
	struct test_msg* msg = (struct test_msg*)buf;

	if(__recv_cnt > __capacity) {
		printf("recv count error!\n");
		return;
	}

	__test_arr[__recv_cnt] = msg->index;
	//	printf("[%u]: %s {%d:%d}\n", msg->index, msg->msg, prod_service_type, prod_service_index);
	++__recv_cnt;
}

static int __send_msg(struct ipc_prod_port* prod_port, int count)
{
	int rslt;

	for(int i = 0; i < count; ++i) {
		char* write_buf = ipc_alloc_write_buf_mp(prod_port, sizeof(struct test_msg));
		err_exit_silent(!write_buf);

		__fill_msg(write_buf, i);

		rslt = ipc_write_mp(prod_port, write_buf);
		err_exit_silent(rslt < 0);

		__sync_add_and_fetch(&__send_cnt, 1);
		usleep(1);
	}

	return 0;
error_ret:
	return -1;
}

static int __send_msg_ex(struct ipc_prod_port* prod_port, int start, int count)
{
	int rslt;
	printf("__send_msg_ex, start: %d, count: %d\n", start, count);

	for(int i = 0; i < count; ++i) {
		char* write_buf = ipc_alloc_write_buf_mp(prod_port, sizeof(struct test_msg));
		err_exit_silent(!write_buf);

		__fill_msg(write_buf, i + start);

		rslt = ipc_write_mp(prod_port, write_buf);
		err_exit_silent(rslt < 0);

		__sync_add_and_fetch(&__send_cnt, 1);
		usleep(1);
	}

	return 0;
error_ret:
	return -1;
}

static int __read_msg(struct ipc_cons_port* cons_port, int count)
{
	int rslt;
	char read_buf[65535];

	for(int i = 0; i < count; ++i) {
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

	struct ipc_service_key cons_key = {
		.service_type = 1,
		.service_index = 1,
	};

	struct ipc_service_key prod_key = {
		.service_type = 2,
		.service_index = 1,
	};

	struct ipc_channel_cfg cfg = {
		.cons_service_key = { 1, 1 },
		.message_queue_len = 16,
		.message_count[0 ... MSG_POOL_COUNT - 1] = 32,
	};

	rslt = ipc_channel_load(&cfg.cons_service_key);
	if(rslt < 0) {
		printf("ipc channel not exist.\n");

		rslt = ipc_channel_create(&cfg);
		err_exit(rslt < 0, "create ipc channel failed.");
	}


	cons_port = ipc_open_cons_port(&cons_key, __on_read_msg);
	err_exit(!cons_port, "open cons port failed.");

	prod_port = ipc_open_prod_port(&cons_key, &prod_key);
	err_exit(!prod_port, "open prod port failed.");

	while(1) {
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
//	int index = (int)param;
	struct _test_thread_info *inf = (struct _test_thread_info *)param;

	struct ipc_prod_port* prod_port;

	struct ipc_service_key prod_key = {
		.service_type = 2,
		.service_index = inf->thread_idx,
	};

	while(!__cons_running) {
		usleep(100);
	}

	prod_port = ipc_open_prod_port(&__cons_key, &prod_key);
	err_exit(!prod_port, "open prod port failed: %d.", inf->thread_idx);

#if 0
	while(1)
	{
		int msg_cnt = inf->send_count; //random() % 32 + 1;

		struct timeval tv;
		gettimeofday(&tv, 0);

		if(tv.tv_sec - __start_time > 20)
			break;

		rslt = __send_msg_ex(prod_port, inf->thread_idx * msg_cnt, msg_cnt);
//		if(rslt < 0)
//			printf("send msg failed.\n");

		usleep(1);
		//		err_exit(rslt < 0, "send msg failed: %d.", index);
	}
#endif
	rslt = __send_msg_ex(prod_port, inf->thread_idx * inf->send_count, inf->send_count);

	ipc_close_prod_port(prod_port);

	free(inf);

	return 0;
error_ret:
	return (void*)-1;
}

int test_ipc_channel_multi_prod(int prod_cnt, int capacity)
{
	int rslt;
	int used = 0;
	pthread_t trds[prod_cnt];

	struct ipc_channel_cfg cfg = {
		.cons_service_key = __cons_key,
		.message_queue_len = 1024,
		.message_count[0 ... MSG_POOL_COUNT - 1] = 2048,
	};

	struct ipc_cons_port* cons_port;
	struct timeval tv;
	gettimeofday(&tv, 0);

	__start_time = tv.tv_sec;
	__capacity = capacity;

	__test_arr = (int *)malloc(capacity * sizeof(int));

	memset(__test_arr, 0, sizeof(int) * __capacity);

	rslt = ipc_channel_load(&cfg.cons_service_key);
	if(rslt < 0) {
		printf("ipc channel not exist.\n");

		rslt = ipc_channel_create(&cfg);
		err_exit(rslt < 0, "create ipc channel failed.");
	}
	else {
		printf("ipc channel loaded.\n");
	}

	cons_port = ipc_open_cons_port(&__cons_key, __on_read_msg_ex);
	err_exit(!cons_port, "open cons port failed.");

	for(int i = 0; i < prod_cnt; ++i) {
		cpu_set_t cs;
		struct _test_thread_info *inf = (struct _test_thread_info *)malloc(sizeof(struct _test_thread_info));
		inf->thread_idx = i;
		inf->thread_cnt = prod_cnt;

		if(i == prod_cnt - 1)
			inf->send_count = __capacity - used;
		else
			inf->send_count = __capacity / prod_cnt;

		used += inf->send_count;

		printf("thread: %d, sendcnt: %d\n", i, inf->send_count);

		rslt = pthread_create(&trds[i], 0, __prod_thread, (void*)inf);
		err_exit(rslt < 0, "create thread failed: %d.", i);

		CPU_SET(i, &cs);

		rslt = pthread_setaffinity_np(trds[i], sizeof(cs), &cs);
		err_exit(rslt < 0, "set thread cpu failed: %d.", i);
	}

	__cons_running = 1;

	while(1) {
		struct timeval tv;
		gettimeofday(&tv, 0);

		if(tv.tv_sec - __start_time > 5)
			break;

		rslt = __read_msg(cons_port, 32);
		usleep(1);
	}

	quick_sort(__test_arr, __capacity);

	for (int i = 0; i < __capacity; ++i) {
		printf("%d\n", __test_arr[i]);
	}

	printf("sent: %ld, recv: %ld\n", __send_cnt, __recv_cnt);

	ipc_channel_check_state_cons(cons_port);
	ipc_close_cons_port(cons_port);
	ipc_channel_destroy(&__cons_key);

error_ret:
	return -1;
}

