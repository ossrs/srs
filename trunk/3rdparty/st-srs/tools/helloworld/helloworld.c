/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2022 Winlin */

#include <stdio.h>

#include <st.h>

int main(int argc, char** argv)
{
    st_init();

    int i;
    for (i = 0; i < 10000; i++) {
        printf("#%03d, Hello, state-threads world!\n", i);
        st_sleep(1);
    }

    return 0;
}

