
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










#elif defined(__aarch64__)

    /****************************************************************/
    /* https://github.com/ossrs/srs/issues/1282#issuecomment-445539513 */

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
    #define JB_X29           10
    #define JB_LR            11
    #define JB_SP		 13

    #define JB_D8		 14
    #define JB_D9		 15
    #define JB_D10		 16
    #define JB_D11		 17
    #define JB_D12		 18
    #define JB_D13		 19
    #define JB_D14		 20
    #define JB_D15		 21

    	.file "md.S"
    	.text

    	/* _st_md_cxt_save(__jmp_buf env) */
    	.globl _st_md_cxt_save
    	.type _st_md_cxt_save, %function
    	.align 4
    _st_md_cxt_save:
    	stp	x19, x20, [x0, #JB_X19<<3]
    	stp	x21, x22, [x0, #JB_X21<<3]
    	stp	x23, x24, [x0, #JB_X23<<3]
    	stp	x25, x26, [x0, #JB_X25<<3]
    	stp	x27, x28, [x0, #JB_X27<<3]
    	stp	x29, x30, [x0, #JB_X29<<3]

    	stp	 d8,  d9, [x0, #JB_D8<<3]
    	stp	d10, d11, [x0, #JB_D10<<3]
    	stp	d12, d13, [x0, #JB_D12<<3]
    	stp	d14, d15, [x0, #JB_D14<<3]
    	mov	x2,  sp
    	str	x2,  [x0, #JB_SP<<3]

    	mov	x0, #0
    	ret
    	.size _st_md_cxt_save, .-_st_md_cxt_save

    /****************************************************************/

    	/* _st_md_cxt_restore(__jmp_buf env, int val) */
    	.globl _st_md_cxt_restore
    	.type _st_md_cxt_restore, %function
    	.align 4
    _st_md_cxt_restore:
    	ldp	x19, x20, [x0, #JB_X19<<3]
    	ldp	x21, x22, [x0, #JB_X21<<3]
    	ldp	x23, x24, [x0, #JB_X23<<3]
    	ldp	x25, x26, [x0, #JB_X25<<3]
    	ldp	x27, x28, [x0, #JB_X27<<3]

    	ldp	x29, x30, [x0, #JB_X29<<3]

    	ldp	 d8,  d9, [x0, #JB_D8<<3]
    	ldp	d10, d11, [x0, #JB_D10<<3]
    	ldp	d12, d13, [x0, #JB_D12<<3]
    	ldp	d14, d15, [x0, #JB_D14<<3]

    	ldr	x5, [x0, #JB_SP<<3]
    	mov	sp, x5

    	cmp	x1, #0
    	mov	x0, #1
    	csel	x0, x1, x0, ne
    	/* Use br instead of ret because ret is guaranteed to mispredict */
    	br	x30
    	.size _st_md_cxt_restore, .-_st_md_cxt_restore

    /****************************************************************/










#elif defined(__arm__)

    /****************************************************************/
    /* https://github.com/ossrs/srs/issues/1282#issuecomment-445539513 */

    /* Register list for a ldm/stm instruction to load/store
       the general registers from a __jmp_buf.  */
    # define JMP_BUF_REGLIST	{v1-v6, sl, fp, sp, lr}

    	.file "md.S"
    	.text

    	/* _st_md_cxt_save(__jmp_buf env) */
    	.globl _st_md_cxt_save
    	.type _st_md_cxt_save, %function
    	.align 2
    _st_md_cxt_save:
    	mov	ip, r0

    	/* Save registers */
    	stmia	ip!, JMP_BUF_REGLIST

    #ifdef __VFP_FP__
    	/* Store the VFP registers.  */
    	/* Following instruction is vstmia ip!, {d8-d15}.  */
    	stc	p11, cr8, [ip], #64
    #endif

    #ifdef __IWMMXT__
    	/* Save the call-preserved iWMMXt registers.  */
    	/* Following instructions are wstrd wr10, [ip], #8 (etc.)  */
    	stcl	p1, cr10, [r12], #8
    	stcl	p1, cr11, [r12], #8
    	stcl	p1, cr12, [r12], #8
    	stcl	p1, cr13, [r12], #8
    	stcl	p1, cr14, [r12], #8
    	stcl	p1, cr15, [r12], #8
    #endif

    	mov	r0, #0
    	bx	lr

    	.size _st_md_cxt_save, .-_st_md_cxt_save

    /****************************************************************/

    	/* _st_md_cxt_restore(__jmp_buf env, int val) */
    	.globl _st_md_cxt_restore
    	.type _st_md_cxt_restore, %function
    	.align 2
    _st_md_cxt_restore:
    	mov	ip, r0

    	/* Restore registers */
    	ldmia	ip!, JMP_BUF_REGLIST

    #ifdef __VFP_FP__
    	/* Restore the VFP registers.  */
    	/* Following instruction is vldmia ip!, {d8-d15}.  */
    	ldc	p11, cr8, [r12], #64
    #endif

    #ifdef __IWMMXT__
    	/* Restore the call-preserved iWMMXt registers.  */
    	/* Following instructions are wldrd wr10, [ip], #8 (etc.)  */
    	ldcl	p1, cr10, [r12], #8
    	ldcl	p1, cr11, [r12], #8
    	ldcl	p1, cr12, [r12], #8
    	ldcl	p1, cr13, [r12], #8
    	ldcl	p1, cr14, [r12], #8
    	ldcl	p1, cr15, [r12], #8
    #endif

    	movs	r0, r1		/* get the return value in place */
    	moveq	r0, #1		/* can't let setjmp() return zero! */
    	bx	lr

    	.size _st_md_cxt_restore, .-_st_md_cxt_restore

    /****************************************************************/

#endif

#endif
