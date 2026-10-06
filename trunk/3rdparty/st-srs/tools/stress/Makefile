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

# The warning and output flags, the executable suffix, and the files to clean.
WARNFLAGS   = -Wall
LINK_OUT    = -o $(EMPTY)
EXE         =
CLEAN_FILES = stress stress.dSYM

# Native Windows x64 with MSVC (cl), the WIN64 platform of ../../Makefile. Run GNU make from Git Bash or
# PowerShell with the MSVC environment on PATH, where uname -s is MINGW64_NT-* or MSYS_NT-*. The library
# brings ws2_32.lib itself.
ifneq ($(filter MINGW% MSYS%,$(OS_NAME)),)
ST_TARGET   = win64-debug
CC          = cl
CFLAGS      = -nologo -Z7 -Od -I../../obj -I.. -DDEBUG
WARNFLAGS   = -W4
LINK_OUT    = -Fe
EXE         = .exe
CLEAN_FILES += stress.exe stress.obj stress.ilk stress.pdb
endif

./stress$(EXE): stress.c ../tool.h $(LDLIBS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(WARNFLAGS) $(LINK_OUT)$@ $< $(LDLIBS)

clean:
	cd ../.. && make clean
	rm -rf $(CLEAN_FILES)

$(LDLIBS):
	cd ../.. && make $(ST_TARGET)

