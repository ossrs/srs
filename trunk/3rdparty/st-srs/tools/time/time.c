/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

/*
 * Time, as SRS reads it with st_utime and waits with st_usleep: the clock
 * increases, sleeps last at least the given time and all wake, the
 * scheduler's last clock moves after a switch, and st_time follows time(NULL)
 * with the time cache on and off. A child process, forked before ST is set
 * up, replaces the clock with st_set_utime_function and checks that sleeps
 * follow it. Windows has no fork, so the child is this program run again with
 * the argument custom-clock.
 */

#include "tool.h"

#include <time.h>
#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#endif

/* Long enough that a sleep still blocked here is a failure, not a slow run. */
#define BLOCK_US (5 * 1000 * 1000)

/* The real monotonic clock, independent of ST. */
static st_utime_t real_us(void)
{
#ifdef _WIN32
    /* Windows has no clock_gettime; the performance counter is its monotonic clock. */
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (st_utime_t)(now.QuadPart / freq.QuadPart) * 1000000LL +
        (st_utime_t)(now.QuadPart % freq.QuadPart) * 1000000LL / freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (st_utime_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
#endif
}

/*
 * The custom clock: the real clock moved 1000 hours ahead, so its values
 * cannot be mistaken for ST's own, plus a jump the child adds to skip time.
 */
#define CLOCK_BASE (1000LL * 3600 * 1000000)
static st_utime_t clock_jump;
static int clock_calls;

static st_utime_t custom_utime(void)
{
    clock_calls++;
    return real_us() + CLOCK_BASE + clock_jump;
}

static int slept;

static void *sleeper(void *arg)
{
    if (st_usleep(*(st_utime_t *)arg) == 0) {
        slept = 1;
    }
    return NULL;
}

/*
 * In the child, before st_init: set the custom clock, then a coroutine sleeps
 * 60 s on it. Jumping the clock 60 s ahead wakes it at once, in real time.
 */
static int custom_clock(void)
{
    CHECK(st_set_utime_function(custom_utime) == 0);
    CHECK(tool_init() == 0);
    CHECK(clock_calls > 0);

    /* After st_init, the clock cannot be changed. */
    errno = 0;
    CHECK(st_set_utime_function(NULL) == -1 && errno == EINVAL);

    st_utime_t u0 = st_utime();
    CHECK(u0 >= CLOCK_BASE);
    CHECK(st_utime_last_clock() >= CLOCK_BASE && st_utime_last_clock() <= u0);

    st_utime_t jump = 60LL * 1000000;
    st_thread_t t = st_thread_create(sleeper, &jump, 1, 0);
    CHECK(t != NULL);
    CHECK(st_usleep(1000) == 0);
    CHECK(!slept);

    st_utime_t r0 = real_us();
    clock_jump += jump;
    /* The idle wait was sized on the old clock, so a short sleep reads the new one. */
    CHECK(st_usleep(1000) == 0);
    CHECK(st_thread_join(t, NULL) == 0);
    CHECK(slept);
    CHECK(real_us() - r0 < BLOCK_US);
    CHECK(st_utime() - u0 >= jump);
    CHECK(st_utime_last_clock() - u0 >= jump);
    return 0;
}

/* st_set_utime_function works only before st_init, so it runs in a child forked before ST is set up. */
static int fork_custom_clock(void)
{
    fflush(stdout);
#ifdef _WIN32
    /* Run this program again as the child, with the same environment, and wait for its exit code. */
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, sizeof(path));
    CHECK(n > 0 && n < sizeof(path));
    CHECK(_spawnl(_P_WAIT, path, "time", "custom-clock", NULL) == 0);
    return 0;
#else
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        int r = custom_clock();
        fflush(stdout);
        _exit(r);
    }

    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
#endif
}

/* The clock never goes back, and moves across a sleep. */
static int utime_increases(void)
{
    st_utime_t prev = st_utime();
    CHECK(prev > 0);
    for (int i = 0; i < 1000; i++) {
        st_utime_t now = st_utime();
        CHECK(now >= prev);
        prev = now;
    }

    CHECK(st_usleep(1000) == 0);
    CHECK(st_utime() > prev);
    return 0;
}

/* Each sleep lasts at least the given time, by ST's clock and the real one; 0 returns at once. */
static int usleep_at_least(void)
{
    CHECK(st_usleep(0) == 0);
    CHECK(st_usleep(ST_UTIME_NO_WAIT) == 0);

    st_utime_t sleeps[] = {1000, 5000, 10000};
    for (int i = 0; i < (int)(sizeof(sleeps) / sizeof(sleeps[0])); i++) {
        st_utime_t u0 = st_utime();
        st_utime_t r0 = real_us();
        CHECK(st_usleep(sleeps[i]) == 0);
        CHECK(st_utime() - u0 >= sleeps[i]);
        CHECK(real_us() - r0 >= sleeps[i]);
    }

    /* st_sleep is st_usleep in whole seconds; 0 returns at once. */
    CHECK(st_sleep(0) == 0);
    return 0;
}

#define NN_SLEEPERS 3
static int nn_woken;

typedef struct {
    st_utime_t us;
    st_utime_t slept;
} sleeper_arg_t;

static void *due_sleeper(void *arg)
{
    sleeper_arg_t *a = (sleeper_arg_t *)arg;
    /* A sleep is due its time after the scheduler's last clock, which may lag st_utime. */
    st_utime_t u0 = st_utime_last_clock();
    if (st_usleep(a->us) == 0) {
        a->slept = st_utime() - u0;
        nn_woken++;
    }
    return NULL;
}

/*
 * Coroutines sleeping at once each wake, none before its own sleep ends. The
 * order is not checked: sleeps that end in the same scheduler pass run in
 * reverse, so a late wake on a slow host reorders them.
 */
static int wake_all(void)
{
    sleeper_arg_t args[NN_SLEEPERS] = {{15000, 0}, {5000, 0}, {10000, 0}};
    st_thread_t threads[NN_SLEEPERS];
    for (int i = 0; i < NN_SLEEPERS; i++) {
        threads[i] = st_thread_create(due_sleeper, &args[i], 1, 0);
        CHECK(threads[i] != NULL);
    }
    for (int i = 0; i < NN_SLEEPERS; i++) {
        CHECK(st_thread_join(threads[i], NULL) == 0);
    }

    CHECK(nn_woken == NN_SLEEPERS);
    for (int i = 0; i < NN_SLEEPERS; i++) {
        CHECK(args[i].slept >= args[i].us);
    }
    return 0;
}

/* The scheduler's last clock moves after a sleep and a yield, and never passes st_utime. */
static int last_clock(void)
{
    st_utime_t l0 = st_utime_last_clock();
    CHECK(l0 > 0 && l0 <= st_utime());

    CHECK(st_usleep(2000) == 0);
    st_utime_t l1 = st_utime_last_clock();
    CHECK(l1 - l0 >= 2000);
    CHECK(l1 <= st_utime());

    /* Without a sleep, the last clock stays until the next switch. */
    while (st_utime() - l1 < 1000) {
    }
    CHECK(st_utime_last_clock() == l1);
    st_thread_yield();
    st_utime_t l2 = st_utime_last_clock();
    CHECK(l2 - l1 >= 1000);
    CHECK(l2 <= st_utime());
    return 0;
}

/* st_time follows time(NULL) with the cache off, and keeps the cached value with it on. */
static int timecache(void)
{
    /* The cache is off by default. */
    time_t t0 = time(NULL);
    time_t s = st_time();
    time_t t1 = time(NULL);
    CHECK(t0 <= s && s <= t1);

    t0 = time(NULL);
    CHECK(st_timecache_set(1) == 0);
    t1 = time(NULL);
    time_t c = st_time();
    CHECK(t0 <= c && c <= t1);

    /* The cache is refreshed about once a second, so it holds across a short sleep. */
    CHECK(st_usleep(5000) == 0);
    CHECK(st_time() == c);

    CHECK(st_timecache_set(1) == 1);
    CHECK(st_timecache_set(0) == 1);
    CHECK(st_timecache_set(0) == 0);

    t0 = time(NULL);
    s = st_time();
    t1 = time(NULL);
    CHECK(t0 <= s && s <= t1);
    return 0;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    /* The child of fork_custom_clock. */
    if (argc == 2 && !strcmp(argv[1], "custom-clock")) {
        int r = custom_clock();
        fflush(stdout);
        return r;
    }
#endif
    CHECK(fork_custom_clock() == 0);

    CHECK(tool_init() == 0);

    /* The parent never set a clock, and after st_init it cannot. */
    errno = 0;
    CHECK(st_set_utime_function(custom_utime) == -1 && errno == EINVAL);
    CHECK(clock_calls == 0);

    CHECK(utime_increases() == 0);
    CHECK(usleep_at_least() == 0);
    CHECK(wake_all() == 0);
    CHECK(last_clock() == 0);
    CHECK(timecache() == 0);

    printf("time OK\n");
    return 0;
}
