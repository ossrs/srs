/*
g++ thread-join.cpp ../../objs/st/libst.a -g -O0 -o thread-join && ./thread-join
*/
#include <stdio.h>
#include <stdlib.h>
#include "../../objs/st/st.h"

void* pfn(void* arg) {
    printf("pid=%d, coroutine is ok\n", ::getpid());
    return NULL;
}

int main(int argc, char** argv) {
    st_init();

    printf("pid=%d, create coroutine #1\n", ::getpid());
    st_thread_t thread = st_thread_create(pfn, NULL, 1, 0);
    st_thread_join(thread, NULL);

    st_usleep(100 * 1000);

    printf("pid=%d, create coroutine #2\n", ::getpid());
    thread = st_thread_create(pfn, NULL, 1, 0);
    st_thread_join(thread, NULL);

    st_usleep(100 * 1000);

    printf("pid=%d, create coroutine #3\n", ::getpid());
    thread = st_thread_create(pfn, NULL, 1, 0);
    st_thread_join(thread, NULL);

    printf("done\n");
    st_thread_exit(NULL);
    return 0;
}

