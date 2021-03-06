/*
# https://www.jianshu.com/p/eaa2700ebedc
g++ frame0.cpp -g -O0 -o frame && ./frame
*/
#include <stdio.h>
#include <stdlib.h>

int callee(int a, long b) {
    int c = a;
    c += (int)b;
    return c;
}
void caller() {
    int v = callee(10, 20);
    printf("v=%d\n", v);
}

int main(int argc, char** argv)
{
    caller();
    return 0;
}

