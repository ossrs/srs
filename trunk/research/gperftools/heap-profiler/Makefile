heap_profiler: heap_profiler.cc Makefile
	g++ -o heap_profiler heap_profiler.cc -g -O0 -ansi \
        -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free \
        -I../../../objs/gperf/include ../../../objs/gperf/lib/libtcmalloc_and_profiler.a -lpthread
clean:
	rm -f heap_profiler srs.*.heap
