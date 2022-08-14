/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2022 Winlin */

long foo() {
    char c;
    int i;
    long l;
    long long ll;
    return c + i + l + ll;
}

int main(int argc, char** argv)
{
    foo();
    return 0;
}

