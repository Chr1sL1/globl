#include "common_types.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "core/asm.h"

u64 test_int_div(i32 size)
{
	i32* int_arr1 = (i32*)malloc(size * sizeof(i32));
	i32* int_arr2 = (i32*)malloc(size * sizeof(i32));
	i32* int_arr3 = (i32*)malloc(size * sizeof(i32));
	u64 sum_rdtsc = 0;

	for(i32 i = 0; i < size; ++i)
	{
		int_arr1[i] = random();
		int_arr2[i] = random() + 10;
	}

	u64 r1 = rdtsc();
	for(i32 i = 0; i < size; ++i)
	{
		int_arr3[i] = int_arr1[i] / int_arr2[i];
	}
	u64 r2 = rdtsc() - r1;

	i32 fd = open("result.txt", O_CREAT, 0666);
	if(fd < 0) return -1;

	for(i32 i = 0; i < size; ++i)
	{
		write(fd, &int_arr3[i], sizeof(i32));
	}

	close(fd);

	free(int_arr3);
	free(int_arr2);
	free(int_arr1);

	return r2 / size;
}

i32 test_float_div(i32 size)
{
	f32* int_arr1 = (f32*)malloc(size * sizeof(f32));
	f32* int_arr2 = (f32*)malloc(size * sizeof(f32));
	f32* int_arr3 = (f32*)malloc(size * sizeof(f32));
	u64 sum_rdtsc = 0;

	for(i32 i = 0; i < size; ++i)
	{
		int_arr1[i] = random();
		int_arr2[i] = random() + 10;
	}

	u64 r1 = rdtsc();
	for(i32 i = 0; i < size; ++i)
	{
		int_arr3[i] = int_arr1[i] / int_arr2[i];
	}
	u64 r2 = rdtsc() - r1;

	i32 fd = open("result.txt", O_CREAT, 0666);
	if(fd < 0) return -1;

	for(i32 i = 0; i < size; ++i)
	{
		write(fd, &int_arr3[i], sizeof(i32));
	}

	close(fd);

	free(int_arr3);
	free(int_arr2);
	free(int_arr1);

	return r2 / size;

}


void test_misc(void)
{
	i32 result = test_int_div(1000000);
	if(result > 0)
		printf("test_int_div result: %d\n", result);

	result = test_float_div(1000000);
	if(result > 0)
		printf("test_float_div result: %d\n", result);
}

