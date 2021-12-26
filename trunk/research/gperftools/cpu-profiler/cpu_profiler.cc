//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
/**
@see: https://gperftools.github.io/gperftools/cpuprofile.html
config srs with gperf(to make gperftools):
    ./configure --with-gperf --jobs=3
set the pprof path if not set:
    export PPROF_PATH=`pwd`/../../../objs/pprof
to do cpu profile:
    make && rm -f ./srs.prof* && env CPUPROFILE=./srs.prof ./cpu_profiler
    $PPROF_PATH --text cpu_profiler ./srs.prof*
to do cpu profile by signal:
    make && rm -f ./srs.prof* && env CPUPROFILE=./srs.prof CPUPROFILESIGNAL=12 ./cpu_profiler
    $PPROF_PATH --text cpu_profiler ./srs.prof*
*/
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

#include <gperftools/profiler.h>

void cpu_profile_imp() {
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 110 * 1024 * 1024; ++j) {
        }
        printf("cpu profile, loop 110M\n");
        printf("press CTRL+C if you want to abort the program.\n");
        sleep(3);
    }
}
void cpu_profile() {
    cpu_profile_imp();
}

void handler(int sig) {
    exit(0);
}
int main(int argc, char** argv) {
    signal(SIGINT, handler);
    
    // must start profiler manually.
    ProfilerStart(NULL);
    
    if (getenv("CPUPROFILESIGNAL")) {
        printf("if specified CPUPROFILESIGNAL, use signal to active it: kill -12 %d\n", getpid());
        sleep(3);
    }
    
    cpu_profile();
    // not neccessary to call stop.
    //ProfilerStop();
    
    return 0;
}

