/*
g++ hello-world.cpp ../../objs/st/libst.a -g -O0 -o hello-world && ./hello-world
*/
#include <stdio.h>
#include "../../objs/st/st.h"

void foo() {
    st_init();

    for (int i = 0; ; i++) {
        st_sleep(1);
        printf("#%d: main: working\n", i);
    }
}

int main() {
    foo();
    return 0;
}
