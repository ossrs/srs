/*
ulimit -S -v 204800
g++ -g -O0 bad_alloc1.cpp -o bad_alloc && ./bad_alloc
*/
#include <stdio.h>
#include <new>
void handler() {
    printf("Memory allocate failed\n");
    std::set_new_handler(NULL); // New will try to alloc again, then abort.
}
int main(){
    std::set_new_handler(handler);
    char* p1 = new char[193000 * 1024]; // huge allocation
    char* p0 = new char[100 * 1024]; // small allocation
    printf("OK\n");
}

