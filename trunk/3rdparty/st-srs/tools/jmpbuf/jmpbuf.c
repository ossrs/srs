/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2022 Winlin */

#include <stdio.h>
#include <setjmp.h>

/* We define the jmpbuf, because the system's is different in different OS */
typedef struct _st_jmp_buf {
    /*
     *   OS         CPU                  SIZE
     * Darwin   __amd64__/__x86_64__    long[8]
     * Darwin   __aarch64__             long[22]
     * Linux    __i386__                long[6]
     * Linux    __amd64__/__x86_64__    long[8]
     * Linux    __aarch64__             long[22]
     * Linux    __arm__                 long[16]
     * Linux    __mips__/__mips64       long[13]
     * Linux    __riscv                 long[14]
     * Linux    __loongarch64           long[12]
     * Cygwin64 __amd64__/__x86_64__    long[8]
     */
    long __jmpbuf[22];
} _st_jmp_buf_t[1];

int main(int argc, char** argv)
{
    jmp_buf ctx = {0};
    int r0 = setjmp(ctx);
    int nn_jb = sizeof(ctx);
    printf("jmp_buf: r0=%d, sizeof(jmp_buf)=%d (unsigned long long [%d])\n", r0, nn_jb, nn_jb/8);

    _st_jmp_buf_t ctx2 = {0};
    int r1 = sizeof(_st_jmp_buf_t);
    int r2 = sizeof(ctx2);
    printf("_st_jmp_buf_t: sizeof(_st_jmp_buf_t)=%d/%d (unsigned long long [%d])\n", r1, r2, r2/8);

    return 0;
}

