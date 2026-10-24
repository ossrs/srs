#include <srt/srt.h>

#include <stdio.h>

int main()
{
    if (srt_startup() < 0) {
        printf("srt_startup failed: %s\n", srt_getlasterror_str());
        return 1;
    }

    SRTSOCKET s = srt_create_socket();
    if (s == SRT_INVALID_SOCK) {
        printf("srt_create_socket failed: %s\n", srt_getlasterror_str());
        return 1;
    }

    srt_close(s);
    srt_cleanup();
    printf("libsrt %s created a socket\n", SRT_VERSION_STRING);
    return 0;
}
