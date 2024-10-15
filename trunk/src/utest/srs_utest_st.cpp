//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_st.hpp>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

using namespace std;

VOID TEST(StTest, StUtimeInMicroseconds)
{
	st_utime_t st_time_1 = st_utime();
	// sleep 1 microsecond
#if !defined(SRS_CYGWIN64)
	usleep(1);
#endif
	st_utime_t st_time_2 = st_utime();
	
	EXPECT_GT(st_time_1, 0);
	EXPECT_GT(st_time_2, 0);
	EXPECT_GE(st_time_2, st_time_1);
	// st_time_2 - st_time_1 should be in range of [1, 100] microseconds
	EXPECT_GE(st_time_2 - st_time_1, 0);
	EXPECT_LE(st_time_2 - st_time_1, 100);
}

static inline st_utime_t time_gettimeofday() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000000LL + tv.tv_usec);
}

VOID TEST(StTest, StUtimePerformance)
{
	clock_t start;
	int gettimeofday_elapsed_time = 0;
	int st_utime_elapsed_time = 0;

	// Both the st_utime(clock_gettime or gettimeofday) and gettimeofday's
	// elpased time to execute is dependence on whether it is the first time be called.
	// In general, the gettimeofday has better performance, but the gap between
	// them is really small, maybe less than 10 clock ~ 10 microseconds.

	// check st_utime first, then gettimeofday
	{
		start = clock();
		st_utime_t t2 = st_utime();
		int elapsed_time = clock() - start;
		st_utime_elapsed_time += elapsed_time;
		EXPECT_GT(t2, 0);
		
		start = clock();
		st_utime_t t1 = time_gettimeofday();
		elapsed_time = clock() - start;
		gettimeofday_elapsed_time += elapsed_time;
		EXPECT_GT(t1, 0);


		EXPECT_GE(gettimeofday_elapsed_time, 0);
		EXPECT_GE(st_utime_elapsed_time, 0);

		// pass the test, if 
		EXPECT_LT(gettimeofday_elapsed_time > st_utime_elapsed_time ?
				  gettimeofday_elapsed_time - st_utime_elapsed_time :
				  st_utime_elapsed_time - gettimeofday_elapsed_time, 10);
	}
	
	// check gettimeofday first, then st_utime
	{
		start = clock();
		st_utime_t t1 = time_gettimeofday();
		int elapsed_time = clock() - start;
		gettimeofday_elapsed_time += elapsed_time;
		EXPECT_GT(t1, 0);

		start = clock();
		st_utime_t t2 = st_utime();
		elapsed_time = clock() - start;
		st_utime_elapsed_time += elapsed_time;
		EXPECT_GT(t2, 0);

		EXPECT_GE(gettimeofday_elapsed_time, 0);
		EXPECT_GE(st_utime_elapsed_time, 0);

		EXPECT_LT(gettimeofday_elapsed_time > st_utime_elapsed_time ?
				  gettimeofday_elapsed_time - st_utime_elapsed_time :
				  st_utime_elapsed_time - gettimeofday_elapsed_time, 10);
	}

	// compare st_utime & gettimeofday in a loop
	for (int i = 0; i < 100; i++) {
		    start = clock();
			st_utime_t t2 = st_utime();
			int elapsed_time = clock() - start;
			st_utime_elapsed_time = elapsed_time;
			EXPECT_GT(t2, 0);
			usleep(1);
			
			start = clock();
			st_utime_t t1 = time_gettimeofday();
			elapsed_time = clock() - start;
			gettimeofday_elapsed_time = elapsed_time;
			EXPECT_GT(t1, 0);
			usleep(1);
			
			EXPECT_GE(gettimeofday_elapsed_time, 0);
			EXPECT_GE(st_utime_elapsed_time, 0);

			EXPECT_LT(gettimeofday_elapsed_time > st_utime_elapsed_time ?
					  gettimeofday_elapsed_time - st_utime_elapsed_time :
					  st_utime_elapsed_time - gettimeofday_elapsed_time, 10);

	}
}
