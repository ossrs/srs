/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2021-2022 The SRS Authors */

/* If user disable the ASM, such as avoiding bugs in ASM, donot compile it. */
#if !defined(MD_ST_NO_ASM)

#if defined(__amd64__) || defined(__x86_64__)

    /****************************************************************/

    /*
     * Internal __jmp_buf layout
     */
    #define JB_RBX  0
    #define JB_RBP  1
    #define JB_R12  2                   /* The first six integer or pointer arguments are passed in registers RDI, RSI, RDX, RCX, R8, R9. */
    #define JB_R13  3                   /* If the callee wishes to use registers RBX, RSP, RBP, and R12–R15, it must restore their original values before returning control to the caller. */
    #define JB_R14  4                   /* @see https://en.wikipedia.org/wiki/X86_calling_conventions */
    #define JB_R15  5                   /* @see https://www.cnblogs.com/Five100Miles/p/8458561.html */
    #define JB_RSP  6
    #define JB_PC   7

    .file "md_darwin.S"
    .text

    /* _st_md_cxt_save(__jmp_buf env) */ /* The env is rdi, https://en.wikipedia.org/wiki/X86_calling_conventions */
    .globl __st_md_cxt_save
        .align 16
    __st_md_cxt_save:
        /*
         * Save registers.
         */
        movq %rbx, (JB_RBX*8)(%rdi)     /* Save rbx to env[0], *(int64_t*)(rdi+0)=rbx */
        movq %rbp, (JB_RBP*8)(%rdi)     /* Save rbp to env[1], *(int64_t*)(rdi+1)=rbp */
        movq %r12, (JB_R12*8)(%rdi)     /* Save r12 to env[2], *(int64_t*)(rdi+2)=r12 */
        movq %r13, (JB_R13*8)(%rdi)     /* Save r13 to env[3], *(int64_t*)(rdi+3)=r13 */
        movq %r14, (JB_R14*8)(%rdi)     /* Save r14 to env[4], *(int64_t*)(rdi+4)=r14 */
        movq %r15, (JB_R15*8)(%rdi)     /* Save r15 to env[5], *(int64_t*)(rdi+5)=r15 */
        /* Save SP */
        leaq 8(%rsp), %r8              /* Save *(int64_t*)(rsp+8) to r8, https://github.com/ossrs/state-threads/issues/11#issuecomment-888709759 */
        movq %r8, (JB_RSP*8)(%rdi)     /* Save r8(rsp) to env[6], *(int64_t*)(rdi+6)=r8 */
        /* Save PC we are returning to */
        movq (%rsp), %r9               /* Save PC(parent function address) %(rsp) to r9 */
        movq %r9, (JB_PC*8)(%rdi)      /* Save r9(PC) to env[7], *(int64_t*)(rdi+7)=r9 */
        xorq %rax, %rax                 /* Reset rax to 0 */
        ret


    /****************************************************************/

    /* _st_md_cxt_restore(__jmp_buf env, int val) */ /* The env is rdi, val is esi/rsi, https://en.wikipedia.org/wiki/X86_calling_conventions */
    .globl __st_md_cxt_restore
        .align 16
    __st_md_cxt_restore:
        /*
         * Restore registers.
         */
        movq (JB_RBX*8)(%rdi), %rbx     /* Load rbx from env[0] */
        movq (JB_RBP*8)(%rdi), %rbp     /* Load rbp from env[1] */
        movq (JB_R12*8)(%rdi), %r12     /* Load r12 from env[2] */
        movq (JB_R13*8)(%rdi), %r13     /* Load r13 from env[3] */
        movq (JB_R14*8)(%rdi), %r14     /* Load r14 from env[4] */
        movq (JB_R15*8)(%rdi), %r15     /* Load r15 from env[5] */
        /* Set return value */          /* The esi is param1 val, the eax is return value */
        test %esi, %esi                 /*      if (!val) {         */
        mov $01, %eax                   /*          val=1;          */
        cmove %eax, %esi                /*      }                   */
        mov %esi, %eax                  /*      return val;         */
        /* Restore PC and RSP */
        movq (JB_PC*8)(%rdi), %r8      /* Load r8(PC) from env[7] */
        movq (JB_RSP*8)(%rdi), %rsp     /* Load rsp from env[6] */
        /* Jump to saved PC */
        jmpq *%r8                      /* Jump to r8(PC) */

    /****************************************************************/











#elif defined(__aarch64__)

    /****************************************************************/
    /* See https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms */
    /* See https://developer.arm.com/documentation/102374/0100/Function-calls */
    /* See https://developer.arm.com/documentation/102374/0100/Procedure-Call-Standard */
    /* See https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst#machine-registers */
    /* See https://wiki.cdot.senecacollege.ca/wiki/AArch64_Register_and_Instruction_Quick_Start */
    /*
     * See setjmp.h of Darwin.
     *
     * _JBLEN is the number of ints required to save the following:
     * r21-r29, sp, fp, lr == 12 registers, 8 bytes each. d8-d15
     * are another 8 registers, each 8 bytes long. (aapcs64 specifies
     * that only 64-bit versions of FP registers need to be saved).
     * Finally, two 8-byte fields for signal handling purposes.
     */

    /* The called routine is expected to preserve r19-r28 *** These registers are generally
        safe to use in your program. */
    #define JB_X19            0
    #define JB_X20            1
    #define JB_X21            2
    #define JB_X22            3
    #define JB_X23            4
    #define JB_X24            5
    #define JB_X25            6
    #define JB_X26            7
    #define JB_X27            8
    #define JB_X28            9
    /* r29 and r30 are used as the frame register and link register (avoid) */
    #define JB_X29           10
    #define JB_LR            11
    /* Register '31' is one of two registers depending on the instruction context:
        For instructions dealing with the stack, it is the stack pointer, named rsp */
    #define JB_SP   13

    /* FP registers */
    #define JB_D8   14
    #define JB_D9   15
    #define JB_D10   16
    #define JB_D11   17
    #define JB_D12   18
    #define JB_D13   19
    #define JB_D14   20
    #define JB_D15   21

     .file "md.S"
     .text

     /* _st_md_cxt_save(__jmp_buf env) */
     .globl __st_md_cxt_save
     .align 4
    __st_md_cxt_save:
        stp x19, x20, [x0, #JB_X19<<3]
        stp x21, x22, [x0, #JB_X21<<3]
        stp x23, x24, [x0, #JB_X23<<3]
        stp x25, x26, [x0, #JB_X25<<3]
        stp x27, x28, [x0, #JB_X27<<3]
        stp x29, x30, [x0, #JB_X29<<3]

        stp  d8,  d9, [x0, #JB_D8<<3]
        stp d10, d11, [x0, #JB_D10<<3]
        stp d12, d13, [x0, #JB_D12<<3]
        stp d14, d15, [x0, #JB_D14<<3]
        mov x2,  sp
        str x2,  [x0, #JB_SP<<3]

        mov x0, #0
        ret

    /****************************************************************/

     /* _st_md_cxt_restore(__jmp_buf env, int val) */
     .globl __st_md_cxt_restore
     .align 4
    __st_md_cxt_restore:
        ldp x19, x20, [x0, #JB_X19<<3]
        ldp x21, x22, [x0, #JB_X21<<3]
        ldp x23, x24, [x0, #JB_X23<<3]
        ldp x25, x26, [x0, #JB_X25<<3]
        ldp x27, x28, [x0, #JB_X27<<3]

        ldp x29, x30, [x0, #JB_X29<<3]

        ldp  d8,  d9, [x0, #JB_D8<<3]
        ldp d10, d11, [x0, #JB_D10<<3]
        ldp d12, d13, [x0, #JB_D12<<3]
        ldp d14, d15, [x0, #JB_D14<<3]

        ldr x5, [x0, #JB_SP<<3]
        mov sp, x5

        /* x0 = (x1 || 1); */
        cmp x1, #0
        mov x0, #1
        csel x0, x1, x0, ne

        ret

    /****************************************************************/











#endif

#endif