/*
 arm-linux-gnueabi-g++ -g -o jmp_sp jmp_sp.cpp -static
 arm-linux-gnueabi-strip jmp_sp
*/
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

int main(int argc, char** argv) {
#if defined(__amd64__) || defined(__x86_64__)
    printf("x86_64 sizeof(long int)=%d, sizeof(long)=%d, sizeof(int)=%d\n", (int)sizeof(long int), (int)sizeof(long), (int)sizeof(int));
#else
    printf("arm sizeof(long int)=%d, sizeof(long)=%d, sizeof(int)=%d\n", (int)sizeof(long int), (int)sizeof(long), (int)sizeof(int));
#endif
    
    jmp_buf env;
    
    int ret = setjmp(env);
    printf("setjmp func1 ret=%d\n", ret);
    
#if defined(__amd64__) || defined(__x86_64__)
    // typedef lint64_t __jmp_buf[8];
    printf("after setjmp: ");
    for (int i = 0; i < 8; i++) {
        printf("env[%d]=%#x, ", i, (int)env[0].__jmpbuf[i]);
    }
    printf("\n");
#else
    // typedef int32_t __jmp_buf[64] __attribute__((__aligned__ (8)));
    printf("after setjmp: ");
    for (int i = 0; i < 64; i++) {
        printf("env[%d]=%#x, ", i, (int)env[0].__jmpbuf[i]);
    }
    printf("\n");
#endif

    return 0;
}
