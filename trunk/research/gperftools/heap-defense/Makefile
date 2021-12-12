.PHONY: default clean

default: heap_defense

heap_defense: heap_defense.cc Makefile
	g++ -o heap_defense heap_defense.cc -g -O0 -ansi \
        -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free \
        -I../../../objs/gperf/include ../../../objs/gperf/lib/libtcmalloc_debug.a -lpthread
clean:
	rm -rf heap_defense ./*.dSYM
