/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2022 Winlin */

void foo() {
}

void foo2(char a) {
}

void foo3(int a)  {
}

void foo4(long a) {
}

void foo5(long long a) {
}

long foo6(long a) {
    return a + 1;
}

// Note: Use b *main to set to the first instruction of main,
// see https://stackoverflow.com/questions/40960758/break-main-vs-break-main-in-gdb
int main(int argc, char** argv)
{
    foo();
    foo2('s');
    foo3(0x7);
    foo4(0x7);
    foo5(0x7);
    foo6(0x7);
    return 0;
}

