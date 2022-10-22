.PHONY: default clean

LDLIBS=
CFLAGS=-g -O0

OS_NAME 	= $(shell uname -s)
ST_TARGET 	= linux-debug
ifeq ($(OS_NAME), Darwin)
ST_TARGET	= darwin-debug
CPU_ARCHS 	= $(shell g++ -dM -E - </dev/null |grep -q '__x86_64' && echo x86_64)
CPU_ARCHS 	+= $(shell g++ -dM -E - </dev/null |grep -q '__aarch64' && echo arm64)
CFLAGS      += -arch $(CPU_ARCHS)
endif

default: ./jmpbuf ./jmpbuf.E.txt

./jmpbuf:  jmpbuf.c $(LDLIBS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wall -o $@ $^ $(LDLIBS)

./jmpbuf.E.txt: jmpbuf.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wall -E -o jmpbuf.E.txt $^ $(LDLIBS)

clean:
	rm -rf jmpbuf jmpbuf.E.txt jmpbuf.dSYM

