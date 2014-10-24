/*
# see: https://github.com/winlinvip/simple-rtmp-server/wiki/v1_SrsLinuxArm
 arm-linux-gnueabi-g++ -o jmp jmp.cpp -static
 arm-linux-gnueabi-strip jmp
*/
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

jmp_buf env_func1, env_func2;

int sum = 0;

void func1() {
    int ret = setjmp(env_func1);
    printf("setjmp func1 ret=%d\n", ret);
    
    if (sum <= 0) {
        return;
    }
    
    if (sum++ > 1000) {
        return;
    }
    
    // jmp to func2
    longjmp(env_func2, 3);
}

void func2() {
    int ret = setjmp(env_func2);
    printf("setjmp func2 ret=%d\n", ret);
    
    if (sum <= 0) {
        return;
    }
    
    // jmp to func1
    longjmp(env_func1, 2);
}

int main(int argc, char** argv) {
    printf("hello, setjmp/longjmp!\n");
    func1();
    sum++;
    func2();
    printf("jmp finished, sum=%d\n", sum);
    return 0;
}
