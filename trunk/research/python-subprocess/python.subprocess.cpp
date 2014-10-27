#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

/**
# always to print to stdout and stderr.
g++ python.subprocess.cpp -o python.subprocess
*/
int main(int argc, char** argv) {
    if (argc <= 2) {
        printf("Usage: <%s> <interval_ms> <max_loop>\n"
            "   %s 50 100000\n", argv[0], argv[0]);
        exit(-1);
        return -1;
    }
    
    int interval_ms = ::atoi(argv[1]);
    int max_loop = ::atoi(argv[2]);
    printf("always to print to stdout and stderr.\n");
    printf("interval: %d ms\n", interval_ms);
    printf("max_loop: %d\n", max_loop);
    
    for (int i = 0; i < max_loop; i++) {
        fprintf(stdout, "always to print to stdout and stderr. interval=%dms, max=%d, current=%d\n", interval_ms, max_loop, i);
        fprintf(stderr, "always to print to stdout and stderr. interval=%dms, max=%d, current=%d\n", interval_ms, max_loop, i);
        if (interval_ms > 0) {
            usleep(interval_ms * 1000);
        }
    }
    
    return 0;
}
