/*
# http://blog.csdn.net/win_lin/article/details/40948277
# for all supports setjmp and longjmp:
    g++ -g -O0 -o jmp_2flow jmp_2flow.cpp
*/
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

jmp_buf context_thread_0;
jmp_buf context_thread_1;

void thread0_functions()
{
    int ret = setjmp(context_thread_0);
    // when ret is 0, create thread,
    // when ret is not 0, longjmp to this thread.
    if (ret == 0) {
        return;
    }
    
    int age = 10000;
    const char* name = "winlin";
    printf("[thread0] age=%d, name=%s\n", age, name);
    if (!setjmp(context_thread_0)) {
        printf("[thread0] switch to thread1\n");
        longjmp(context_thread_1, 1);
    }
    
    // crash, for the stack is modified by thread1.
    // name = 0x2b67004009c8 <error: Cannot access memory at address 0x2b67004009c8>
    printf("[thread0] terminated, age=%d, name=%s\n", age, name);
    exit(0);
}

void thread1_functions()
{
    int ret = setjmp(context_thread_1);
    // when ret is 0, create thread,
    // when ret is not 0, longjmp to this thread.
    if (ret == 0) {
        return;
    }
    
    int age = 11111;
    printf("[thread1] age=%d\n", age);
    if (!setjmp(context_thread_1)) {
        printf("[thread1] switch to thread0\n");
        longjmp(context_thread_0, 1);
    }
    
    printf("[thread1] terminated, age=%d\n", age);
    exit(0);
}

int main(int argc, char** argv) 
{
    thread0_functions();
    thread1_functions();
    
    // kickstart
    longjmp(context_thread_0, 1);
    
    return 0;
}
