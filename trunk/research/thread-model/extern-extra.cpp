
#include <stdio.h>

int __thread ga = 100;
int __thread gb = 200;

void* pfn2(void* arg)
{
    printf("Thread2: ga=%d, gb=%d\n", ga, gb);
    return NULL;
}


