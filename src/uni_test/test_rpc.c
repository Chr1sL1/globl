
#include "common_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "core/ipc_channel.h"
#include "core/misc.h"
#include "core/asm.h"
#include "core/co.h"

#pragma pack(1)
struct test_add_req 
{
    int value_a;
    int value_b;
};
struct test_add_rsp
{
    int value_result;
};
#pragma pack()

