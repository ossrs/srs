//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
/**
@see: https://blog.csdn.net/win_lin/article/details/50461709
config srs with gperf(to make gperftools):
    ./configure --gperf=on --gmd=on --jobs=3
to check mem corruption:
    make && env TCMALLOC_PAGE_FENCE=1 ./heap_defense
*/
#include <stdio.h>

void foo(char* buf) {
    buf[16] = 0x0f;
}

void bar(char* buf) {
    printf("buf[15]=%#x\n", (unsigned char)buf[15]);
}

int main(int argc, char** argv) {
    char* buf = new char[16];
    foo(buf);
    bar(buf);

    return 0;
}

