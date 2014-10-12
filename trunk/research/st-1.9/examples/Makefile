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

##########################
# Supported OSes:
#
# AIX
# FREEBSD
# HPUX
# HPUX_64
# IRIX
# IRIX_64
# LINUX
# LINUX_IA64
# NETBSD
# OPENBSD
# OSF1
# SOLARIS
# SOLARIS_64

##########################

CC          = cc

SHELL       = /bin/sh
ECHO        = /bin/echo

DEPTH       = ..
BUILD       =
TARGETDIR   =

DEFINES     =
CFLAGS      =
OTHER_FLAGS =

OBJDIR      = $(DEPTH)/$(TARGETDIR)
INCDIR      = $(DEPTH)/$(TARGETDIR)
LIBST       = $(OBJDIR)/libst.a
HEADER      = $(INCDIR)/st.h

LIBRESOLV   =
EXTRALIBS   =

ifeq ($(OS),)
EXAMPLES    = unknown
else
EXAMPLES    = $(OBJDIR)/lookupdns $(OBJDIR)/proxy $(OBJDIR)/server
endif


##########################
# Platform section.
#

ifeq (DARWIN, $(findstring DARWIN, $(OS)))
LIBRESOLV   = -lresolv
endif

ifeq (LINUX, $(findstring LINUX, $(OS)))
LIBRESOLV   = -lresolv
endif

ifeq (SOLARIS, $(findstring SOLARIS, $(OS)))
LIBRESOLV   = -lresolv
EXTRALIBS   = -lsocket -lnsl
endif

#
# End of platform section.
##########################


all: $(EXAMPLES)

$(OBJDIR)/lookupdns: lookupdns.c $(OBJDIR)/res.o $(LIBST) $(HEADER)
	$(CC) $(CFLAGS) -I$(INCDIR) lookupdns.c $(OBJDIR)/res.o $(LIBST) $(LIBRESOLV) $(EXTRALIBS) -o $@

$(OBJDIR)/proxy: proxy.c $(LIBST) $(HEADER)
	$(CC) $(CFLAGS) -I$(INCDIR) proxy.c $(LIBST) $(EXTRALIBS) -o $@

$(OBJDIR)/server: server.c $(OBJDIR)/error.o $(LIBST) $(HEADER)
	$(CC) $(CFLAGS) -I$(INCDIR) server.c $(OBJDIR)/error.o $(LIBST) $(EXTRALIBS) -o $@

$(OBJDIR)/%.o: %.c
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

.DEFAULT:
	@cd $(DEPTH); $(MAKE) $@

