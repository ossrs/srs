#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "public.h"

#define srs_trace(msg, ...)   printf(msg, ##__VA_ARGS__);printf("\n")

int io_port = 1990;
int sleep_ms = 100;

void stack_print(long int previous_sp, int level)
{
    if (level <= 0) {
        return;
    }
    
    register long int rsp asm("sp");
    char buf[level * 1024];
    
    stack_print(rsp, level - 1);
    
    srs_trace("%d. psp=%#lx, sp=%#lx, size=%dB(%dB+%dKB)", 
        level, previous_sp, rsp, (int)(previous_sp - rsp), 
        (int)(previous_sp - rsp - sizeof(buf)), (int)(sizeof(buf) / 1024));
}

int huge_stack_test()
{
    srs_trace("===================================================");
    srs_trace("huge_stack test: start");
    
    register long int rsp asm("sp");
    stack_print(rsp, 10);
    
    srs_trace("huge_stack test: end");
    
    return 0;
}

int sleep_test()
{
    srs_trace("===================================================");
    srs_trace("sleep test: start");
    
    srs_trace("1. sleep...");
    st_usleep(sleep_ms * 1000);
    
    srs_trace("2. sleep ok");
    
    srs_trace("sleep test: end");
    
    return 0;
}

void* thread_func(void* arg)
{
    srs_trace("1. thread run");
    st_usleep(sleep_ms * 1000);
    srs_trace("2. thread completed");
    return NULL;
}

int thread_test()
{
    srs_trace("===================================================");
    srs_trace("thread test: start");
    
    st_thread_t trd = st_thread_create(thread_func, NULL, 1, 0);
    if (trd == NULL) {
        srs_trace("st_thread_create failed");
        return -1;
    }
    
    st_thread_join(trd, NULL);
    srs_trace("3. thread joined");
    
    srs_trace("thread test: end");
    
    return 0;
}

st_mutex_t sync_start = NULL;
st_cond_t sync_cond = NULL;
st_mutex_t sync_mutex = NULL;
st_cond_t sync_end = NULL;

void* sync_master(void* arg)
{
    // wait for main to sync_start this thread.
    st_mutex_lock(sync_start);
    st_mutex_unlock(sync_start);
    
    st_usleep(sleep_ms * 1000);
    st_cond_signal(sync_cond);
    
    st_mutex_lock(sync_mutex);
    srs_trace("2. st mutex is ok");
    st_mutex_unlock(sync_mutex);
    
    st_usleep(sleep_ms * 1000);
    srs_trace("3. st thread is ok");
    st_cond_signal(sync_cond);
    
    return NULL;
}

void* sync_slave(void* arg)
{
    // lock mutex to control thread.
    st_mutex_lock(sync_mutex);
    
    // wait for main to sync_start this thread.
    st_mutex_lock(sync_start);
    st_mutex_unlock(sync_start);
    
    // wait thread to ready.
    st_cond_wait(sync_cond);
    srs_trace("1. st cond is ok");
    
    // release mutex to control thread
    st_usleep(sleep_ms * 1000);
    st_mutex_unlock(sync_mutex);
    
    // wait thread to exit.
    st_cond_wait(sync_cond);
    srs_trace("4. st is ok");
    
    st_cond_signal(sync_end);
    
    return NULL;
}

int sync_test()
{
    srs_trace("===================================================");
    srs_trace("sync test: start");
    
    if ((sync_start = st_mutex_new()) == NULL) {
        srs_trace("st_mutex_new sync_start failed");
        return -1;
    }
    st_mutex_lock(sync_start);

    if ((sync_cond = st_cond_new()) == NULL) {
        srs_trace("st_cond_new cond failed");
        return -1;
    }

    if ((sync_end = st_cond_new()) == NULL) {
        srs_trace("st_cond_new end failed");
        return -1;
    }
    
    if ((sync_mutex = st_mutex_new()) == NULL) {
        srs_trace("st_mutex_new mutex failed");
        return -1;
    }
    
    if (!st_thread_create(sync_master, NULL, 0, 0)) {
        srs_trace("st_thread_create failed");
        return -1;
    }
    
    if (!st_thread_create(sync_slave, NULL, 0, 0)) {
        srs_trace("st_thread_create failed");
        return -1;
    }
    
    // run all threads.
    st_mutex_unlock(sync_start);
    
    st_cond_wait(sync_end);
    srs_trace("sync test: end");
    
    return 0;
}

void* io_client(void* arg)
{
    
    int fd;
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        srs_trace("create linux socket error.");
        return NULL;
    }
    srs_trace("6. client create linux socket success. fd=%d", fd);
    
    st_netfd_t stfd;
    if ((stfd = st_netfd_open_socket(fd)) == NULL){
        srs_trace("st_netfd_open_socket open socket failed.");
        return NULL;
    }
    srs_trace("7. client st open socket success. fd=%d", fd);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(io_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (st_connect(stfd, (const struct sockaddr*)&addr, sizeof(struct sockaddr_in), ST_UTIME_NO_TIMEOUT) == -1) {
        srs_trace("bind socket error.");
        return NULL;
    }
    
    char buf[1024];
    if (st_read_fully(stfd, buf, sizeof(buf), ST_UTIME_NO_TIMEOUT) != sizeof(buf)) {
        srs_trace("st_read_fully failed");
        return NULL;
    }
    if (st_write(stfd, buf, sizeof(buf), ST_UTIME_NO_TIMEOUT) != sizeof(buf)) {
        srs_trace("st_write failed");
        return NULL;
    }
    
    st_netfd_close(stfd);
    
    return NULL;
}

int io_test()
{
    srs_trace("===================================================");
    srs_trace("io test: start, port=%d", io_port);
    
    int fd;
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        srs_trace("create linux socket error.");
        return -1;
    }
    srs_trace("1. server create linux socket success. fd=%d", fd);
    
    int reuse_socket = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_socket, sizeof(int)) == -1) {
        srs_trace("setsockopt reuse-addr error.");
        return -1;
    }
    srs_trace("2. server setsockopt reuse-addr success. fd=%d", fd);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(io_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (const struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1) {
        srs_trace("bind socket error.");
        return -1;
    }
    srs_trace("3. server bind socket success. fd=%d", fd);
    
    if (listen(fd, 10) == -1) {
        srs_trace("listen socket error.");
        return -1;
    }
    srs_trace("4. server listen socket success. fd=%d", fd);
    
    st_netfd_t stfd;
    if ((stfd = st_netfd_open_socket(fd)) == NULL){
        srs_trace("st_netfd_open_socket open socket failed.");
        return -1;
    }
    srs_trace("5. server st open socket success. fd=%d", fd);
    
    if (!st_thread_create(io_client, NULL, 0, 0)) {
        srs_trace("st_thread_create failed");
        return -1;
    }
    
    st_netfd_t client_stfd = st_accept(stfd, NULL, NULL, ST_UTIME_NO_TIMEOUT);
    srs_trace("8. server get a client. fd=%d", st_netfd_fileno(client_stfd));
    
    char buf[1024];
    if (st_write(client_stfd, buf, sizeof(buf), ST_UTIME_NO_TIMEOUT) != sizeof(buf)) {
        srs_trace("st_write failed");
        return -1;
    }
    if (st_read_fully(client_stfd, buf, sizeof(buf), ST_UTIME_NO_TIMEOUT) != sizeof(buf)) {
        srs_trace("st_read_fully failed");
        return -1;
    }
    srs_trace("9. server io completed.");
    
    st_netfd_close(stfd);
    st_netfd_close(client_stfd);
    
    srs_trace("io test: end");
    return 0;
}

int pipe_test()
{
    srs_trace("===================================================");
    srs_trace("pipe test: start");
    
    int fds[2];
    if (pipe(fds) < 0) {
        srs_trace("pipe failed");
        return -1;
    }
    srs_trace("1. pipe ok, %d=>%d", fds[1], fds[0]);
    
    st_netfd_t fdw;
    if ((fdw = st_netfd_open_socket(fds[1])) == NULL) {
        srs_trace("st_netfd_open_socket open socket failed.");
        return -1;
    }
    srs_trace("2. open write fd ok");
    
    st_netfd_t fdr;
    if ((fdr = st_netfd_open_socket(fds[0])) == NULL) {
        srs_trace("st_netfd_open_socket open socket failed.");
        return -1;
    }
    srs_trace("3. open read fd ok");
    
    char buf[1024];
    if (st_write(fdw, buf, sizeof(buf), ST_UTIME_NO_TIMEOUT) < 0) {
        srs_trace("st_write socket failed.");
        return -1;
    }
    srs_trace("4. write to pipe ok");
    
    if (st_read(fdr, buf, sizeof(buf), ST_UTIME_NO_TIMEOUT) < 0) {
        srs_trace("st_read socket failed.");
        return -1;
    }
    srs_trace("5. read from pipe ok");
    
    st_netfd_close(fdw);
    st_netfd_close(fdr);
    
    srs_trace("pipe test: end");
    return 0;
}

int main(int argc, char** argv)
{
    if (st_set_eventsys(ST_EVENTSYS_ALT) < 0) {
        srs_trace("st_set_eventsys failed");
        return -1;
    }
    
    if (st_init() < 0) {
        srs_trace("st_init failed");
        return -1;
    }
    
    if (huge_stack_test() < 0) {
        srs_trace("huge_stack_test failed");
        return -1;
    }
    
    if (sleep_test() < 0) {
        srs_trace("sleep_test failed");
        return -1;
    }
    
    if (thread_test() < 0) {
        srs_trace("thread_test failed");
        return -1;
    }
    
    if (sync_test() < 0) {
        srs_trace("sync_test failed");
        return -1;
    }
    
    if (io_test() < 0) {
        srs_trace("io_test failed");
        return -1;
    }
    
    if (pipe_test() < 0) {
        srs_trace("pipe_test failed");
        return -1;
    }
    
    // cleanup.
    srs_trace("wait for all thread completed");
    st_thread_exit(NULL);
    // the following never enter, 
    // the above code will exit when all thread exit,
    // current is a primordial st-thread, when all thread exit,
    // the st idle thread will exit(0), see _st_idle_thread_start()
    srs_trace("all thread completed");
    
    return 0;
}

