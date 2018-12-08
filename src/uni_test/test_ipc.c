#include <stdio.h>
#include <string.h>

#include "syslib/ipc_channel.h"
#include "syslib/misc.h"

#pragma pack(1)
struct test_msg
{
	unsigned char v1;
	unsigned short v2;
	unsigned int v3;
	unsigned long v4;

	char msg[17];
};
#pragma pack()

int test_ipc_channel(void)
{
	int rslt;
	struct ipc_local_port* prod_port;
	struct test_msg* msg;
	char* write_buf;
	char read_buf[65535];
	unsigned int read_size, prod_service_type, prod_service_index;

	struct ipc_channel_cfg cfg = 
	{
		.cons_service_type = 1,
		.cons_service_index = 1,
		.message_queue_len = 16,
		.message_count[0 ... MSG_POOL_COUNT - 1] = 4,
	};

	rslt = ipc_channel_create(&cfg);
	err_exit(rslt < 0, "create ipc channel failed.");

	rslt = ipc_open_cons_port(1, 1);
	err_exit(rslt < 0, "open cons port failed.");

	prod_port = ipc_open_prod_port(1, 1);
	err_exit(!prod_port, "open prod port failed.");

	write_buf = ipc_alloc_write_buf_mp(prod_port, sizeof(struct test_msg), 2, 1);
	err_exit(!write_buf, "alloc write buf failed.");

	msg = (struct test_msg*)write_buf;

	msg->v1 = 1;
	msg->v2 = 10000;
	msg->v3 = 11240293;
	msg->v4 = 98394853745098;
	strncpy(msg->msg, "hello, world.", sizeof(msg->msg));

	rslt = ipc_write_mp(prod_port, write_buf);
	err_exit(rslt < 0, "write msg failed.");

	rslt = ipc_read_sc(read_buf, &read_size, &prod_service_type, &prod_service_index);
	err_exit(rslt < 0, "read msg failed.");

	return 0;
error_ret:
	return -1;
}

