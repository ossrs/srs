/*
g++ asan-switch.cpp ../../objs/st/libst.a -fsanitize=address -fno-omit-frame-pointer -g -O0 -o asan-switch && ./asan-switch
*/
#include <stdio.h>
#include <string.h>
#include "../../objs/st/st.h"

void* foo(void *args) {
    for (int i = 0; ; i++) {
        st_sleep(1);
        if (i && (i % 3) == 0) {
            char *p = new char[3];
            p[3] = 'H';
        }
        printf("#%d: main: working\n", i);
    }
    return NULL;
}

int main(int argc, char **argv) {
    st_init();
    if (argc > 1) {
        foo(NULL); // Directly call foo() to trigger ASAN.
    } else {
        st_thread_create(foo, NULL, 0, 0);
        st_thread_exit(NULL);
    }
    return 0;
}

