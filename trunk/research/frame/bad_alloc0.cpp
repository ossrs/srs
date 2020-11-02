/*
ulimit -S -v 204800
g++ -g -O0 bad_alloc0.cpp -o bad_alloc && ./bad_alloc
*/
#include <stdio.h>
int main(){
    char* p1 = new char[193000 * 1024]; // huge allocation
    char* p0 = new char[100 * 1024]; // small allocation
    printf("OK\n");
}

