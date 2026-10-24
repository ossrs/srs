#include <st.h>

#include <stdio.h>

static void *sleeper(void *arg)
{
    st_usleep(20 * 1000);
    *(int *)arg = 1;
    return NULL;
}

int main()
{
    if (st_init() != 0) {
        printf("st_init failed\n");
        return 1;
    }

    int done = 0;
    st_utime_t start = st_utime();
    st_thread_t t = st_thread_create(sleeper, &done, 1, 0);
    if (!t || st_thread_join(t, NULL) != 0 || !done) {
        printf("coroutine failed\n");
        return 1;
    }

    printf("st coroutine slept %dms\n", (int)((st_utime() - start) / 1000));
    return 0;
}
