# The contents of this file are subject to the Mozilla Public
# License Version 1.1 (the "License"); you may not use this file
# except in compliance with the License. You may obtain a copy of
# the License at http://www.mozilla.org/MPL/
# 
# Software distributed under the License is distributed on an "AS
# IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
# implied. See the License for the specific language governing
# rights and limitations under the License.
# 
# The Original Code is the Netscape Portable Runtime library.
# 
# The Initial Developer of the Original Code is Netscape
# Communications Corporation.  Portions created by Netscape are 
# Copyright (C) 1994-2000 Netscape Communications Corporation.  All
# Rights Reserved.
# 
# Contributor(s):  Silicon Graphics, Inc.
# 
# Portions created by SGI are Copyright (C) 2000-2001 Silicon
# Graphics, Inc.  All Rights Reserved.
# 
# Alternatively, the contents of this file may be used under the
# terms of the GNU General Public License Version 2 or later (the
# "GPL"), in which case the provisions of the GPL are applicable 
# instead of those above.  If you wish to allow use of your 
# version of this file only under the terms of the GPL and not to
# allow others to use your version of this file under the MPL,
# indicate your decision by deleting the provisions above and
# replace them with the notice and other provisions required by
# the GPL.  If you do not delete the provisions above, a recipient
# may use your version of this file under either the MPL or the
# GPL.

# This is the full version of the libst library - modify carefully
VERSION     = 1.9

##########################
# Supported OSes:
#
#OS         = DARWIN
#OS         = LINUX

# Please see the "Other possible defines" section below for
# possible compilation options.
##########################

CC          = cc
AR          = ar
LD          = ld
RANLIB      = ranlib
LN          = ln
STATIC_ONLY = yes

SHELL       = /bin/sh
ECHO        = /bin/echo

BUILD       = DBG
TARGETDIR   = $(OS)_$(shell uname -r)_$(BUILD)

# For Cygwin, it pass a default OS env, we ignore it.
ifeq ($(OS), Windows_NT)
OS =
endif

# For cygwin/windows, the 'uname -r' generate path with parentheses,
# which cause the make fails, so we use 'uname -s' instead.
ifeq ($(OS), CYGWIN64)
TARGETDIR   = $(OS)_$(shell uname -s)_$(BUILD)
endif

DEFINES     = -D$(OS)
CFLAGS      =
SFLAGS      =
ARFLAGS     = -r
LNFLAGS     = -s
DSO_SUFFIX  = so

MAJOR       = $(shell echo $(VERSION) | sed 's/^\([^\.]*\).*/\1/')
DESC        = st.pc

##########################
# Platform section.
# Possible targets:

TARGETS     = darwin-debug darwin-optimized         \
              linux-debug linux-optimized           \
              cygwin64-debug

UTEST_TARGETS = darwin-debug-utest linux-debug-utest \
                darwin-debug-gcov linux-debug-gcov   \
                cygwin64-debug-utest

#
# Platform specifics
#

ifeq ($(OS), DARWIN)
EXTRA_OBJS  = $(TARGETDIR)/md_darwin.o
LD          = cc
SFLAGS      = -fPIC -fno-common
DSO_SUFFIX  = dylib
CFLAGS      += -arch x86_64
LDFLAGS     += -arch x86_64
LDFLAGS     += -dynamiclib -install_name /sw/lib/libst.$(MAJOR).$(DSO_SUFFIX) -compatibility_version $(MAJOR) -current_version $(VERSION)
OTHER_FLAGS = -Wall
DEFINES     += -DMD_HAVE_KQUEUE
endif

ifeq ($(OS), LINUX)
EXTRA_OBJS  = $(TARGETDIR)/md_linux.o
SFLAGS      = -fPIC
LDFLAGS     = -shared -soname=$(SONAME) -lc
OTHER_FLAGS = -Wall
DEFINES     += -DMD_HAVE_EPOLL
endif

ifeq ($(OS), CYGWIN64)
EXTRA_OBJS  = $(TARGETDIR)/md_cygwin64.o
SFLAGS      = -fPIC
DSO_SUFFIX  = dll
LDFLAGS     = -shared -soname=$(SONAME) -lc
OTHER_FLAGS = -Wall
DEFINES     += -DMD_HAVE_SELECT
endif

#
# End of platform section.
##########################


ifeq ($(BUILD), OPT)
OTHER_FLAGS += -O2
else
OTHER_FLAGS += -g -O0
DEFINES     += -DDEBUG
endif

##########################
# Other possible defines:
# To use poll(2) instead of select(2) for events checking:
# DEFINES += -DUSE_POLL
# You may prefer to use select for applications that have many threads
# using one file descriptor, and poll for applications that have many
# different file descriptors.  With USE_POLL poll() is called with at
# least one pollfd per I/O-blocked thread, so 1000 threads sharing one
# descriptor will poll 1000 identical pollfds and select would be more
# efficient.  But if the threads all use different descriptors poll()
# may be better depending on your operating system's implementation of
# poll and select.  Really, it's up to you.  Oh, and on some platforms
# poll() fails with more than a few dozen descriptors.
#
# Some platforms allow to define FD_SETSIZE (if select() is used), e.g.:
# DEFINES += -DFD_SETSIZE=4096
#
# To use malloc(3) instead of mmap(2) for stack allocation:
# DEFINES += -DMALLOC_STACK
#
# To provision more than the default 16 thread-specific-data keys
# (but not too many!):
# DEFINES += -DST_KEYS_MAX=<n>
#
# To start with more than the default 64 initial pollfd slots
# (but the table grows dynamically anyway):
# DEFINES += -DST_MIN_POLLFDS_SIZE=<n>
#
# Note that you can also add these defines by specifying them as
# make/gmake arguments (without editing this Makefile). For example:
#
# make EXTRA_CFLAGS=-DUSE_POLL <target>
#
# (replace make with gmake if needed).
#
# You can also modify the default selection of an alternative event
# notification mechanism. E.g., to enable kqueue(2) support (if it's not
# enabled by default):
#
# gmake EXTRA_CFLAGS=-DMD_HAVE_KQUEUE <target>
#
# or to disable default epoll(4) support:
#
# make EXTRA_CFLAGS=-UMD_HAVE_EPOLL <target>
#
# or to enable stats for ST:
#
# make EXTRA_CFLAGS=-DDEBUG_STATS
#
# or enable the coverage for utest:
# make UTEST_FLAGS="-fprofile-arcs -ftest-coverage"
#
##########################

CFLAGS      += $(DEFINES) $(OTHER_FLAGS) $(EXTRA_CFLAGS)
CFLAGS      += $(UTEST_FLAGS)

OBJS        = $(TARGETDIR)/sched.o \
              $(TARGETDIR)/stk.o   \
              $(TARGETDIR)/sync.o  \
              $(TARGETDIR)/key.o   \
              $(TARGETDIR)/io.o    \
              $(TARGETDIR)/event.o
OBJS        += $(EXTRA_OBJS)
HEADER      = $(TARGETDIR)/st.h
SLIBRARY    = $(TARGETDIR)/libst.a
DLIBRARY    = $(TARGETDIR)/libst.$(DSO_SUFFIX).$(VERSION)

LINKNAME    = libst.$(DSO_SUFFIX)
SONAME      = libst.$(DSO_SUFFIX).$(MAJOR)
FULLNAME    = libst.$(DSO_SUFFIX).$(VERSION)

ifeq ($(OS), DARWIN)
LINKNAME    = libst.$(DSO_SUFFIX)
SONAME      = libst.$(MAJOR).$(DSO_SUFFIX)
FULLNAME    = libst.$(VERSION).$(DSO_SUFFIX)
endif

ifeq ($(STATIC_ONLY), yes)
LIBRARIES   = $(SLIBRARY)
else
LIBRARIES   = $(SLIBRARY) $(DLIBRARY)
endif

ifeq ($(OS),)
ST_ALL      = unknown
else
ST_ALL      = $(TARGETDIR) $(LIBRARIES) $(HEADER) $(DESC)
endif

all: $(ST_ALL)

unknown:
	@echo
	@echo "Please specify one of the following targets:"
	@echo
	@for target in $(TARGETS); do echo $$target; done
	@echo
	@for target in $(UTEST_TARGETS); do echo $$target; done
	@echo

st.pc:	st.pc.in
	sed "s/@VERSION@/${VERSION}/g" < $< > $@

$(TARGETDIR):
	if [ ! -d $(TARGETDIR) ]; then mkdir $(TARGETDIR); fi

$(SLIBRARY): $(OBJS)
	$(AR) $(ARFLAGS) $@ $(OBJS)
	$(RANLIB) $@
	rm -f obj; $(LN) $(LNFLAGS) $(TARGETDIR) obj

$(DLIBRARY): $(OBJS:%.o=%-pic.o)
	$(LD) $(LDFLAGS) $^ -o $@
	if test "$(LINKNAME)"; then                             \
		cd $(TARGETDIR);				\
		rm -f $(SONAME) $(LINKNAME);			\
		$(LN) $(LNFLAGS) $(FULLNAME) $(SONAME);		\
		$(LN) $(LNFLAGS) $(FULLNAME) $(LINKNAME);	\
	fi

$(HEADER): public.h
	rm -f $@
	cp public.h $@

$(TARGETDIR)/md_linux.o: md_linux.S
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGETDIR)/md_darwin.o: md_darwin.S
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGETDIR)/md_cygwin64.o: md_cygwin64.S
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGETDIR)/%.o: %.c common.h md.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf *_OPT *_DBG obj st.pc

##########################
# Pattern rules:

ifneq ($(SFLAGS),)
# Compile with shared library options if it's a C file
$(TARGETDIR)/%-pic.o: %.c common.h md.h
	$(CC) $(CFLAGS) $(SFLAGS) -c $< -o $@
endif

# Compile assembly as normal or C as normal if no SFLAGS
%-pic.o: %.o
	rm -f $@; $(LN) $(LNFLAGS) $(<F) $@

##########################
# Target rules:

darwin-debug:
	$(MAKE) OS="DARWIN" BUILD="DBG"
darwin-optimized:
	$(MAKE) OS="DARWIN" BUILD="OPT"

linux-debug:
	$(MAKE) OS="LINUX" BUILD="DBG"
linux-optimized:
	$(MAKE) OS="LINUX" BUILD="OPT"

cygwin64-debug:
	$(MAKE) OS="CYGWIN64" BUILD="DBG"

darwin-debug-utest:
	@echo "Build utest for state-threads"
	$(MAKE) OS="DARWIN" BUILD="DBG"
	cd utest && $(MAKE)
linux-debug-utest:
	@echo "Build utest for state-threads"
	$(MAKE) OS="LINUX" BUILD="DBG"
	cd utest && $(MAKE)
cygwin64-debug-utest:
	@echo "Build utest for state-threads"
	$(MAKE) OS="CYGWIN64" BUILD="DBG"
	cd utest && $(MAKE) UTEST_FLAGS="-std=gnu++0x" # @see https://www.codenong.com/18784112/

darwin-debug-gcov:
	@echo "Build utest with gcov for state-threads"
	$(MAKE) OS="DARWIN" BUILD="DBG" UTEST_FLAGS="-fprofile-arcs -ftest-coverage" STATIC_ONLY=yes
	cd utest && $(MAKE) UTEST_FLAGS="-fprofile-arcs -ftest-coverage"
linux-debug-gcov:
	@echo "Build utest with gcov for state-threads"
	$(MAKE) OS="LINUX" BUILD="DBG" UTEST_FLAGS="-fprofile-arcs -ftest-coverage" STATIC_ONLY=yes
	cd utest && $(MAKE) UTEST_FLAGS="-fprofile-arcs -ftest-coverage"

##########################

