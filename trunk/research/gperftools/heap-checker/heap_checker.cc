/*
The MIT License (MIT)

Copyright (c) 2013-2016 SRS(ossrs)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/**
@see: http://google-perftools.googlecode.com/svn/trunk/doc/heap_checker.html
config srs with gperf(to make gperftools):
    ./configure --with-gperf --jobs=3
set the pprof path if not set:
    export PPROF_PATH=`pwd`/../../../objs/pprof
to check mem leak:
    make && env HEAPCHECK=normal ./heap_checker
*/
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

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

