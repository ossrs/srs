//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
/**
g++ -o ffempty ffempty.cc -g -O0 -ansi
*/

#include <stdio.h>

int main(int argc, char** argv)
{
    for (int i = 0; i < argc; i++) {
        fprintf(stderr, "argv[%d]=%s\n", i, argv[i]);
    }
   
    fprintf(stderr, "summary:\n"); 
    for (int i = 0; i < argc; i++) {
        fprintf(stderr, "%s ", argv[i]);
    }
    fprintf(stderr, "\n");

    return 0;
}

