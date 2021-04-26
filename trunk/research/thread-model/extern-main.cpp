/*
g++ -std=c++11 -g -O0 extern-main.cpp extern-extra.cpp -o extern-main
*/
#include <stdio.h>
// @see https://linux.die.net/man/3/pthread_create
#include <pthread.h>

/*
Main: ga=100, gb=1867710016
Thread1: ga=100, gb=1867710016
Thread2: ga=100, gb=200
*/
extern __thread int ga;
extern int gb;

void* pfn(void* arg)
{
    printf("Thread1: ga=%d, gb=%d\n", ga, gb);
    return NULL;
}

extern void* pfn2(void* arg);

int main(int argc, char** argv)
{
    printf("Main: ga=%d, gb=%d\n", ga, gb);

    pthread_t trd = NULL;
    pthread_create(&trd, NULL, pfn, NULL);
    pthread_join(trd, NULL);

    pthread_t trd2 = NULL;
    pthread_create(&trd2, NULL, pfn2, NULL);
    pthread_join(trd2, NULL);
    return 0;
}
