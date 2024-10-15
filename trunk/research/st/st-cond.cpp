/*
g++ st-cond.cpp ../../objs/st/libst.a -g -O0 -o st-cond && ./st-cond
*/
#include <stdio.h>
#include "../../objs/st/st.h"

st_cond_t lock;

void* foo(void*) {
    st_cond_wait(lock);
    printf("Hello World, ST!\n");
    return NULL;
}

int main() {
    st_init();
    lock = st_cond_new();

    st_thread_create(foo, NULL, 0, 0);
    st_sleep(1);
    st_cond_signal(lock);
    st_sleep(1);

    st_cond_destroy(lock);
    return 0;
}
