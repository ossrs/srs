/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2022 Winlin */

#include <stdio.h>
#include <setjmp.h>

int main(int argc, char** argv)
{
    jmp_buf ctx = {0};
    int r0 = setjmp(ctx);

    int nn_jb = sizeof(ctx);
    printf("r0=%d, sizeof(jmp_buf)=%d (unsigned long long [%d])\n", r0, nn_jb, nn_jb/8);
    return 0;
}

