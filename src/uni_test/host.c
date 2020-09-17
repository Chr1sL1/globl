#include "common_types.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#include "core/shmem.h"
#include "core/rbtree.h"
#include "core/dlist.h"
#include "core/graph.h"
#include "core/common.h"
#include "core/misc.h"
#include "core/vm_space.h"
#include "core/net.h"
#include "core/timer.h"
#include "core/co.h"
#include "core/asm.h"
#include "core/hash.h"

#include "test.pb.h"
#include "pb_encode.h"
#include "pb_decode.h"


#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

const char* share_memory_name = "test_shm_17x";
static const char* alpha_beta = "abcdefghijlmnopqrstuvwxyz1234567890";

long test_arr[100];

char* mmp_buf;
char* pgp_buf;
char* zone_buf;

long running = 0;

int is_reloaded = 0;

void swap(long* a, long* b)
{
	long tmp = *a;
	*a = *b;
	*b = tmp;
}

void print_array(int* a, int n)
{
	for(int i = 0; i < n; ++i)
	{
		printf("%d,", a[i]);
	}
	printf("\n");
}

void random_shuffle(long* a, int n)
{
	for(int i = 0; i < n; ++i)
	{
		int rd = random() % n;
		swap(&a[i], &a[rd]);
	}
}


void print_node(struct rbnode* node)
{
	if(node)
	{
		struct rbnode* lc = node->lchild;
		struct rbnode* rc = node->rchild;

		printf("%lu(%d)[%lu,%lu] ", (unsigned long)node->key, node->isblack, lc ? (unsigned long)lc->key:0, rc ? (unsigned long)rc->key:0);
	}

}

void signal_stop(int sig, siginfo_t* t, void* usr_data)
{
	struct mmpool* mp = (struct mmpool*)usr_data;
	printf("------------------recvd signal------------------------\n");


	printf("------------------signal end------------------------\n");
	running = 0;
}

void test_rbtree(void)
{
	unsigned long r1 = 0, r2 = 0;
	int test_arr_count= sizeof(test_arr) / sizeof(long);
	struct timeval tv_begin, tv_end;

	for(int i = 0; i < test_arr_count; i++) 
		test_arr[i] = i;

	random_shuffle(test_arr, test_arr_count);

	struct rbtree test_tree;
	rb_init(&test_tree, 0);

	gettimeofday(&tv_begin, NULL);

	for(int i = 0; i < test_arr_count; i++)
	{
		struct rbnode* node = (struct rbnode*)malloc(sizeof(struct rbnode));
		rb_fillnew(node);

		node->key = (void*)test_arr[i];

		r1 = rdtsc();
		rb_insert(&test_tree, node);
		r2 = rdtsc();
		printf("rbinsert: %lu cycles.\n", r2 - r1);

		pre_order(test_tree.root, print_node);
		printf("\n");
		in_order(test_tree.root, print_node);
		printf("\n---size %d---\n", test_tree.size);
	}

	gettimeofday(&tv_end, NULL);
	printf("insert elapse: %ld.\n", (long)tv_end.tv_usec - (long)tv_begin.tv_usec);
	printf("rooooooooooooooooooot:%lu\n", (unsigned long)test_tree.root->key);

//	printf("traverse: \n");
//	rb_traverse(&test_tree, print_node);	
//	printf("\n");

	random_shuffle(test_arr, test_arr_count);

	gettimeofday(&tv_begin, NULL);

	for(int i = 0; i < test_arr_count; i++)
	{
		printf("remove key: %lu.\n", (unsigned long)test_arr[i]);
		r1 = rdtsc();
		struct rbnode* node = rb_remove(&test_tree, (void*)test_arr[i]);
		r2 = rdtsc();
		printf("rbremove: %lu cycles.\n", r2 - r1);

		if(node)
		{
			pre_order(test_tree.root, print_node);
			printf("\n");
			in_order(test_tree.root, print_node);
			printf("\n---size %d---\n", test_tree.size);

			free(node);
			node = NULL;
		}
	}

	gettimeofday(&tv_end, NULL);
	printf("delete elapse: %ld.\n", (long)tv_end.tv_usec - (long)tv_begin.tv_usec);
}

void test_lst(void)
{
	struct kt
	{
		int value;
		struct dlnode nd;
	};

	struct dlist lst;
	struct kt* k = NULL;
	lst_new(&lst);
	int test_arr_count= sizeof(test_arr) / sizeof(long);

	for(int i = 0; i < test_arr_count; i++) 
		test_arr[i] = i;

	random_shuffle(test_arr, test_arr_count);

	for(int i = 0; i < test_arr_count; ++i)
	{
		k = malloc(sizeof(struct kt));
		lst_clr(&k->nd);
		k->value = test_arr[i];
		lst_push_back(&lst, &k->nd);

		printf("%d,", k->value);
	}

	printf("\n");

	struct dlnode * it = lst.head.next;
	while(it != &lst.tail)
	{
		struct kt* k = (struct kt*)((void*)it - (void*)(&((struct kt*)0)->nd));
		printf("%d,", k->value);

		it = it->next;
	}

	printf("\n");

	for(int i = 0; i < test_arr_count; ++i)
	{
		struct dlnode* n = lst_pop_front(&lst);
		struct kt* k = node_cast(kt, n, nd);
		printf("%d,", k->value);
	}
	printf("\n");
}


struct mem_test_entry
{
	void* _block;
	long _size;
	long _usage_duration;
	long _alloc_time;
};

//long test_slb(long total_size, long obj_size, long node_count)
//{
//	long rslt = 0;
//	void* mm_buf = 0;
//	unsigned long now_time = 0;
//	unsigned long loop_count = 0;
//	unsigned long restart_alloc_time = 0;
//	long req_total_size = 0;
//	long enable_alloc = 1;
//	struct mmslab* mp = 0;
//	struct mem_test_entry te[node_count];
//	struct timeval tv;
//	struct shmm_blk* sbo = 0;
//	long shmm_channel = 18;
//
//	mm_buf = malloc(total_size + 1024);
//
//	mp = slb_create(move_ptr_align64(mm_buf, 0), total_size, obj_size);
//	if(!mp) goto error_ret;
//
//	for(long i = 0; i < node_count; ++i)
//	{
//		te[i]._size = obj_size;
//		te[i]._block = 0;
//		te[i]._usage_duration = random() % 200;
//		te[i]._alloc_time = 0;
//
//		req_total_size += te[i]._size;
//	}
//
//	printf("req_total_size : %ld\n", req_total_size);
//	running = 1;
//
//	struct sigaction sa;
//	sa.sa_sigaction = signal_stop;
//	sa.sa_flags = SA_SIGINFO;
//	sigaction(SIGINT, &sa, 0);
//
//	while(running)
//	{
//		gettimeofday(&tv, 0);
//		now_time = tv.tv_sec * 1000000 + tv.tv_usec;
//		loop_count++;
//
//		for(long i = 0; i < node_count; ++i)
//		{
//			if(enable_alloc)
//			{
//				if(te[i]._alloc_time == 0)
//				{
//					printf("--- alloc [%ld], idx: %ld, duration: %ld, loop_count: %ld\n", te[i]._size, i, te[i]._usage_duration, loop_count);
//
//					te[i]._block = slb_alloc(mp);
//					if(!te[i]._block)
//					{
//						printf("alloc error, loopcount: %ld, idx: %ld, reqsize: %ld.\n", loop_count, i, te[i]._size);
//						enable_alloc = 0;
//						restart_alloc_time = now_time + 2000;
//					}
//					else
//					{
//						te[i]._alloc_time = now_time;
//
//
////						if(slb_check(mp) < 0)
////							goto error_ret;
//
//					}
//				}
//			}
//			else if(restart_alloc_time > now_time)
//			{
//				enable_alloc = 1;
//				restart_alloc_time = 0;
//			}
//
//
//			if(te[i]._alloc_time + te[i]._usage_duration < now_time)
//			{
//				printf("--- free [%ld], idx: %ld, loop_count: %ld.\n", te[i]._size, i, loop_count);
//
//				rslt = slb_free(mp, te[i]._block);
//				if(rslt < 0)
//				{
//					printf("free error, loopcount: %ld, idx: %ld.\n", loop_count, i);
//					goto error_ret;
//				}
//
//				te[i]._alloc_time = 0;
//
////				if(slb_check(mp) < 0)
////					goto error_ret;
//			}
//
//
//		}
//		if(rslt < 0) goto error_ret;
//loop_continue:
//		usleep(10);
//	}
//
//	slb_destroy(mp);
////	shmm_destroy(&sb);
//	printf("test slb successed.\n");
//	return 0;
//error_ret:
//	if(mp)
//		slb_check(mp);
//
////	if(sb)
////		shmm_destroy(&sb);
//
//	printf("test slb failed.\n");
//	return -1;
//}


//long profile_slb(void)
//{
//	long rslt = 0;
//	unsigned int size = 200 * 1024;
//	long rnd = 0;
//	unsigned long r1 = 0, r2 = 0;
//
//	unsigned long tmp = 0;
//	unsigned long alloc_sum = 0, free_sum = 0;
//	unsigned long alloc_max = 0, free_max = 0;
//	unsigned long count = 1000;
//
//	mmp_buf = malloc(size);
//
//	struct mmslab* pool = slb_create(move_ptr_align64(mmp_buf, 0), size, 100);
//
//	if(!pool) goto error_ret;
//
//	for(long i = 0; i < count; i++)
//	{
//		rnd = random() % 1024;
//
//		if(rnd <= 0)
//			continue;
//
//		r1 = rdtsc();
//		void* p = slb_alloc(pool);
//		r2 = rdtsc();
//
//		tmp = r2 - r1;
//		alloc_sum += tmp;
//		if(tmp > alloc_max)
//			alloc_max = tmp;
//
//		if(!p)
//			printf("alloc errrrrrrrrrrrrrrrrror.\n");
//
//		r1 = rdtsc();
//		rslt = slb_free(pool, p);
//		r2 = rdtsc();
//
//		if(rslt < 0)
//			printf("free errrrrrrrrrrrrrrrrror.\n");
//
//		tmp = r2 - r1;
//		free_sum += tmp;
//		if(tmp > free_max)
//			free_max = tmp;
//	}
//
//	printf("[avg] alloc cycle: %lu, free cycle: %lu.\n", alloc_sum / count, free_sum / count);
//	printf("[max] alloc cycle: %lu, free cycle: %lu.\n", alloc_max, free_max);
//
//	rslt = slb_check(pool);
//	if(rslt < 0)
//		printf("slb_check errrrrrrrrrrrrrrrrrrror.\n");
//
//
//	slb_destroy(pool);
//
//	return 0;
//error_ret:
//	return -1;
//}


unsigned int at2f(unsigned v)
{
	int i = 32;
	unsigned int k = 0;

	for(; i >= 0; --i)
	{
		k = (1 << i);
		if((k & v) != 0)
			break;
	}

	return k;
}

unsigned int at2t(unsigned v)
{
	int i = 32;
	unsigned int k = 0;

	for(; i >= 0; --i)
	{
		k = (1 << i);
		if((k & v) != 0)
			break;
	}

	return (1 << (k + 1));
}

//unsigned long test_asm(unsigned long val)
//{
//	unsigned long k = 100;
//
//	__asm__("addq %1, %0\n\t" : "+r"(k) : ""(val));
//
//	return k;
//}
//
//unsigned long test_asm_align8(unsigned long val)
//{
//	unsigned long ret;
//	__asm__("addq $8, %1\n\t"
//			"andq $-8, %1\n\t"
//			"movq %1, %0\n\t"
//			: "=r"(ret), "+r"(val));
//
//	return ret;
//}


long test_qqq(long a, long b, long c)
{
	if((a & 0xf) != 0 || (b & 0xf) != 0 || (c & 0xf) != 0)
		return -1;

	a += 1;
	b += 2;
	c += 3;

	return a + b + c;
}

void shmm_sig_int(int sig)
{
	if(sig == SIGINT)
		printf("caught SIGINT, pid: %d\n", getpid());
}

long test_shmm(void)
{
	long rslt = 0;
	pid_t pid = fork();

	if(pid != 0)
	{
//		int status = 0;
//		struct ring_buf* rb;
//		struct shmm_blk* sb = shmm_create(101, 0, 256, 0);
//		if(!sb)
//		{
//			printf("main process exit with error: %d\n", errno);
//			exit(-1);
//		}
//
//		rslt = rbuf_new(sb->addr_begin, sb->addr_end - sb->addr_begin);
//		if(rslt < 0)
//		{
//			printf("new ringbuf failed\n");
//			exit(-1);
//		}
//
//		rb = rbuf_open(sb->addr_begin);
//		if(!rb)
//		{
//			printf("open ring buf failed\n");
//			exit(-1);
//		}
//
//		if(rbuf_write_block(rb, "1234567890", 10) < 0)
//		{
//			printf("write ringbuf failed\n");
//			exit(-1);
//		}
//
//		printf("main process wrote to ringbuf.\n");
//
//		wait(&status);
//
//		rbuf_del(&rb);
//		shmm_destroy(sb);
//
//		printf("main process exit with success.\n");
//		exit(0);
	}
	else
	{
//		int sig;
//		sigset_t ss;
//		sigemptyset(&ss);
//		sigaddset(&ss, SIGINT);
//
//		printf("child process started with pid: %d.\n", getpid());
//
//		signal(SIGINT, shmm_sig_int);
//		sigwait(&ss, &sig);
//
//		char read_buf[2] = { 0 };
//		struct ring_buf* rb;
//		rslt = 0;
//		struct shmm_blk* sb = shmm_open(101, 0);
//		if(!sb)
//		{
//			printf("child process exit with error: %d\n", errno);
//			exit(-1);
//		}
//
//		rb = rbuf_open(sb->addr_begin);
//		if(!rb)
//		{
//			printf("open ring buf failed\n");
//			exit(-1);
//		}
//
//		while(rslt >= 0)
//		{
//			rslt = rbuf_read_block(rb, read_buf, 1);
//
//			if(rslt >= 0)
//				printf("read from ringbuf: %s\n", read_buf);
//		}
//
//		rbuf_close(&rb);
//		shmm_close(sb);
//
//		printf("child process exit with success.\n");
//		exit(0);
	}

	return 0;
error_ret:
	perror(strerror(errno));
	return -1;
}

#pragma pack(1)

struct _ipc_msg_body
{
	unsigned char _msgsize;
	char _buf[256];
};

#pragma pack()


#define dbg(format, ...) printf(format, __VA_ARGS__)

extern long net_test_server(int);
extern int test_ipc_channel(void);
extern int test_ipc_channel_multi_prod(int, int);

extern void test_lua(void);
extern void test_misc(void);

struct bit_set
{
	unsigned long bits[16];
};

void mydiv(int divend, int divisor, int* quotient, int* reminder)
{
	*quotient = divend / divisor;
	*reminder = divend % divisor;
}

void set_bit(struct bit_set* bs, int bit)
{
	int idx, b_idx;
	mydiv(bit, 64, &idx, &b_idx);

	bs->bits[idx] |= (1LL << b_idx);
}

void clr_bit(struct bit_set* bs, int bit)
{
	int idx, b_idx;
	mydiv(bit, 64, &idx, &b_idx);

	bs->bits[idx] &= ~(1LL << b_idx);
}

struct test_stru
{
	unsigned long t1;
	unsigned long t2;
	unsigned long t3;
} __attribute__((aligned(16)));

struct test_co_struct
{
	struct co_task* _co;
	int i;
	int j;
};

static void co_func(struct co_task* co, void* param);

static void co_func(struct co_task* co, void* param)
{
	struct test_co_struct* ts = (struct test_co_struct*)param;

	for(; ts->i < 1000; ++ts->i)
	{
		printf("i = %d\n", ts->i);

		if(ts->i % 4 == 0)
		{
			co_yield(co);
			sleep(1);
		}
	}
}

void test_co()
{
	struct test_co_struct* ts;
	void* rsp = 0;

	if(!is_reloaded)
	{
		printf("---- test_co: new alloc.\n");
		ts = vm_common_alloc(sizeof(struct test_co_struct));
		err_exit(!ts, "ts failed.");

		ts->_co = co_create(co_func);
		err_exit(!ts->_co, "failed.");

		ts->i = 0;
		ts->j = 100;
	}
	else
	{
		printf("---- test_co: reloaded.\n");
	}

	co_run(ts->_co, ts);

	for(; ts->j < 200; ++ts->j)
	{
		printf(">>> j = %d\n", ts->j);

		if(ts->j % 2 == 0)
		{
			co_resume(ts->_co);
			printf("yield cycles: %lu.\n", co_profile_yield(ts->_co));
//			sleep(1);
		}
	}

	co_destroy(ts->_co);
	vm_common_free(ts);

error_ret:
	return;
}

static void my_timer_func(struct timer_node* t, void* param)
{
	struct co_task* co = (struct co_task*)param;

	printf("timer_func called.\n");

	co_resume(co);
}


static void rpc_co_func(struct co_task* co, void* param)
{
	struct timer_node* tm = timer_schedule(100, my_timer_func, 1, co);
	err_exit(!tm, "schedule timer error.");

	printf("rpc_co_func running.\n");

	co_yield(co);

	printf("rpc_co_func returned.\n");

error_ret:
	return;
}

int rpc_call(const char* param, int param_size)
{
	struct co_task* co = co_create(rpc_co_func);
	err_exit(!co, "rpc co create failed.");

	co_run(co, 0);

	printf("rpc call returned.\n");

	running = 0;
	
	return 0;
error_ret:
	return -1;
}

void test_rpc(void)
{
	rpc_call("111", 3);
	running = 1;

	while(running)
	{
		on_tick();
		usleep(1000);
	}
}

void test_pb()
{
	unsigned char buffer[128];

	test_message msg = test_message_init_zero;
	msg.value1 = 100;
	msg.value2 = 3463;

	pb_ostream_t ostream = pb_ostream_from_buffer(buffer, sizeof(buffer));
	pb_encode(&ostream, test_message_fields, &msg);

	pb_istream_t istream = pb_istream_from_buffer(buffer, ostream.bytes_written);

	pb_decode(&istream, test_message_fields, &msg);

	printf(">>>>>> >>>>>>   msg.value1: %d, msg.value2: %ld\n", msg.value1, msg.value2);
}

#pragma pack(1)

union shm_key
{
	struct
	{
		unsigned reserved : 16;

		struct
		{
			union
			{
				struct
				{
					unsigned area_idx : 4;
					unsigned area_type : 8;
				};

				struct
				{
					unsigned service_idx : 6;
					unsigned service_type : 6;
				};
			};

			unsigned type : 4;
		};

	};

	int the_key;
};



#pragma pack()

void test_hash(i32 count, i32 bucket_size)
{
	i32* result_arr = (i32*)malloc(count * sizeof(i32));
	u64 r1 = rdtsc();

	for(i32 i = 0; i < count; ++i) {
		i32 k = random();
		i32 hash_value = jump_consist_hash(k, bucket_size);
		result_arr[i] = hash_value;
//		printf("hash_value: %d\n", hash_value);
	}

	for(i32 i = 0; i < count; ++i) {
		printf("%d,", result_arr[i]);
	}

	printf("\n");
	printf("avg rdtsc: %d\n", (rdtsc() - r1)/count);

	free(result_arr);
}

struct co_task_context
{
	i32* current_value;
};


void co_task_func(struct co_task* co, void* param)
{
	struct co_task_context* cont = (struct co_task_context*)param;
	
	for(i32 i = 0; i < 100; ++i)
	{
		++(*cont->current_value);
		printf("value: %d\n", *cont->current_value);
		co_yield(co);
		++(*cont->current_value);
		co_yield(co);
	}
}

void run_co_test(void)
{
	i32 test_value = 0;
	struct co_task_context cont;

	struct co_task* co = co_create(co_task_func);
	err_exit_silent(!co);

	cont.current_value = &test_value;

	co_run(co, &cont);

	while(*cont.current_value < 100)
	{
		co_resume(co);
	}

error_ret:
	return;
}

int main(void)
{

	i32 ret_code;

//	unsigned long i = test_asm_align8(10);
//	printf("%lu\n", i);
//
	unsigned long seed = time(0);
	srandom(seed);


	struct test_co_struct tcs[10];

	printf("%ld\n", &tcs[5] - &tcs[0]);

//	getchar();

	printf("popcnt: %d\n", popcnt32(-1));

	printf("test_stru: %ld\n", sizeof(struct test_stru));

	struct bit_set bs;
	memset(&bs, 0, sizeof(bs));
	set_bit(&bs, 100);

	union shm_key key1;
	union shm_key key2;

	key1.the_key = 0;
	key2.the_key = 0;
	key2.reserved = 0;
	key2.reserved = 0;

	key1.type = 3;
	key1.area_type = 10;
	key1.area_idx = 12;

	key2.type = 5;
	key2.service_type = 23;
	key2.service_idx = 45;

//	printf("sub keys: 0x%x, 0x%x\n", key1.sub_key, key2.sub_key);
	printf("keys: 0x%x, 0x%x\n", key1.the_key, key2.the_key);
	printf("key1 type: %d, area_type: %d, area_idx: %d\n", key1.type, key1.area_type, key1.area_idx);
	printf("key2 type: %d, s_type: %d, s_idx: %d\n", key2.type, key2.service_type, key2.service_idx);

	ret_code = vm_create_space(0x1001, 4ULL * 1024 * 1024 * 1024, 0, 8, 512 * 1024 / 8);
	err_exit(ret_code < 0, "failed.");

	ret_code = vm_create_common_allocator(16, 1024, 64);
	err_exit(ret_code < 0, "failed.");

	ret_code = timer_module_load();
	err_exit(ret_code < 0, "failed.");

	ret_code = co_module_load(1024, 8 * 1024);
	err_exit(ret_code < 0, "failed.");

	run_co_test();

//	net_test_server(1);

	co_module_unload();
	timer_module_unload();

	vm_destroy_space();


//	test_lua();

//	test_misc();

//	test_ipc_channel();
//	test_ipc_channel_multi_prod(16, 30000);

//	test_pb();

//	rslt = init_mm(102);
//	if(rslt < 0) goto error_ret;


//	init_timer();

//	test_rpc();

//	test_timer();
//
//	test_co();

//	mm_uninitialize();

//	test_shmm();

//	dbg_zone(1024 * 1024);

//	test_ipc();


//	test_mmcpy();
//
//
//	profile_mmpool();

//	profile_pgpool();

//	test_pgp(50 * 1024 * 1024, 100, 64);

//	profile_slb();

//	test_slb(50 * 1024 * 1024, 100, 64);

//	test_mmp(100 * 1024, 6, 10, 64);
//

//	test_mm();
//	unsigned long r1 = rdtsc();
//	unsigned int aaa = align_to_2power_top(11);
//	unsigned long r2 = rdtsc();
//	printf("aaa = %u\n", aaa);
//	printf("cycle = %lu\n", r2 - r1);
//
//	r1 = rdtsc();
//	aaa = at2t(11);
//	r2 = rdtsc();
//	printf("aaa = %u\n", aaa);
//	printf("cycle = %lu\n", r2 - r1);
//
//
//	test_rbtree(); 
//	
//
	printf("this seed: %lu\n", seed);
//	test_hash(10000, 10000);
//	test_lst();

	return 0;
error_ret:
	return -1;
}
