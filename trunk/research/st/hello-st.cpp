/*
g++ hello-st.cpp ../../objs/st/libst.a -g -O0 -o hello-st && ./hello-st
*/
#include <stdio.h>
#include "../../objs/st/st.h"

void foo() {
    st_init();
    st_sleep(1);
    printf("Hello World, ST!\n");
}

int main() {
    foo();
    return 0;
}
