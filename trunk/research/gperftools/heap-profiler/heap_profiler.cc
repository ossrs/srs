/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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
@see: http://google-perftools.googlecode.com/svn/trunk/doc/heapprofile.html
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

