/*
g++ asan-switch.cpp ../../objs/st/libst.a -fsanitize=address -fno-omit-frame-pointer -g -O0 -o asan-switch && ./asan-switch
*/
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include "../../objs/st/st.h"

extern "C" {
extern void st_set_primordial_stack(void* top, void* bottom);
}

void* foo(void *args) {
    for (int i = 0; ; i++) {
        st_sleep(1);
        if (i && (i % 2) == 0) {
            char *p = new char[3];
            p[3] = 'H';
        }
        printf("#%d: main: working\n", i);
    }
    return NULL;
}

int main(int argc, char **argv) {
    register void* stack_top asm ("sp");
    struct rlimit limit;
    if (getrlimit (RLIMIT_STACK, &limit) == 0) {
        void* stack_bottom = (char*)stack_top - limit.rlim_cur;
        st_set_primordial_stack(stack_top, stack_bottom);
    }

    st_init();
    if (argc > 1) {
        // Directly call foo() to trigger ASAN, call the function in the primordial thread,
        // note that asan can not capther the stack of primordial thread.
        foo(NULL);
    } else {
        st_thread_create(foo, NULL, 0, 0);
        st_thread_exit(NULL);
    }
    return 0;
}

