/*
g++ -std=c++11 -g -O0 thread-local.cpp -o thread-local
*/
#include <stdio.h>
// @see https://linux.die.net/man/3/pthread_create
#include <pthread.h>

// Global thread local int variable.
thread_local int tl_g_nn = 0;

// Global thread local variable object.
class MyClass
{
public:
    int tl_nn;
    MyClass(int nn) {
        tl_nn = nn;
    }
};

thread_local MyClass g_obj(0);
thread_local MyClass* gp_obj = new MyClass(0);
thread_local MyClass* gp_obj2 = NULL;
MyClass* get_gp_obj2()
{
    if (!gp_obj2) {
        gp_obj2 = new MyClass(0);
    }
    return gp_obj2;
}

void* pfn(void* arg)
{
    int tid = (int)(long long)arg;
    tl_g_nn += tid;
    g_obj.tl_nn += tid;
    gp_obj->tl_nn += tid;
    get_gp_obj2()->tl_nn += tid;

    printf("PFN%d: tl_g_nn(%p)=%d, g_obj(%p)=%d, gp_obj(%p,%p)=%d, gp_obj2(%p,%p)=%d\n", tid,
        &tl_g_nn, tl_g_nn, &g_obj, g_obj.tl_nn, &gp_obj, gp_obj, gp_obj->tl_nn,
        &gp_obj2, gp_obj2, get_gp_obj2()->tl_nn);
    return NULL;
}

int main(int argc, char** argv)
{
    pthread_t trd = NULL, trd2 = NULL;
    pthread_create(&trd, NULL, pfn, (void*)1);
    pthread_create(&trd2, NULL, pfn, (void*)2);
    pthread_join(trd, NULL);
    pthread_join(trd2, NULL);

    tl_g_nn += 100;
    g_obj.tl_nn += 100;
    gp_obj->tl_nn += 100;
    get_gp_obj2()->tl_nn += 100;

    printf("MAIN: tl_g_nn(%p)=%d, g_obj(%p)=%d, gp_obj(%p,%p)=%d, gp_obj2(%p,%p)=%d\n",
        &tl_g_nn, tl_g_nn, &g_obj, g_obj.tl_nn, &gp_obj, gp_obj, gp_obj->tl_nn,
        &gp_obj2, gp_obj2, get_gp_obj2()->tl_nn);
    return 0;
}

