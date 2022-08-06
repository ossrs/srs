/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2021 Winlin */

#include <stdio.h>
#include <setjmp.h>

int foo_return_zero();
int foo_return_one();
int foo_return_one_arg1(int r0);
extern void print_buf(unsigned char* p, int nn_jb);
extern void print_jmpbuf();

int main(int argc, char** argv)
{
    printf("OS specs:\n");
#ifdef __linux__
    printf("__linux__: %d\n", __linux__);
#endif
#ifdef __APPLE__
    printf("__APPLE__: %d\n", __APPLE__);
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
    printf("sizeof(long)=%d\n", (int)sizeof(long));
    printf("sizeof(long long int)=%d\n", (int)sizeof(long long int));
    printf("sizeof(void*)=%d\n", (int)sizeof(void*));
#ifdef __ptr_t
    printf("sizeof(__ptr_t)=%d\n", (int)sizeof(__ptr_t));
#endif

    printf("\nReturn value:\n");
    int r0 = foo_return_zero();
    int r1 = foo_return_one();
    int r2 = foo_return_one_arg1(r1);
    printf("foo_return_zero=%d, foo_return_one=%d, foo_return_one_arg1=%d\n", r0, r1, r2);

    printf("\nCalling conventions:\n");
    print_jmpbuf();

    return 0;
}

int foo_return_zero()
{
    return 0;
}

int foo_return_one()
{
    return 1;
}

int foo_return_one_arg1(int r0)
{
    return r0 + 2;
}

#ifdef __linux__

#if defined(__riscv) || defined(__arm__) || defined(__aarch64__)
void print_jmpbuf() {
}
#elif __mips64
void print_jmpbuf()
{
    // https://en.wikipedia.org/wiki/MIPS_architecture#Calling_conventions
    register void* ra asm("ra");
    register void* gp asm("gp");
    register void* sp asm("sp");
    register void* fp asm("fp");
    // $s0–$s7	$16–$23	saved temporaries
    register void* s0 asm("s0");
    register void* s1 asm("s1");
    register void* s2 asm("s2");
    register void* s3 asm("s3");
    register void* s4 asm("s4");
    register void* s5 asm("s5");
    register void* s6 asm("s6");
    register void* s7 asm("s7");

    /*
    typedef unsigned long long __jmp_buf[13];
    typedef struct __jmp_buf_tag {
         __jmp_buf __jmpbuf;
        int __mask_was_saved;
        __sigset_t __saved_mask;
    } jmp_buf[1];
    */
    jmp_buf ctx = {0};
    int r0 = setjmp(ctx);
    if (!r0) {
        longjmp(ctx, 1);
    }

    printf("ra=%p, sp=%p, s0=%p, s1=%p, s2=%p, s3=%p, s4=%p, s5=%p, s6=%p, s7=%p, fp=%p, gp=%p\n",
        ra, sp, s0, s1, s2, s3, s4, s5, s6, s7, fp, gp);

    int nn_jb = sizeof(ctx[0].__jmpbuf);
    printf("sizeof(jmp_buf)=%d (unsigned long long [%d])\n", nn_jb, nn_jb/8);

    unsigned char* p = (unsigned char*)ctx[0].__jmpbuf;
    print_buf(p, nn_jb);
}
#elif __mips__
void print_jmpbuf()
{
    // https://en.wikipedia.org/wiki/MIPS_architecture#Calling_conventions
    register void* ra asm("ra");
    register void* gp asm("gp");
    register void* sp asm("sp");
    register void* fp asm("fp");
    // $s0–$s7	$16–$23	saved temporaries
    register void* s0 asm("s0");
    register void* s1 asm("s1");
    register void* s2 asm("s2");
    register void* s3 asm("s3");
    register void* s4 asm("s4");
    register void* s5 asm("s5");
    register void* s6 asm("s6");
    register void* s7 asm("s7");

    /*
    typedef unsigned long long __jmp_buf[13];
    typedef struct __jmp_buf_tag {
        __jmp_buf __jb;
        unsigned long __fl;
        unsigned long __ss[128/sizeof(long)];
    } jmp_buf[1];
    */
    jmp_buf ctx = {0};
    int r0 = setjmp(ctx);
    if (!r0) {
        longjmp(ctx, 1);
    }

    printf("ra=%p, sp=%p, s0=%p, s1=%p, s2=%p, s3=%p, s4=%p, s5=%p, s6=%p, s7=%p, fp=%p, gp=%p\n",
        ra, sp, s0, s1, s2, s3, s4, s5, s6, s7, fp, gp);

    int nn_jb = sizeof(ctx[0].__jb);
    printf("sizeof(jmp_buf)=%d (unsigned long long [%d])\n", nn_jb, nn_jb/8);

    unsigned char* p = (unsigned char*)ctx[0].__jb;
    print_buf(p, nn_jb);
}
#elif __loongarch64
void print_jmpbuf()
{
    // https://github.com/ossrs/state-threads/issues/24#porting
    register void* ra asm("r1"); // r1, ra, Return address
    register void* sp asm("r3"); // r3, sp, Stack pointer
    register void* fp asm("r22"); // r22, fp, Frame pointer
    // r23-r31, s0-s8, Subroutine register variable
    register void* s0 asm("r23");
    register void* s1 asm("r24");
    register void* s2 asm("r25");
    register void* s3 asm("r26");
    register void* s4 asm("r27");
    register void* s5 asm("r28");
    register void* s6 asm("r29");
    register void* s7 asm("r30");
    register void* s8 asm("r31");

    /*
    struct __jmp_buf_tag {
        __jmp_buf __jmpbuf;
        int __mask_was_saved;
        __sigset_t __saved_mask;
    };
    typedef struct __jmp_buf_tag jmp_buf[1];
    */
    jmp_buf ctx = {0};
    int r0 = setjmp(ctx);
    if (!r0) {
        longjmp(ctx, 1);
    }

    printf("ra=%p, sp=%p, fp=%p, s0=%p, s1=%p, s2=%p, s3=%p, s4=%p, s5=%p, s6=%p, s7=%p, s7=%p\n",
        ra, sp, fp, s0, s1, s2, s3, s4, s5, s6, s7, s8);

    int nn_jb = sizeof(ctx[0].__jmpbuf);
    printf("sizeof(jmp_buf)=%d (unsigned long long [%d])\n", nn_jb, nn_jb/8);

    unsigned char* p = (unsigned char*)ctx[0].__jmpbuf;
    print_buf(p, nn_jb);
}
#endif
#endif

#ifdef __APPLE__
#ifdef __x86_64__
void print_jmpbuf()
{
    // https://courses.cs.washington.edu/courses/cse378/10au/sections/Section1_recap.pdf
    void *rbx, *rbp, *r12, *r13, *r14, *r15, *rsp;
    __asm__ __volatile__ ("movq %%rbx,%0": "=r"(rbx): /* No input */);
    __asm__ __volatile__ ("movq %%rbp,%0": "=r"(rbp): /* No input */);
    __asm__ __volatile__ ("movq %%r12,%0": "=r"(r12): /* No input */);
    __asm__ __volatile__ ("movq %%r13,%0": "=r"(r13): /* No input */);
    __asm__ __volatile__ ("movq %%r14,%0": "=r"(r14): /* No input */);
    __asm__ __volatile__ ("movq %%r15,%0": "=r"(r15): /* No input */);
    __asm__ __volatile__ ("movq %%rsp,%0": "=r"(rsp): /* No input */);

    printf("rbx=%p, rbp=%p, r12=%p, r13=%p, r14=%p, r15=%p, rsp=%p\n",
        rbx, rbp, r12, r13, r14, r15, rsp);

    jmp_buf ctx = {0};
    int r0 = setjmp(ctx);
    if (!r0) {
        longjmp(ctx, 1);
    }

    int nn_jb = sizeof(ctx);
    printf("sizeof(jmp_buf)=%d (unsigned long long [%d])\n", nn_jb, nn_jb/8);

    unsigned char* p = (unsigned char*)ctx;
    print_buf(p, nn_jb);
}
#endif
#endif

void print_buf(unsigned char* p, int nn_jb)
{
    printf("    ");

    int i;
    for (i = 0; i < nn_jb; i++) {
        printf("0x%02x ", (unsigned char)p[i]);

        int newline = ((i + 1) % sizeof(void*));
        if (!newline || i == nn_jb - 1) {
            printf("\n");
        }

        if (!newline && i < nn_jb - 1) {
            printf("    ");
        }
    }
}

