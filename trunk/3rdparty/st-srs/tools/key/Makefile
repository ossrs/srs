.PHONY: clean

LDLIBS=../../obj/libst.a
CFLAGS=-g -O0 -I../../obj -I.. -DDEBUG

OS_NAME 	= $(shell uname -s)
ST_TARGET 	= linux-debug
ifeq ($(OS_NAME), Darwin)
ST_TARGET	= darwin-debug
CPU_ARCHS 	= $(shell g++ -dM -E - </dev/null |grep -q '__x86_64' && echo x86_64)
CPU_ARCHS 	+= $(shell g++ -dM -E - </dev/null |grep -q '__aarch64' && echo arm64)
CFLAGS      += -arch $(CPU_ARCHS)
endif

./key: key.c ../tool.h $(LDLIBS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wall -o $@ $< $(LDLIBS)

clean:
	cd ../.. && make clean
	rm -rf key key.dSYM

$(LDLIBS):
	cd ../.. && make $(ST_TARGET)

