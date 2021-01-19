/*
g++ mmap0.cpp -g -O0 -o mmap && ./mmap
*/
#include <stdio.h>
#include <assert.h>
#include <sys/mman.h>
#include <string.h>

int main(int argc, char** argv)
{
    int zero_fd = -1;
    int size = 64 * 1024;
    int mmap_flags = MAP_PRIVATE | MAP_ANON;
    char* vaddr = (char*)mmap(NULL, size, PROT_READ | PROT_WRITE, mmap_flags, zero_fd, 0);
    assert (vaddr != (void *)MAP_FAILED);
    printf("vaddr=%p, size=%d\n", vaddr, size);

    vaddr[0] = 0x0f;
    printf("OK: access vaddr p[0]=%x\n", vaddr[0]);

    int REDZONE = 4096;
    mprotect(vaddr, REDZONE, PROT_NONE);
    printf("protect vaddr=%p, [0, %d]\n", vaddr, REDZONE);

    printf("try to access vaddr\n");
    vaddr[0] = 0x0f;
    printf("OK: access vaddr p[0]=%x\n", vaddr[0]);

    return 0;
}

