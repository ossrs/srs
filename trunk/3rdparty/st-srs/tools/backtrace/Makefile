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

# The warning and output flags, the executable suffix, and the files to clean.
WARNFLAGS   = -Wall
LINK_OUT    = -o $(EMPTY)
EXE         =
CLEAN_FILES = backtrace backtrace.dSYM

# Native Windows x64 with MSVC (cl), the WIN64 platform of ../../Makefile. Run GNU make from Git Bash or
# PowerShell with the MSVC environment on PATH, where uname -s is MINGW64_NT-* or MSYS_NT-*. The library
# brings ws2_32.lib itself.
ifneq ($(filter MINGW% MSYS%,$(OS_NAME)),)
LDLIBS      = ../../obj/libst.a
ST_TARGET   = win64-debug
CC          = cl
CFLAGS      = -nologo -Z7 -Od -I../../obj
WARNFLAGS   = -W4
LINK_OUT    = -Fe
EXE         = .exe
CLEAN_FILES += backtrace.exe backtrace.obj backtrace.ilk backtrace.pdb
endif

./backtrace$(EXE): backtrace.c $(LDLIBS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(WARNFLAGS) $(LINK_OUT)$@ $< $(LDLIBS)

clean:
	cd ../.. && make clean
	rm -rf $(CLEAN_FILES)

$(LDLIBS):
	cd ../.. && make $(ST_TARGET)

