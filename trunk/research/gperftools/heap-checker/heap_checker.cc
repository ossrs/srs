//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
/**
@see: https://gperftools.github.io/gperftools/heap_checker.html
config srs with gperf(to make gperftools):
    ./configure --gperf=on --jobs=3
set the pprof path if not set:
    export PPROF_PATH=`pwd`/../../../objs/pprof
to check mem leak:
    make && env HEAPCHECK=normal ./heap_checker
*/
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

#include <gperftools/profiler.h>

void explicit_leak_imp() {
    printf("func leak: do something...\n");
    for (int i = 0; i < 1024; ++i) {
        char* p = new char[1024];
    }
    printf("func leak: memory leaked\n");
}
void explicit_leak() {
    explicit_leak_imp();
}

char* pglobal = NULL;
void global_leak_imp() {
    printf("global leak: do something...\n");
    for (int i = 0; i < 1024; ++i) {
        pglobal = new char[189];
    }
    printf("global leak: memory leaked\n");
}
void global_leak() {
    global_leak_imp();
}

bool loop = true;
void handler(int sig) {
    // we must use signal to notice the main thread to exit normally.
    if (sig == SIGINT) {
        loop = false;
    }
}
int main(int argc, char** argv) {
    signal(SIGINT, handler);
    
    global_leak();
    printf("press CTRL+C if you want to abort the program.\n");
    sleep(3);
    if (!loop) {
        return 0;
    }
    
    explicit_leak();
    printf("press CTRL+C if you want to abort the program.\n");
    sleep(3);
    if (!loop) {
        return 0;
    }

    return 0;
}

