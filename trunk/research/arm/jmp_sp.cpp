/*
# see: https://github.com/ossrs/srs/issues/190
# see: https://github.com/ossrs/srs/wiki/v1_CN_SrsLinuxArm
    g++ -g -O0 -o jmp_sp jmp_sp.cpp
    arm-linux-gnueabi-g++ -g -o jmp_sp jmp_sp.cpp -static
    arm-linux-gnueabi-strip jmp_sp
*/
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

jmp_buf context;

void do_longjmp()
{
    /**
    the definition of jmp_buf:
        typedef struct __jmp_buf_tag jmp_buf[1];
        struct __jmp_buf_tag {
             __jmp_buf __jmpbuf;
             int __mask_was_saved;
             __sigset_t __saved_mask;
        };
    */
#if defined(__amd64__) || defined(__x86_64__)
    // http://ftp.gnu.org/gnu/glibc/glibc-2.12.2.tar.xz
    // http://ftp.gnu.org/gnu/glibc/glibc-2.12.1.tar.gz
    /*
     * Starting with glibc 2.4, JB_SP definitions are not public anymore.
     * They, however, can still be found in glibc source tree in
     * architecture-specific "jmpbuf-offsets.h" files.
     * Most importantly, the content of jmp_buf is mangled by setjmp to make
     * it completely opaque (the mangling can be disabled by setting the
     * LD_POINTER_GUARD environment variable before application execution).
     * Therefore we will use built-in _st_md_cxt_save/_st_md_cxt_restore
     * functions as a setjmp/longjmp replacement wherever they are available
     * unless USE_LIBC_SETJMP is defined.
     */
    // for glibc 2.4+, it's not possible to get and set the sp in jmp_buf
    /**
    for example, the following is show the jmp_buf when setjmp:
        (gdb) x /64xb context[0].__jmpbuf
        0x600ca0 <context>:	0x00	0x00	0x00	0x00	0x00	0x00	0x00	0x00
        0x600ca8 <context+8>:	0xf8	0xc1	0x71	0xe5	0xa8	0x88	0xb4	0x15
        0x600cb0 <context+16>:	0xa0	0x05	0x40	0x00	0x00	0x00	0x00	0x00
        0x600cb8 <context+24>:	0x90	0xe4	0xff	0xff	0xff	0x7f	0x00	0x00
        0x600cc0 <context+32>:	0x00	0x00	0x00	0x00	0x00	0x00	0x00	0x00
        0x600cc8 <context+40>:	0x00	0x00	0x00	0x00	0x00	0x00	0x00	0x00
        0x600cd0 <context+48>:	0xf8	0xc1	0x51	0xe5	0xa8	0x88	0xb4	0x15
        0x600cd8 <context+56>:	0xf8	0xc1	0xd9	0x2f	0xd7	0x77	0x4b	0xea
        (gdb) p /x $sp
        $4 = 0x7fffffffe380
    we cannot finger the sp out.
    where the glibc is 2.12.
    */
    register long int rsp0 asm("rsp");
    
    int ret = setjmp(context);
    printf("setjmp func0 ret=%d, rsp=%#lx\n", ret, rsp0);
    
    printf("after setjmp: ");
    for (int i = 0; i < 8; i++) {
        printf("env[%d]=%#x, ", i, (int)context[0].__jmpbuf[i]);
    }
    printf("\n");
#endif

#if defined(__arm__)
    /**
        /usr/arm-linux-gnueabi/include/bits/setjmp.h
        #ifndef _ASM
        The exact set of registers saved may depend on the particular core
           in use, as some coprocessor registers may need to be saved.  The C
           Library ABI requires that the buffer be 8-byte aligned, and
           recommends that the buffer contain 64 words.  The first 28 words
           are occupied by v1-v6, sl, fp, sp, pc, d8-d15, and fpscr.  (Note
           that d8-15 require 17 words, due to the use of fstmx.)
        typedef int __jmp_buf[64] __attribute__((__aligned__ (8)));
        
        the layout of setjmp for arm:
            0-5: v1-v6 
            6: sl
            7: fp
            8: sp
            9: pc
            10-26: d8-d15 17words
            27: fpscr
    */
    /**
    For example, on raspberry-pi, armv6 cpu:
        (gdb) x /64xb (char*)context[0].__jmpbuf
            v1, 0:  0x00	0x00	0x00	0x00	
            v2, 1:  0x00	0x00	0x00	0x00
            v3, 2:  0x2c	0x84	0x00	0x00	
            v4, 3:  0x00	0x00	0x00	0x00
            v5, 4:  0x00	0x00	0x00	0x00	
            v6, 5:  0x00	0x00	0x00	0x00
            sl, 6:  0x00	0xf0	0xff	0xb6	
            fp, 7:  0x9c	0xfb	0xff	0xbe
            sp, 8:  0x88	0xfb	0xff	0xbe	
            pc, 9:  0x08	0x85	0x00	0x00
        (gdb) p /x $sp
        $5 = 0xbefffb88
        (gdb) p /x $pc
        $4 = 0x850c
    */
    int ret = setjmp(context);
    printf("setjmp func1 ret=%d\n", ret);
    
    printf("after setjmp: ");
    for (int i = 0; i < 64; i++) {
        printf("env[%d]=%#x, ", i, (int)context[0].__jmpbuf[i]);
    }
    
    printf("func0 terminated\n");
#endif
}

int main(int argc, char** argv) 
{
#if defined(__amd64__) || defined(__x86_64__)
    printf("x86_64 sizeof(long int)=%d, sizeof(long)=%d, "
        "sizeof(int)=%d, __WORDSIZE=%d, __GLIBC__=%d, __GLIBC_MINOR__=%d\n", 
        (int)sizeof(long int), (int)sizeof(long), (int)sizeof(int), 
        (int)__WORDSIZE, (int)__GLIBC__, (int)__GLIBC_MINOR__);
#else
    printf("arm sizeof(long int)=%d, sizeof(long)=%d, "
        "sizeof(int)=%d, __GLIBC__=%d,__GLIBC_MINOR__=%d\n", 
        (int)sizeof(long int), (int)sizeof(long), (int)sizeof(int), 
        (int)__GLIBC__, (int)__GLIBC_MINOR__);
#endif

    do_longjmp();
    
    printf("terminated\n");

    return 0;
}
