/* SPDX-License-Identifier: MPL-1.1 OR GPL-2.0-or-later */

/* If user disable the ASM, such as avoiding bugs in ASM, donot compile it. */
#if !defined(MD_ST_NO_ASM)

/*
 * Portions created by SGI are Copyright (C) 2000 Silicon Graphics, Inc.
 * All Rights Reserved.
 */

#if defined(__i386__)

    /****************************************************************/

    /*
     * Internal __jmp_buf layout
     */
    #define JB_BX  0
    #define JB_SI  1
    #define JB_DI  2
    #define JB_BP  3
    #define JB_SP  4
    #define JB_PC  5

    .file "md.S"
    .text

    /* _st_md_cxt_save(__jmp_buf env) */
    .globl _st_md_cxt_save
        .type _st_md_cxt_save, @function
        .align 16
    _st_md_cxt_save:
        movl 4(%esp), %eax

        /*
         * Save registers.
         */
        movl %ebx, (JB_BX*4)(%eax)
        movl %esi, (JB_SI*4)(%eax)
        movl %edi, (JB_DI*4)(%eax)
        /* Save SP */
        leal 4(%esp), %ecx
        movl %ecx, (JB_SP*4)(%eax)
        /* Save PC we are returning to */
        movl 0(%esp), %ecx
        movl %ecx, (JB_PC*4)(%eax)
        /* Save caller frame pointer */
        movl %ebp, (JB_BP*4)(%eax)
        xorl %eax, %eax
        ret
    .size _st_md_cxt_save, .-_st_md_cxt_save


    /****************************************************************/

    /* _st_md_cxt_restore(__jmp_buf env, int val) */
    .globl _st_md_cxt_restore
        .type _st_md_cxt_restore, @function
        .align 16
    _st_md_cxt_restore:
        /* First argument is jmp_buf */
        movl 4(%esp), %ecx
        /* Second argument is return value */
        movl 8(%esp), %eax
        /* Set the return address */
        movl (JB_PC*4)(%ecx), %edx
        /*
         * Restore registers.
         */
        movl (JB_BX*4)(%ecx), %ebx
        movl (JB_SI*4)(%ecx), %esi
        movl (JB_DI*4)(%ecx), %edi
        movl (JB_BP*4)(%ecx), %ebp
        movl (JB_SP*4)(%ecx), %esp
        testl %eax, %eax
        jnz  1f
        incl %eax
        /* Jump to saved PC */
        1: jmp *%edx
    .size _st_md_cxt_restore, .-_st_md_cxt_restore

    /****************************************************************/










#elif defined(__amd64__) || defined(__x86_64__)

    /****************************************************************/

    /*
     * Internal __jmp_buf layout
     */
    #define JB_RBX  0
    #define JB_RBP  1
    #define JB_R12  2
    #define JB_R13  3
    #define JB_R14  4
    #define JB_R15  5
    #define JB_RSP  6
    #define JB_PC   7

    .file "md.S"
    .text

    /* _st_md_cxt_save(__jmp_buf env) */
    .globl _st_md_cxt_save
        .type _st_md_cxt_save, @function
        .align 16
    _st_md_cxt_save:
        /*
         * Save registers.
         */
        movq %rbx, (JB_RBX*8)(%rdi)
        movq %rbp, (JB_RBP*8)(%rdi)
        movq %r12, (JB_R12*8)(%rdi)
        movq %r13, (JB_R13*8)(%rdi)
        movq %r14, (JB_R14*8)(%rdi)
        movq %r15, (JB_R15*8)(%rdi)
        /* Save SP */
        leaq 8(%rsp), %rdx
        movq %rdx, (JB_RSP*8)(%rdi)
        /* Save PC we are returning to */
        movq (%rsp), %rax
        movq %rax, (JB_PC*8)(%rdi)
        xorq %rax, %rax
        ret
    .size _st_md_cxt_save, .-_st_md_cxt_save


    /****************************************************************/

    /* _st_md_cxt_restore(__jmp_buf env, int val) */
    .globl _st_md_cxt_restore
        .type _st_md_cxt_restore, @function
        .align 16
    _st_md_cxt_restore:
        /*
         * Restore registers.
         */
        movq (JB_RBX*8)(%rdi), %rbx
        movq (JB_RBP*8)(%rdi), %rbp
        movq (JB_R12*8)(%rdi), %r12
        movq (JB_R13*8)(%rdi), %r13
        movq (JB_R14*8)(%rdi), %r14
        movq (JB_R15*8)(%rdi), %r15
        /* Set return value */
        test %esi, %esi
        mov $01, %eax
        cmove %eax, %esi
        mov %esi, %eax
        movq (JB_PC*8)(%rdi), %rdx
        movq (JB_RSP*8)(%rdi), %rsp
        /* Jump to saved PC */
        jmpq *%rdx
    .size _st_md_cxt_restore, .-_st_md_cxt_restore

    /****************************************************************/

#endif

#endif
