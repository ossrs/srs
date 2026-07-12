#
# Copyright (C) 2011-2012 Michael Tuexen
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the project nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.	IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

AUTOMAKE_OPTIONS       = subdir-objects
EXTRA_DIST             = Makefile.nmake
lib_LTLIBRARIES        = libusrsctp.la
libusrsctp_la_SOURCES  = user_atomic.h \
                         user_environment.c user_environment.h \
                         user_inpcb.h \
                         user_ip_icmp.h \
                         user_ip6_var.h \
                         user_malloc.h \
                         user_mbuf.c \
                         user_mbuf.h \
                         user_queue.h \
                         user_recv_thread.c user_recv_thread.h \
                         user_route.h \
                         user_socket.c \
                         user_socketvar.h \
                         user_uma.h \
                         netinet/sctp.h \
                         netinet/sctp_asconf.c netinet/sctp_asconf.h \
                         netinet/sctp_auth.c netinet/sctp_auth.h \
                         netinet/sctp_bsd_addr.c netinet/sctp_bsd_addr.h \
                         netinet/sctp_callout.c netinet/sctp_callout.h \
                         netinet/sctp_cc_functions.c \
                         netinet/sctp_constants.h \
                         netinet/sctp_crc32.c netinet/sctp_crc32.h \
                         netinet/sctp_header.h \
                         netinet/sctp_indata.c netinet/sctp_indata.h \
                         netinet/sctp_input.c netinet/sctp_input.h \
                         netinet/sctp_lock_userspace.h \
                         netinet/sctp_os.h \
                         netinet/sctp_os_userspace.h \
                         netinet/sctp_output.c netinet/sctp_output.h \
                         netinet/sctp_pcb.c netinet/sctp_pcb.h \
                         netinet/sctp_peeloff.c netinet/sctp_peeloff.h \
                         netinet/sctp_process_lock.h \
                         netinet/sctp_sha1.c netinet/sctp_sha1.h \
                         netinet/sctp_ss_functions.c \
                         netinet/sctp_structs.h \
                         netinet/sctp_sysctl.c netinet/sctp_sysctl.h \
                         netinet/sctp_userspace.c \
                         netinet/sctp_timer.c netinet/sctp_timer.h \
                         netinet/sctp_uio.h \
                         netinet/sctp_usrreq.c \
                         netinet/sctp_var.h \
                         netinet/sctputil.c netinet/sctputil.h \
                         netinet6/sctp6_var.h \
                         netinet6/sctp6_usrreq.c
libusrsctp_la_CFLAGS = $(LIBCFLAGS)
libusrsctp_la_LDFLAGS = -version-info 2:0:0
include_HEADERS = usrsctp.h

