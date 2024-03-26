/*
# !!! ST does not support C++ exceptions on cygwin !!!
g++ exceptions.cpp ../../objs/st/libst.a -g -O0 -o exceptions && ./exceptions
*/
#include <stdio.h>
#include <exception>
#include "../../objs/st/st.h"

int handle_exception() {
    try {
        throw 3;
    } catch (...) {
        return 5;
    }
}

void* foo(void* arg) {
    int r0 = handle_exception();
    printf("r0=%d\n", r0);
    return NULL;
}

int main(int argc, char** argv) {
    st_init();
    st_thread_create(foo, NULL, 0, 0);
    st_thread_exit(NULL);
    return 0;
}

