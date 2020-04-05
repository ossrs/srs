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
#OS         = AIX
#OS         = CYGWIN
#OS         = DARWIN
#OS         = FREEBSD
#OS         = HPUX
#OS         = HPUX_64
#OS         = IRIX
#OS         = IRIX_64
#OS         = LINUX
#OS         = NETBSD
#OS         = OPENBSD
#OS         = OSF1
#OS         = SOLARIS
#OS         = SOLARIS_64

# Please see the "Other possible defines" section below for
# possible compilation options.
##########################

CC          = cc
AR          = ar
LD          = ld
RANLIB      = ranlib
LN          = ln

SHELL       = /bin/sh
ECHO        = /bin/echo

BUILD       = DBG
TARGETDIR   = $(OS)_$(shell uname -r)_$(BUILD)

DEFINES     = -D$(OS)
CFLAGS      =
SFLAGS      =
ARFLAGS     = -rv
LNFLAGS     = -s
DSO_SUFFIX  = so

MAJOR       = $(shell echo $(VERSION) | sed 's/^\([^\.]*\).*/\1/')
DESC        = st.pc

##########################
# Platform section.
# Possible targets:

TARGETS     = aix-debug aix-optimized               \
              cygwin-debug cygwin-optimized         \
              darwin-debug darwin-optimized         \
              freebsd-debug freebsd-optimized       \
              hpux-debug hpux-optimized             \
              hpux-64-debug hpux-64-optimized       \
              irix-n32-debug irix-n32-optimized     \
              irix-64-debug irix-64-optimized       \
              linux-debug linux-optimized           \
              netbsd-debug netbsd-optimized         \
              openbsd-debug openbsd-optimized       \
              osf1-debug osf1-optimized             \
              solaris-debug solaris-optimized       \
              solaris-64-debug solaris-64-optimized

#
# Platform specifics
#

ifeq ($(OS), AIX)
AIX_VERSION = $(shell uname -v).$(shell uname -r)
TARGETDIR   = $(OS)_$(AIX_VERSION)_$(BUILD)
CC          = xlC
STATIC_ONLY = yes
ifeq ($(BUILD), OPT)
OTHER_FLAGS = -w
endif
ifneq ($(filter-out 4.1 4.2, $(AIX_VERSION)),)
DEFINES     += -DMD_HAVE_SOCKLEN_T
endif
endif

ifeq ($(OS), CYGWIN)
TARGETDIR   = $(OS)_$(BUILD)
CC          = gcc
LD          = gcc
DSO_SUFFIX  = dll
SLIBRARY    = $(TARGETDIR)/libst.dll.a
DLIBRARY    = $(TARGETDIR)/libst.dll
DEF_FILE    = $(TARGETDIR)/libst.def
LDFLAGS     = libst.def -shared --enable-auto-image-base -Wl,--output-def,$(DEF_FILE),--out-implib,$(SLIBRARY)
OTHER_FLAGS = -Wall
endif

ifeq ($(OS), DARWIN)
EXTRA_OBJS  = $(TARGETDIR)/md_darwin.o
LD          = cc
SFLAGS      = -fPIC -fno-common
DSO_SUFFIX  = dylib
RELEASE     = $(shell uname -r | cut -d. -f1)
PPC         = $(shell test $(RELEASE) -le 9 && echo yes)
INTEL       = $(shell test $(RELEASE) -ge 9 && echo yes)
ifeq ($(PPC), yes)
CFLAGS      += -arch ppc
LDFLAGS     += -arch ppc
endif
ifeq ($(INTEL), yes)
CFLAGS      += -arch x86_64
LDFLAGS     += -arch x86_64
endif
LDFLAGS     += -dynamiclib -install_name /sw/lib/libst.$(MAJOR).$(DSO_SUFFIX) -compatibility_version $(MAJOR) -current_version $(VERSION)
OTHER_FLAGS = -Wall
endif

ifeq ($(OS), FREEBSD)
SFLAGS      = -fPIC
LDFLAGS     = -shared -soname=$(SONAME) -lc
OTHER_FLAGS = -Wall
ifeq ($(shell test -f /usr/include/sys/event.h && echo yes), yes)
DEFINES     += -DMD_HAVE_KQUEUE
endif
endif

ifeq (HPUX, $(findstring HPUX, $(OS)))
ifeq ($(OS), HPUX_64)
DEFINES     = -DHPUX
CFLAGS      = -Ae +DD64 +Z
else
CFLAGS      = -Ae +DAportable +Z
endif
RANLIB      = true
LDFLAGS     = -b
DSO_SUFFIX  = sl
endif

ifeq (IRIX, $(findstring IRIX, $(OS)))
ifeq ($(OS), IRIX_64)
DEFINES     = -DIRIX
ABIFLAG     = -64
else
ABIFLAG     = -n32
endif
RANLIB      = true
CFLAGS      = $(ABIFLAG) -mips3
LDFLAGS     = $(ABIFLAG) -shared
OTHER_FLAGS = -fullwarn
endif

ifeq ($(OS), LINUX)
EXTRA_OBJS  = $(TARGETDIR)/md.o
SFLAGS      = -fPIC
LDFLAGS     = -shared -soname=$(SONAME) -lc
OTHER_FLAGS = -Wall
ifeq ($(shell test -f /usr/include/sys/epoll.h && echo yes), yes)
DEFINES     += -DMD_HAVE_EPOLL
endif
# For SRS, sendmmsg
ifeq ($(shell grep -qs sendmmsg /usr/include/sys/socket.h && echo yes), yes)
DEFINES     += -DMD_HAVE_SENDMMSG -D_GNU_SOURCE
endif
endif

ifeq ($(OS), NETBSD)
SFLAGS      = -fPIC
LDFLAGS     = -shared -soname=$(SONAME) -lc
OTHER_FLAGS = -Wall
endif

ifeq ($(OS), OPENBSD)
SFLAGS      = -fPIC
LDFLAGS     = -shared -soname=$(SONAME) -lc
OTHER_FLAGS = -Wall
ifeq ($(shell test -f /usr/include/sys/event.h && echo yes), yes)
DEFINES     += -DMD_HAVE_KQUEUE
endif
endif

ifeq ($(OS), OSF1)
RANLIB      = true
LDFLAGS     = -shared -all -expect_unresolved "*"
endif

ifeq (SOLARIS, $(findstring SOLARIS, $(OS)))
TARGETDIR   = $(OS)_$(shell uname -r | sed 's/^5/2/')_$(BUILD)
CC          = gcc
LD          = gcc
RANLIB      = true
LDFLAGS     = -G
OTHER_FLAGS = -Wall
ifeq ($(OS), SOLARIS_64)
DEFINES     = -DSOLARIS
CFLAGS     += -m64
LDFLAGS    += -m64
endif
endif

#
# End of platform section.
##########################


ifeq ($(BUILD), OPT)
OTHER_FLAGS += -O
else
OTHER_FLAGS += -g
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
# or to enable sendmmsg(2) support:
#
# make EXTRA_CFLAGS="-DMD_HAVE_SENDMMSG -D_GNU_SOURCE"
#
##########################

CFLAGS      += $(DEFINES) $(OTHER_FLAGS) $(EXTRA_CFLAGS)

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
EXAMPLES    = examples

LINKNAME    = libst.$(DSO_SUFFIX)
SONAME      = libst.$(DSO_SUFFIX).$(MAJOR)
FULLNAME    = libst.$(DSO_SUFFIX).$(VERSION)

ifeq ($(OS), CYGWIN)
SONAME      = cygst.$(DSO_SUFFIX)
SLIBRARY    = $(TARGETDIR)/libst.dll.a
DLIBRARY    = $(TARGETDIR)/$(SONAME)
LINKNAME    =
# examples directory does not compile under cygwin
EXAMPLES    =
endif

# for SRS
# disable examples for ubuntu crossbuild failed.
# @see https://github.com/winlinvip/simple-rtmp-server/issues/308
ifeq ($(OS), LINUX)
EXAMPLES =
endif

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
ST_ALL      = $(TARGETDIR) $(LIBRARIES) $(HEADER) $(EXAMPLES) $(DESC)
endif

all: $(ST_ALL)

unknown:
	@echo
	@echo "Please specify one of the following targets:"
	@echo
	@for target in $(TARGETS); do echo $$target; done
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

$(TARGETDIR)/md.o: md.S
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGETDIR)/md_darwin.o: md_darwin.S
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGETDIR)/%.o: %.c common.h md.h
	$(CC) $(CFLAGS) -c $< -o $@

examples: $(SLIBRARY)
	@echo Making $@
	@cd $@; $(MAKE) CC="$(CC)" CFLAGS="$(CFLAGS)" OS="$(OS)" TARGETDIR="$(TARGETDIR)"

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

default-debug:
	. ./osguess.sh; $(MAKE) OS="$$OS" BUILD="DBG"
default default-optimized:
	. ./osguess.sh; $(MAKE) OS="$$OS" BUILD="OPT"

aix-debug:
	$(MAKE) OS="AIX" BUILD="DBG"
aix-optimized:
	$(MAKE) OS="AIX" BUILD="OPT"

cygwin-debug:
	$(MAKE) OS="CYGWIN" BUILD="DBG"
cygwin-optimized:
	$(MAKE) OS="CYGWIN" BUILD="OPT"

darwin-debug:
	$(MAKE) OS="DARWIN" BUILD="DBG"
darwin-optimized:
	$(MAKE) OS="DARWIN" BUILD="OPT"

freebsd-debug:
	$(MAKE) OS="FREEBSD" BUILD="DBG"
freebsd-optimized:
	$(MAKE) OS="FREEBSD" BUILD="OPT"

hpux-debug:
	$(MAKE) OS="HPUX" BUILD="DBG"
hpux-optimized:
	$(MAKE) OS="HPUX" BUILD="OPT"
hpux-64-debug:
	$(MAKE) OS="HPUX_64" BUILD="DBG"
hpux-64-optimized:
	$(MAKE) OS="HPUX_64" BUILD="OPT"

irix-n32-debug:
	$(MAKE) OS="IRIX" BUILD="DBG"
irix-n32-optimized:
	$(MAKE) OS="IRIX" BUILD="OPT"
irix-64-debug:
	$(MAKE) OS="IRIX_64" BUILD="DBG"
irix-64-optimized:
	$(MAKE) OS="IRIX_64" BUILD="OPT"

linux-debug:
	$(MAKE) OS="LINUX" BUILD="DBG"
linux-optimized:
	$(MAKE) OS="LINUX" BUILD="OPT"
# compatibility
linux-ia64-debug: linux-debug
linux-ia64-optimized: linux-optimized

netbsd-debug:
	$(MAKE) OS="NETBSD" BUILD="DBG"
netbsd-optimized:
	$(MAKE) OS="NETBSD" BUILD="OPT"

openbsd-debug:
	$(MAKE) OS="OPENBSD" BUILD="DBG"
openbsd-optimized:
	$(MAKE) OS="OPENBSD" BUILD="OPT"

osf1-debug:
	$(MAKE) OS="OSF1" BUILD="DBG"
osf1-optimized:
	$(MAKE) OS="OSF1" BUILD="OPT"

solaris-debug:
	$(MAKE) OS="SOLARIS" BUILD="DBG"
solaris-optimized:
	$(MAKE) OS="SOLARIS" BUILD="OPT"
solaris-64-debug:
	$(MAKE) OS="SOLARIS_64" BUILD="DBG"
solaris-64-optimized:
	$(MAKE) OS="SOLARIS_64" BUILD="OPT"

##########################

