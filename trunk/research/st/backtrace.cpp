/*
g++ backtrace.cpp ../../objs/st/libst.a -g -O0 -o backtrace && ./backtrace
*/
#include <stdio.h>
#include "../../objs/st/st.h"

void* pfn(void* arg) {
    for (;;) {
        printf("Hello, coroutine\n");
        st_sleep(1);
    }
    return NULL;
}

int bar(int argc) {
    st_thread_create(pfn, NULL, 0, 0);
    return argc + 1;
}

int foo(int argc) {
    return bar(argc);
}

int main(int argc, char** argv) {
    st_init();
    foo(argc);
    st_thread_exit(NULL);
    return 0;
}