#
# Portions created by SGI are Copyright (C) 2000 Silicon Graphics, Inc.
# All Rights Reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met: 
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer. 
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of Silicon Graphics, Inc. nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission. 
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDERS AND CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
# TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

CC          = cc

SHELL       = /bin/sh
ECHO        = /bin/echo

DEPTH       = ..
BUILD       =
TARGETDIR   = obj

DEFINES     =
OTHER_FLAGS =
CFLAGS      =

OBJDIR      = $(DEPTH)/$(TARGETDIR)
INCDIR      = $(DEPTH)/$(TARGETDIR)

LIBRESOLV   =
EXTRALIBS   =

SLIBRARY    = $(OBJDIR)/libstx.a
OBJS        = $(OBJDIR)/dnscache.o $(OBJDIR)/dnsres.o $(OBJDIR)/lrucache.o


CFLAGS      += -Wall -I$(INCDIR)
AR          = ar
ARFLAGS     = rv
RANLIB      = ranlib


##########################
# Platform section.
#

ifeq (LINUX, $(findstring LINUX, $(OS)))
LIBRESOLV   = -lresolv
endif

ifeq ($(OS), SOLARIS)
LIBRESOLV   = -lresolv
EXTRALIBS   = -lsocket -lnsl
endif

#
# End of platform section.
##########################


all: $(SLIBRARY)

$(SLIBRARY): $(OBJS)
	$(AR) $(ARFLAGS) $@ $(OBJS)
	$(RANLIB) $@

$(OBJDIR)/%.o: %.c stx.h common.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJS) $(SLIBRARY)

#.DEFAULT:
#	@cd $(DEPTH); $(MAKE) $@

