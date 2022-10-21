.PHONY: default clean

LDLIBS=../../obj/libst.a -ldl
CFLAGS=-g -O0 -rdynamic -I../../obj

OS_NAME 	= $(shell uname -s)
ST_TARGET 	= linux-debug
ifeq ($(OS_NAME), Darwin)
ST_TARGET	= darwin-debug
CPU_ARCHS 	= $(shell g++ -dM -E - </dev/null |grep -q '__x86_64' && echo x86_64)
CPU_ARCHS 	+= $(shell g++ -dM -E - </dev/null |grep -q '__aarch64' && echo arm64)
CFLAGS      += -arch $(CPU_ARCHS)
endif

./backtrace: backtrace.c $(LDLIBS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wall -o $@ $^ $(LDLIBS)

clean:
	cd ../.. && make clean
	rm -rf backtrace backtrace.dSYM

$(LDLIBS):
	cd ../.. && make $(ST_TARGET)

