/*
g++ huge-threads.cpp ../../objs/st/libst.a -g -O0 -o huge-threads && ./huge-threads 60000
*/
#include <stdio.h>
#include <stdlib.h>
#include "../../objs/st/st.h"

void* pfn(void* arg) {
    char v[32*1024]; // 32KB in stack.
    for (;;) {
        v[0] = v[sizeof(v) - 1] = 0xf;
        st_usleep(1000 * 1000);
    }
    return NULL;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s nn_coroutines [verbose]\n", argv[0]);
        exit(-1);
    }

    st_init();
    int nn = ::atoi(argv[1]);
    printf("pid=%d, create %d coroutines\n", ::getpid(), nn);
    for (int i = 0; i < nn; i++) {
        st_thread_t thread = st_thread_create(pfn, NULL, 1, 0);
        if (!thread) {
            printf("create thread fail, i=%d\n", i);
            return -1;
        }
        if (argc > 2) {
            printf("thread #%d: %p\n", i, thread);
        }
    }

    printf("done\n");
    st_thread_exit(NULL);
    return 0;
}

