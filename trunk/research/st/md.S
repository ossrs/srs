/*
 * Portions created by SGI are Copyright (C) 2000 Silicon Graphics, Inc.
 * All Rights Reserved.
 */

#if defined(__ia64__)

/*
 * The internal __jmp_buf layout is different from one used
 * by setjmp()/longjmp().
 *
 *       Offset   Description
 *       ------   -----------
 *       0x000    stack pointer (r12)
 *       0x008    gp (r1)
 *       0x010    caller's unat
 *       0x018    fpsr
 *       0x020    r4
 *       0x028    r5
 *       0x030    r6
 *       0x038    r7
 *       0x040    rp (b0)
 *       0x048    b1
 *       0x050    b2
 *       0x058    b3
 *       0x060    b4
 *       0x068    b5
 *       0x070    ar.pfs
 *       0x078    ar.lc
 *       0x080    pr
 *       0x088    ar.bsp
 *       0x090    ar.unat
 *       0x098    &__jmp_buf
 *       0x0a0    ar.rsc
 *       0x0a8    ar.rnat
 *       0x0b0     f2
 *       0x0c0     f3
 *       0x0d0     f4
 *       0x0e0     f5
 *       0x0f0    f16
 *       0x100    f17
 *       0x110    f18
 *       0x120    f19
 *       0x130    f20
 *       0x130    f21
 *       0x140    f22
 *       0x150    f23
 *       0x160    f24
 *       0x170    f25
 *       0x180    f26
 *       0x190    f27
 *       0x1a0    f28
 *       0x1b0    f29
 *       0x1c0    f30
 *       0x1d0    f31
 *
 * Note that the address of __jmp_buf is saved but not used: we assume
 * that the jmp_buf data structure is never moved around in memory.
 */

/*
 * Implemented according to "IA-64 Software Conventions and Runtime
 * Architecture Guide", Chapter 10: "Context Management".
 */

        .text
        .psr abi64
        .psr lsb
        .lsb

        /* _st_md_cxt_save(__jmp_buf env) */
        .align 32
        .global _st_md_cxt_save
        .proc _st_md_cxt_save
_st_md_cxt_save:
        alloc r14 = ar.pfs,1,0,0,0
        mov r16 = ar.unat
        ;;
        mov r17 = ar.fpsr
        mov r2 = in0
        add r3 = 8,in0
        ;;
        st8.spill.nta [r2] = sp,16    // r12 (sp)
        ;;
        st8.spill.nta [r3] = gp,16    // r1  (gp)
        ;;
        st8.nta [r2] = r16,16         // save caller's unat
        st8.nta [r3] = r17,16         // save fpsr
        add r8 = 0xb0,in0
        ;;   
        st8.spill.nta [r2] = r4,16    // r4
        ;;
        st8.spill.nta [r3] = r5,16    // r5
        add r9 = 0xc0,in0
        ;;
        stf.spill.nta [r8] = f2,32
        stf.spill.nta [r9] = f3,32
    mov r15 = rp
        ;;
        stf.spill.nta [r8] = f4,32
        stf.spill.nta [r9] = f5,32
        mov r17 = b1
        ;;
        stf.spill.nta [r8] = f16,32
        stf.spill.nta [r9] = f17,32
        mov r18 = b2
        ;;
        stf.spill.nta [r8] = f18,32
        stf.spill.nta [r9] = f19,32
        mov r19 = b3
        ;;
        stf.spill.nta [r8] = f20,32
        stf.spill.nta [r9] = f21,32
        mov r20 = b4
        ;;
        stf.spill.nta [r8] = f22,32
        stf.spill.nta [r9] = f23,32
        mov r21 = b5
        ;;
        stf.spill.nta [r8] = f24,32
        stf.spill.nta [r9] = f25,32
        mov r22 = ar.lc
        ;;
        stf.spill.nta [r8] = f26,32
        stf.spill.nta [r9] = f27,32
        mov r24 = pr
        ;;
        stf.spill.nta [r8] = f28,32
        stf.spill.nta [r9] = f29,32
        ;;
        stf.spill.nta [r8] = f30
        stf.spill.nta [r9] = f31

        st8.spill.nta [r2] = r6,16    // r6
        ;;
        st8.spill.nta [r3] = r7,16    // r7
        ;;
        mov r23 = ar.bsp
        mov r25 = ar.unat

        st8.nta [r2] = r15,16         // b0
        st8.nta [r3] = r17,16         // b1
        ;;
        st8.nta [r2] = r18,16         // b2
        st8.nta [r3] = r19,16         // b3
    mov r26 = ar.rsc
        ;;
        st8.nta [r2] = r20,16         // b4
        st8.nta [r3] = r21,16         // b5
        ;;
        st8.nta [r2] = r14,16         // ar.pfs
        st8.nta [r3] = r22,16         // ar.lc
        ;;
        st8.nta [r2] = r24,16         // pr
        st8.nta [r3] = r23,16         // ar.bsp
        ;;
        st8.nta [r2] = r25,16         // ar.unat
        st8.nta [r3] = in0,16         // &__jmp_buf (just in case)
        ;;
        st8.nta [r2] = r26            // ar.rsc
    ;;
    flushrs                       // flush dirty regs to backing store
        ;;
    and r27 = ~0x3,r26            // clear ar.rsc.mode
    ;;
    mov ar.rsc = r27              // put RSE in enforced lazy mode
    ;;
    mov r28 = ar.rnat
    ;;
    st8.nta [r3] = r28            // ar.rnat
    mov ar.rsc = r26              // restore ar.rsc
    ;;
        mov r8 = 0
        br.ret.sptk.few b0
        .endp _st_md_cxt_save


/****************************************************************/

        /* _st_md_cxt_restore(__jmp_buf env, int val) */
        .global _st_md_cxt_restore
        .proc _st_md_cxt_restore
_st_md_cxt_restore:
        alloc r8 = ar.pfs,2,0,0,0
        add r2 = 0x88,in0             // r2 <- &jmpbuf.ar_bsp
    mov r16 = ar.rsc
    ;;
    flushrs                       // flush dirty regs to backing store
        ;;
    and r17 = ~0x3,r16            // clear ar.rsc.mode
    ;;
    mov ar.rsc = r17              // put RSE in enforced lazy mode
        ;;
    invala                        // invalidate the ALAT
    ;;
        ld8 r23 = [r2],8              // r23 <- jmpbuf.ar_bsp
        ;;
    mov ar.bspstore = r23         // write BSPSTORE
        ld8 r25 = [r2],24             // r25 <- jmpbuf.ar_unat
    ;;
    ld8 r26 = [r2],-8             // r26 <- jmpbuf.ar_rnat
        ;;
    mov ar.rnat = r26             // write RNAT
    ld8 r27 = [r2]                // r27 <- jmpbuf.ar_rsc
    ;;
    mov ar.rsc = r27              // write RSE control
        mov r2 = in0
    ;;
    mov ar.unat = r25             // write ar.unat
    add r3 = 8,in0
        ;;
        ld8.fill.nta sp = [r2],16     // r12 (sp)
        ld8.fill.nta gp = [r3],16     // r1  (gp)
        ;;
        ld8.nta r16 = [r2],16         // caller's unat
        ld8.nta r17 = [r3],16         // fpsr
        ;;
        ld8.fill.nta r4 = [r2],16     // r4
        ld8.fill.nta r5 = [r3],16     // r5
        ;;
        ld8.fill.nta r6 = [r2],16     // r6
        ld8.fill.nta r7 = [r3],16     // r7
        ;;
        mov ar.unat = r16             // restore caller's unat
        mov ar.fpsr = r17             // restore fpsr
        ;;
        ld8.nta r16 = [r2],16         // b0
        ld8.nta r17 = [r3],16         // b1
        ;;
        ld8.nta r18 = [r2],16         // b2
        ld8.nta r19 = [r3],16         // b3
        ;;
        ld8.nta r20 = [r2],16         // b4
        ld8.nta r21 = [r3],16         // b5
        ;;
        ld8.nta r11 = [r2],16         // ar.pfs
        ld8.nta r22 = [r3],72         // ar.lc
        ;;
        ld8.nta r24 = [r2],48         // pr
        mov b0 = r16
        ;;
        ldf.fill.nta f2 = [r2],32
        ldf.fill.nta f3 = [r3],32
        mov b1 = r17
        ;;
        ldf.fill.nta f4 = [r2],32
        ldf.fill.nta f5 = [r3],32
        mov b2 = r18
        ;;
        ldf.fill.nta f16 = [r2],32
        ldf.fill.nta f17 = [r3],32
        mov b3 = r19
        ;;
        ldf.fill.nta f18 = [r2],32
        ldf.fill.nta f19 = [r3],32
        mov b4 = r20
        ;;
        ldf.fill.nta f20 = [r2],32
        ldf.fill.nta f21 = [r3],32
        mov b5 = r21
        ;;
        ldf.fill.nta f22 = [r2],32
        ldf.fill.nta f23 = [r3],32
        mov ar.lc = r22
        ;;
        ldf.fill.nta f24 = [r2],32
        ldf.fill.nta f25 = [r3],32
        cmp.eq p6,p7 = 0,in1
        ;;
        ldf.fill.nta f26 = [r2],32
        ldf.fill.nta f27 = [r3],32
        mov ar.pfs = r11
        ;;
        ldf.fill.nta f28 = [r2],32
        ldf.fill.nta f29 = [r3],32
        ;;
        ldf.fill.nta f30 = [r2]
        ldf.fill.nta f31 = [r3]
(p6)    mov r8 = 1
(p7)    mov r8 = in1

        mov pr = r24,-1
        br.ret.sptk.few b0
        .endp _st_md_cxt_restore

/****************************************************************/

#elif defined(__i386__)

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
1:      jmp *%edx
        .size _st_md_cxt_restore, .-_st_md_cxt_restore

/****************************************************************/

#elif defined(__amd64__) || defined(__x86_64__)

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

