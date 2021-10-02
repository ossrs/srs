/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2021 Winlin */

#include <stdio.h>

#include <setjmp.h>
extern int _st_md_cxt_save(jmp_buf env);
extern void _st_md_cxt_restore(jmp_buf env, int val);

void verify_jmpbuf();
void print_buf(unsigned char* p, int nn_jb);

int main(int argc, char** argv)
{
    verify_jmpbuf();
    return 0;
}

#ifdef __linux__
#ifdef __mips__
void verify_jmpbuf()
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

    jmp_buf ctx = {0};
    int r0 = _st_md_cxt_save(ctx);
    if (!r0) {
        _st_md_cxt_restore(ctx, 1); // Restore/Jump to previous line, set r0 to 1.
    }

    printf("sp=%p, ra=%p, gp=%p, s0=%p, s1=%p, s2=%p, s3=%p, s4=%p, s5=%p, s6=%p, s7=%p, fp=%p\n",
        sp, ra, gp, s0, s1, s2, s3, s4, s5, s6, s7, fp);

    int nn_jb = sizeof(ctx[0].__jb);
    unsigned char* p = (unsigned char*)ctx[0].__jb;
    print_buf(p, nn_jb);
}
#endif
#endif

#ifdef __APPLE__
#ifdef __x86_64__
void verify_jmpbuf()
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
    int r0 = _st_md_cxt_save(ctx);
    if (!r0) {
        _st_md_cxt_restore(ctx, 1); // Restore/Jump to previous line, set r0 to 1.
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

    for (int i = 0; i < nn_jb; i++) {
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

