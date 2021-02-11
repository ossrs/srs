/*
g++ -g -O0 cost.cpp ../../objs/st/libst.a -I../../objs/st -o cost && ./cost | grep COST
*/
#include <stdio.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <st.h>

#define SRS_UTIME_MILLISECONDS 1000
#define srsu2i(us) ((int)(us))
#define srsu2ms(us) ((us) / SRS_UTIME_MILLISECONDS)
#define srsu2msi(us) int((us) / SRS_UTIME_MILLISECONDS)

int64_t srs_update_system_time()
{
    timeval now;
    ::gettimeofday(&now, NULL);
    return ((int64_t)now.tv_sec) * 1000 * 1000 + (int64_t)now.tv_usec;
}

int main(int argc, char** argv)
{
    // The cost for srs_update_system_time() itself.
    if (true) {
        int64_t start = srs_update_system_time();

        int64_t ts_gettimeofday = srs_update_system_time();

        printf("[COST] gettimeofday=%dus\n", srsu2i(ts_gettimeofday - start));
    }

    // The cost for allocate 1MB memory.
    if (true) {
        int64_t start = srs_update_system_time();

        int size = 1024 * 1024;
        char* p = new char[size];
        int64_t ts_allocate = srs_update_system_time();

        for (int i = 0; i < size; i++) {
            p[i] = 0x0F;
        }
        int64_t ts_init = srs_update_system_time();

        printf("[COST] new[%d]=%dus, init=%dus\n",
            size,
            srsu2i(ts_allocate - start),
            srsu2i(ts_init - ts_allocate)
        );
    }

    // The cost for loop.
    if (true) {
        int64_t start = srs_update_system_time();

        for (long long i = 0; i < 1000000LL; i++);
        int64_t ts_loop = srs_update_system_time();

        for (long long i = 0; i < 10000000LL; i++);
        int64_t ts_loop2 = srs_update_system_time();

        printf("[COST] loop 100w=%dus, 1000w=%dus\n", srsu2i(ts_loop - start), srsu2i(ts_loop2 - ts_loop));
    }

    // The cost for printf.
    if (true) {
        int64_t start = srs_update_system_time();

        printf("TEST: OK\n");
        int64_t ts_printf = srs_update_system_time();

        printf("TEST: OK OK\n");
        int64_t ts_printf2 = srs_update_system_time();

        printf("TEST: OK OK %s\n", "OK");
        int64_t ts_printf3 = srs_update_system_time();

        printf("[COST] printf=%dus %dus %dus\n",
            srsu2i(ts_printf - start),
            srsu2i(ts_printf2 - ts_printf),
            srsu2i(ts_printf3 - ts_printf2)
        );
    }

    // The cost for file open or close.
    if (true) {
        int64_t start = srs_update_system_time();

        int fd = ::open("cost.log", O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        int64_t ts_open = srs_update_system_time();

        ::close(fd);
        int64_t ts_close = srs_update_system_time();

        printf("[COST] open=%dus, close=%dus\n",
            srsu2i(ts_open - start),
            srsu2i(ts_close - ts_open)
        );
    }

    // The cost for file writing.
    if (true) {
        int64_t start = srs_update_system_time();

        int fd = ::open("cost.log", O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        int64_t ts_open = srs_update_system_time();

        ::write(fd, "Hello\n", 6);
        int64_t ts_write = srs_update_system_time();

        ::write(fd, "HelloHello\n", 12);
        int64_t ts_write2 = srs_update_system_time();

        ::close(fd);
        int64_t ts_close = srs_update_system_time();

        printf("[COST] write=%dus %dus\n",
            srsu2i(ts_write - ts_open),
            srsu2i(ts_write2 - ts_write)
        );
    }

    // The cost for file reading.
    if (true) {
        char buf[128];
        int64_t start = srs_update_system_time();

        int fd = ::open("cost.log", O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        int64_t ts_open = srs_update_system_time();

        ::read(fd, buf, 6);
        int64_t ts_read = srs_update_system_time();

        ::read(fd, buf, 6);
        int64_t ts_read2 = srs_update_system_time();

        ::close(fd);
        int64_t ts_close = srs_update_system_time();

        printf("[COST] read=%dus %dus\n",
            srsu2i(ts_read - ts_open),
            srsu2i(ts_read2 - ts_read)
        );
    }

    // The cost for ST timer.
    st_set_eventsys(ST_EVENTSYS_ALT);
    st_init();
    for (;;) {
        int64_t start = srs_update_system_time();
        st_usleep(20 * 1000);
        int64_t cost = srs_update_system_time() - start;
        if (cost > (20 + 10) * 1000) {
            printf("[COST] timer=%dms\n", (int)(cost / 1000));
        }
    }

    return 0;
}

