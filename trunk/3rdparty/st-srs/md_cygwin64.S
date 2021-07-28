
/* If user disable the ASM, such as avoiding bugs in ASM, donot compile it. */
#if !defined(MD_ST_NO_ASM)

#if defined(__amd64__) || defined(__x86_64__)

    /****************************************************************/

    /*
     * Internal __jmp_buf layout
     */
    #define JB_RBX  0
    #define JB_RBP  1
    #define JB_R12  2                   /* R12:R15	Nonvolatile	Must be preserved by callee */
    #define JB_R13  3                   /* @see https://docs.microsoft.com/en-us/cpp/build/x64-software-conventions?view=msvc-160#register-usage */
    #define JB_R14  4                   /* RBX, RBP, RDI, RSI, R12, R14, R14, and R15 must be saved in any function using them. */
    #define JB_R15  5                   /* @see https://software.intel.com/content/www/us/en/develop/articles/introduction-to-x64-assembly.html */
    #define JB_RSP  6
    #define JB_PC   7

    .file "md_cygwin64.S"
    .text

    /* _st_md_cxt_save(__jmp_buf env) */ /* The env is rcx, https://docs.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-160 */
    .globl _st_md_cxt_save
        .align 16
    _st_md_cxt_save:
        /*
         * Save registers.
         */
        movq %rbx, (JB_RBX*8)(%rcx)     /* Save rbx to env[0], *(int64_t*)(rcx+0)=rbx */
        movq %rbp, (JB_RBP*8)(%rcx)     /* Save rbp to env[1], *(int64_t*)(rcx+1)=rbp */
        movq %r12, (JB_R12*8)(%rcx)     /* Save r12 to env[2], *(int64_t*)(rcx+2)=r12 */
        movq %r13, (JB_R13*8)(%rcx)     /* Save r13 to env[3], *(int64_t*)(rcx+3)=r13 */
        movq %r14, (JB_R14*8)(%rcx)     /* Save r14 to env[4], *(int64_t*)(rcx+4)=r14 */
        movq %r15, (JB_R15*8)(%rcx)     /* Save r15 to env[5], *(int64_t*)(rcx+5)=r15 */
        /* Save SP */
        leaq 8(%rsp), %r8               /* Save *(int64_t*)(rsp+8) to r8, https://github.com/ossrs/state-threads/issues/20#issuecomment-887569093 */
        movq %r8, (JB_RSP*8)(%rcx)      /* Save r8(rsp) to env[6], *(int64_t*)(rcx+6)=r8 */
        /* Save PC we are returning to */
        movq (%rsp), %r9               /* Save PC(parent function address) %(rsp) to r9, https://github.com/ossrs/state-threads/issues/20#issuecomment-887569093 */
        movq %r9, (JB_PC*8)(%rcx)      /* Save r9(PC) to env[7], *(int64_t*)(rcx+7)=r9 */
        xorq %rax, %rax                /* Reset rax(return value) to 0 */
        ret


    /****************************************************************/

    /* _st_md_cxt_restore(__jmp_buf env, int val) */ /* The env is rcx, val is edx/rdx, https://docs.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-160 */
    .globl _st_md_cxt_restore
        .align 16
    _st_md_cxt_restore:
        /*
         * Restore registers.
         */
        movq (JB_RBX*8)(%rcx), %rbx     /* Load rbx from env[0] */
        movq (JB_RBP*8)(%rcx), %rbp     /* Load rbp from env[1] */
        movq (JB_R12*8)(%rcx), %r12     /* Load r12 from env[2] */
        movq (JB_R13*8)(%rcx), %r13     /* Load r13 from env[3] */
        movq (JB_R14*8)(%rcx), %r14     /* Load r14 from env[4] */
        movq (JB_R15*8)(%rcx), %r15     /* Load r15 from env[5] */
        /* Set return value */          /* The edx is param1 val, the eax is return value */
        test %edx, %edx                 /*      if (!val) {         */
        mov $01, %eax                   /*          val=1;          */
        cmove %eax, %edx                /*      }                   */
        mov %edx, %eax                  /*      return val;         */
        movq (JB_PC*8)(%rcx), %r8       /* Load r8(PC) from env[7], https://github.com/ossrs/state-threads/issues/20#issuecomment-887569093 */
        movq (JB_RSP*8)(%rcx), %rsp     /* Load rsp from env[6] */
        /* Jump to saved PC */
        jmpq *%r8                       /* Jump to r8(PC) */

    /****************************************************************/

#endif

#endif
