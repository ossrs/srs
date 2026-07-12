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

AM_CPPFLAGS = -I$(srcdir)/../usrsctplib
EXTRA_DIST = \
    Makefile.nmake \
    test_libmgmt.c \
    test_timer.c \
    tsctp.c \
    chargen_server_upcall.c \
    daytime_server.c \
    daytime_server_upcall.c \
    discard_server.c \
    discard_server_upcall.c \
    echo_server.c \
    echo_server_upcall.c \
    client.c \
    client_upcall.c \
    rtcweb.c \
    ekr_client.c \
    ekr_server.c \
    ekr_loop.c \
    ekr_loop_upcall.c \
    ekr_peer.c \
    test_libmgmt.c \
    http_client.c \
    http_client_upcall.c \
    st_client.c

noinst_PROGRAMS = \
    test_libmgmt \
    test_timer \
    tsctp \
    chargen_server_upcall \
    daytime_server \
    daytime_server_upcall \
    discard_server \
    discard_server_upcall \
    echo_server \
    echo_server_upcall \
    client \
    client_upcall \
    rtcweb \
    ekr_client \
    ekr_server \
    ekr_loop \
    ekr_loop_upcall \
    ekr_peer \
    test_libmgmt \
    http_client \
    http_client_upcall \
    st_client

test_libmgmt_SOURCES = programs_helper.c test_libmgmt.c
test_libmgmt_LDADD = ../usrsctplib/libusrsctp.la
test_timer_SOURCES = programs_helper.c test_timer.c
test_timer_LDADD = ../usrsctplib/libusrsctp.la
tsctp_SOURCES = programs_helper.c tsctp.c
tsctp_LDADD = ../usrsctplib/libusrsctp.la
chargen_server_upcall_SOURCES = programs_helper.c chargen_server_upcall.c
chargen_server_upcall_LDADD = ../usrsctplib/libusrsctp.la
daytime_server_SOURCES = programs_helper.c daytime_server.c
daytime_server_LDADD = ../usrsctplib/libusrsctp.la
daytime_server_upcall_SOURCES = programs_helper.c daytime_server_upcall.c
daytime_server_upcall_LDADD = ../usrsctplib/libusrsctp.la
discard_server_SOURCES = programs_helper.c discard_server.c
discard_server_LDADD = ../usrsctplib/libusrsctp.la
discard_server_upcall_SOURCES = programs_helper.c discard_server_upcall.c
discard_server_upcall_LDADD = ../usrsctplib/libusrsctp.la
echo_server_SOURCES = programs_helper.c echo_server.c
echo_server_LDADD = ../usrsctplib/libusrsctp.la
echo_server_upcall_SOURCES = programs_helper.c echo_server_upcall.c
echo_server_upcall_LDADD = ../usrsctplib/libusrsctp.la
client_SOURCES = programs_helper.c client.c
client_LDADD = ../usrsctplib/libusrsctp.la
client_upcall_SOURCES = programs_helper.c client_upcall.c
client_upcall_LDADD = ../usrsctplib/libusrsctp.la
rtcweb_SOURCES = programs_helper.c rtcweb.c
rtcweb_LDADD = ../usrsctplib/libusrsctp.la
ekr_server_SOURCES = programs_helper.c ekr_server.c
ekr_server_LDADD = ../usrsctplib/libusrsctp.la
ekr_client_SOURCES = programs_helper.c ekr_client.c
ekr_client_LDADD = ../usrsctplib/libusrsctp.la
ekr_peer_SOURCES = programs_helper.c ekr_peer.c
ekr_peer_LDADD = ../usrsctplib/libusrsctp.la
ekr_loop_SOURCES = programs_helper.c ekr_loop.c
ekr_loop_LDADD = ../usrsctplib/libusrsctp.la
ekr_loop_upcall_SOURCES = programs_helper.c ekr_loop_upcall.c
ekr_loop_upcall_LDADD = ../usrsctplib/libusrsctp.la
http_client_SOURCES = programs_helper.c http_client.c
http_client_LDADD = ../usrsctplib/libusrsctp.la
http_client_upcall_SOURCES = programs_helper.c http_client_upcall.c
http_client_upcall_LDADD = ../usrsctplib/libusrsctp.la
st_client_SOURCES = programs_helper.c st_client.c
st_client_LDADD = ../usrsctplib/libusrsctp.la
