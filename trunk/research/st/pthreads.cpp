/*
Directly compile c++ source and execute:
    g++ pthreads.cpp ../../objs/st/libst.a -g -O0 -o pthreads && ./pthreads
*/
#include <stdio.h>
#include <pthread.h>
#include "../../objs/st/st.h"

void* foo(void* arg) {
    while (true) {
        printf("Hello, child thread\n");
        st_sleep(1);
    }
    return NULL;
}

void* pfn(void* arg) {
    st_init();
    st_thread_create(foo, NULL, 0, 0);
    st_thread_exit(NULL);
    return NULL;
}

int main(int argc, char** argv) {
    st_init();

    pthread_t trd;
    pthread_create(&trd, NULL, pfn, NULL);

    while (true) {
        printf("Hello, main thread\n");
        st_sleep(1);
    }
    return 0;
}

