//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
/**
@see: https://gperftools.github.io/gperftools/heapprofile.html
config srs with gperf(to make gperftools):
    ./configure --with-gperf --jobs=3
set the pprof path if not set:
    export PPROF_PATH=`pwd`/../../../objs/pprof
to do mem profile:
    make && rm -f srs.*.heap && env HEAPPROFILE=./srs ./heap_profiler
    $PPROF_PATH --text heap_profiler ./*.heap
*/
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

#include <gperftools/heap-profiler.h>

void memory_alloc_profile_imp() {
    for (int i = 0; i < 2; ++i) {
        char* p = new char[110 * 1024 * 1024];
        for (int j = 0; j < 110 * 1024 * 1024; ++j) {
            p[j] = j;
        }
        printf("mem profile, increase 110MB\n");
        printf("press CTRL+C if you want to abort the program.\n");
        sleep(5);
    }
}
void memory_alloc_profile() {
    memory_alloc_profile_imp();
}

int main(int argc, char** argv) {
    // must start profiler manually.
    HeapProfilerStart(NULL);
    
    memory_alloc_profile();
    // not neccessary to call stop.
    //HeapProfilerStop();
    
    return 0;
}

