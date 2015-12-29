/*
# see: https://github.com/ossrs/srs/wiki/v1_CN_SrsLinuxArm
    g++ -g -O0 -o jmp jmp.cpp
    arm-linux-gnueabi-g++ -o jmp jmp.cpp -static
    arm-linux-gnueabi-strip jmp
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <setjmp.h>

bool func1_ok = false, func2_ok = false;
jmp_buf env_func1, env_func2;

int sum = 0;

void func1() {
    int ret = setjmp(env_func1);
    printf("[func1] setjmp ret=%d, sum++=%d\n", ret, sum++);
    func1_ok = true;
    
    sleep(1);
    
    // jmp to func2
    if (func2_ok) {
        longjmp(env_func2, 1);
    }
}

void func2() {
    int ret = setjmp(env_func2);
    printf("[func2] setjmp ret=%d, sum++=%d\n", ret, sum++);
    func2_ok = true;
    
    sleep(1);
    
    // jmp to func1
    if (func1_ok) {
        longjmp(env_func1, 2);
    }
}

int main(int argc, char** argv) {
    printf("hello, setjmp/longjmp!\n");
    func1();
    func2();
    printf("jmp finished, sum=%d\n", sum);
    return 0;
}
