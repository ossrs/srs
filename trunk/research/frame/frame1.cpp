/*
# https://www.jianshu.com/p/eaa2700ebedc
g++ frame1.cpp -g -O0 -o frame && ./frame
*/
#include <stdio.h>
#include <stdlib.h>

int callee(int a, long b, long c, int d, long e, int f, int g, int h) {
    int v = a;
    v += (int)b;
    v += (int)c;
    v += (int)e;
    v += (int)g;
    v += (int)h;
    return v;
}
void caller() {
    int a = 10;
    int b = 20;
    long c = 30;
    int d = 40;
    int e = 50;
    int f = 60;
    int g = 70;
    int h = 80;
    int v = callee(a, b, c, d, e, f, g, h);
    printf("v=%d, c=%ld\n", v, c);
}

int main(int argc, char** argv)
{
    caller();
    return 0;
}

