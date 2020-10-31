/*
g++ st0.cpp ../../objs/st/libst.a -g -O0 -o st && ./st
*/
#include <stdio.h>
#include "../../objs/st/st.h"

int main(int argc, char** argv) {
    st_init();
    for (;;) {
        st_usleep(1000 * 1000);
    }
    return 0;
}

