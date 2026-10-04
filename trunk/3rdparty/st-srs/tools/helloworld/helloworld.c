/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <stdio.h>
#include <stdlib.h>

#include <st.h>

// Usage: ./helloworld [count], where count defaults to 10, one line every 10ms.
int main(int argc, char** argv)
{
    int count = 10;
    if (argc > 1 && (count = atoi(argv[1])) <= 0) {
        printf("Invalid count %s\n", argv[1]);
        return 1;
    }

    if (st_init() != 0) {
        printf("st_init failed\n");
        return 1;
    }

    int i;
    for (i = 0; i < count; i++) {
        printf("#%03d, Hello, state-threads world!\n", i);
        if (st_usleep(10 * 1000) != 0) {
            printf("st_usleep failed\n");
            return 1;
        }
    }

    return 0;
}
