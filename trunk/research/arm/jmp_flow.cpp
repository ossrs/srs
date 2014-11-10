/*
# for all supports setjmp and longjmp:
    g++ -g -O0 -o jmp_flow jmp_flow.cpp
*/
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

jmp_buf context_level_0;

void func_level_0()
{
    const char* level_0_0 = "stack variables for func_level_0";
    int ret = setjmp(context_level_0);
    printf("func_level_0 ret=%d\n", ret);
    if (ret != 0) {
        printf("call by longjmp.\n");
        exit(0);
    }
}

int main(int argc, char** argv) 
{
    func_level_0();
    longjmp(context_level_0, 1);
    return 0;
}
