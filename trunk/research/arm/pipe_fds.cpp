/**
g++ pipe_fds.cpp -g -O0 -o pipe_fds

About the limits:
[winlin@dev6 srs]$ ulimit -n
    1024
[winlin@dev6 srs]$ sudo lsof -p 21182
    pipe_fds 21182 winlin    0u   CHR  136,4      0t0       7 /dev/pts/4
    pipe_fds 21182 winlin    1u   CHR  136,4      0t0       7 /dev/pts/4
    pipe_fds 21182 winlin    2u   CHR  136,4      0t0       7 /dev/pts/4
    pipe_fds 21182 winlin    3r  FIFO    0,8      0t0  464543 pipe
    pipe_fds 21182 winlin 1021r  FIFO    0,8      0t0  465052 pipe
    pipe_fds 21182 winlin 1022w  FIFO    0,8      0t0  465052 pipe
So, all fds can be open is <1024, that is, can open 1023 files.
The 0, 1, 2 is opened file, so can open 1023-3=1020files, 
Where 1020/2=512, so we can open 512 pipes.
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char** argv)
{
    if (argc <= 1) {
        printf("Usage: %s <nb_pipes>\n"
            "   nb_pipes the pipes to open.\n"
            "For example:\n"
            "   %s 1024\n", argv[0], argv[0]);
        exit(-1);
    }
    
    int nb_pipes = ::atoi(argv[1]);
    for (int i = 0; i < nb_pipes; i++) {
        int fds[2];
        if (pipe(fds) < 0) {
            printf("failed to create pipe. i=%d, errno=%d(%s)\n", 
                i, errno, strerror(errno));
            break;
        }
    }
    
    printf("Press CTRL+C to quit, use bellow command to show the fds opened:\n");
    printf("    sudo lsof -p %d\n", getpid());
    sleep(-1);
    return 0;
}
