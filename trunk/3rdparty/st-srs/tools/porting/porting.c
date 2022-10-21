/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2022 Winlin */

#include <stdio.h>
#include <setjmp.h>
#include <execinfo.h>
#include <stdlib.h>

void bar()
{
}

void foo()
{
    bar();
}

int main(int argc, char** argv)
{
    printf("OS specs:\n");
#ifdef __linux__
    printf("__linux__: %d\n", __linux__);
#endif
#ifdef __APPLE__
    printf("__APPLE__: %d\n", __APPLE__);
#endif
#ifdef __CYGWIN__
    printf("__CYGWIN__: %d\n", __CYGWIN__);
#endif
#ifdef _WIN32
    printf("_WIN32: %d\n", _WIN32);
#endif

    printf("\nCPU specs:\n");
#ifdef __mips__
    // https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00565-2B-MIPS32-QRC-01.01.pdf
    printf("__mips__: %d, __mips: %d, _MIPSEL: %d\n", __mips__, __mips, _MIPSEL);
#endif
#ifdef __mips64
    printf("__mips64: %d\n", __mips64);
#endif
#ifdef __x86_64__
    printf("__x86_64__: %d\n", __x86_64__);
#endif
#ifdef __loongarch64
    printf("__loongarch__: %d __loongarch64: %d\n", __loongarch__, __loongarch64);
#endif
#ifdef __riscv
    printf("__riscv: %d\n", __riscv);
#endif
#ifdef __arm__
    printf("__arm__: %d\n", __arm__);
#endif
#ifdef __aarch64__
    printf("__aarch64__: %d\n", __aarch64__);
#endif

    printf("\nCompiler specs:\n");
#ifdef __GLIBC__
    printf("__GLIBC__: %d\n", __GLIBC__);
#endif

    printf("\nCalling conventions:\n");
    foo();

    printf("\nCall setjmp:\n");
    jmp_buf ctx;
    if (!setjmp(ctx)) {
        printf("Call longjmp with return=1\n");
        longjmp(ctx, 1);

        // Not reachable code.
        printf("Should never be here.\n");
    }

    printf("\nDone\n");
    return 0;
}

