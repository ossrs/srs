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

##########################
# Target dir and cc:
CC          = cc
TARGETDIR   = objs

##########################
# Supported OSes:
OS         = LINUX

ifneq ($(shell test -f /usr/include/sys/epoll.h && echo yes), yes)
default:
	@echo "epoll not found"
	@exit 1
endif

EXTRA_OBJS  = $(TARGETDIR)/md.o

CFLAGS      =
OTHER_FLAGS += -Wall -g -O0
DEFINES     = -D$(OS) -DDEBUG -DMD_HAVE_EPOLL -DMALLOC_STACK

##########################
# Other possible defines:
# To use malloc(3) instead of mmap(2) for stack allocation:
# DEFINES += -DMALLOC_STACK
#
# To provision more than the default 16 thread-specific-data keys
# (but not too many!):
# DEFINES += -DST_KEYS_MAX=<n>
#
# Note that you can also add these defines by specifying them as
# make/gmake arguments (without editing this Makefile). For example:
#
# make EXTRA_CFLAGS=-UMD_HAVE_EPOLL <target>
#
##########################

CFLAGS      += $(DEFINES) $(OTHER_FLAGS) $(EXTRA_CFLAGS)

OBJS        = $(TARGETDIR)/sched.o 	\
              $(TARGETDIR)/stk.o   	\
              $(TARGETDIR)/sync.o  	\
              $(TARGETDIR)/key.o   	\
              $(TARGETDIR)/io.o    	\
              $(TARGETDIR)/event.o	\
              $(TARGETDIR)/srs.o
OBJS        += $(EXTRA_OBJS)
SRS    		= $(TARGETDIR)/srs

linux-debug: all
all: $(TARGETDIR) $(SRS)

$(TARGETDIR):
	if [ ! -d $(TARGETDIR) ]; then mkdir $(TARGETDIR); fi

$(SRS): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(TARGETDIR)/md.o: md.S
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGETDIR)/%.o: %.c common.h md.h Makefile
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(TARGETDIR)
