cpu_profiler: cpu_profiler.cc Makefile
	g++ -o cpu_profiler cpu_profiler.cc -g -O0 -ansi \
        -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free \
        -I../../../objs/gperf/include ../../../objs/gperf/lib/libtcmalloc_and_profiler.a -lpthread
clean:
	rm -f cpu_profiler ./srs.conf*
