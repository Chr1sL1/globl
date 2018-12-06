#include <stdio.h>
#include "syslib/ipc_channel.h"
#include "syslib/misc.h"

int test_ipc_channel(void)
{
	int rslt;
	struct ipc_local_port* prod_port;

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

	prod_port = ipc_open_prod_port(2, 1);
	err_exit(!prod_port, "open prod port failed.");

	return 0;
error_ret:
	return -1;
}

