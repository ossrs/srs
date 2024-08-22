/*
g++ hello-thread.cpp ../../objs/st/libst.a -g -O0 -o hello-thread && ./hello-thread
*/
#include <stdio.h>
#include "../../objs/st/st.h"

void* foo(void *args) {
    for (int i = 0; ; i++) {
        st_sleep(1);
        printf("#%d: main: working\n", i);
    }

    return NULL;
}

int main() {
    st_init();
    st_thread_create(foo, NULL, 0, 0);
    st_thread_exit(NULL);
    return 0;
}
