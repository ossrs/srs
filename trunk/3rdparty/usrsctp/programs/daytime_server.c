/*
 * Copyright (C) 2012-2013 Michael Tuexen
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.	IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Usage: daytime_server [local_encaps_port] [remote_encaps_port]
 *
 * Example
 * Server: $ ./daytime_server 11111 22222
 * Client: $ ./client 127.0.0.1 13 0 22222 11111
 */

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <usrsctp.h>
#include "programs_helper.h"

#define PORT 13
#define DAYTIME_PPID 40
#define SLEEP 1

int
main(int argc, char *argv[])
{
	struct socket *sock, *conn_sock;
	struct sockaddr_in addr;
	struct sctp_udpencaps encaps;
	socklen_t addr_len;
	char buffer[80];
	time_t now;
	struct sctp_sndinfo sndinfo;

	if (argc > 1) {
		usrsctp_init(atoi(argv[1]), NULL, debug_printf_stack);
	} else {
		usrsctp_init(9899, NULL, debug_printf_stack);
	}
#ifdef SCTP_DEBUG
	usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_NONE);
#endif
	usrsctp_sysctl_set_sctp_blackhole(2);
	usrsctp_sysctl_set_sctp_no_csum_on_loopback(0);

	if ((sock = usrsctp_socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL)) == NULL) {
		perror("usrsctp_socket");
	}
	if (argc > 2) {
		memset(&encaps, 0, sizeof(struct sctp_udpencaps));
		encaps.sue_address.ss_family = AF_INET;
		encaps.sue_port = htons(atoi(argv[2]));
		if (usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_REMOTE_UDP_ENCAPS_PORT, (const void*)&encaps, (socklen_t)sizeof(struct sctp_udpencaps)) < 0) {
			perror("setsockopt");
		}
	}
	memset((void *)&addr, 0, sizeof(struct sockaddr_in));
#ifdef HAVE_SIN_LEN
	addr.sin_len = sizeof(struct sockaddr_in);
#endif
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (usrsctp_bind(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
		perror("usrsctp_bind");
	}
	if (usrsctp_listen(sock, 1) < 0) {
		perror("usrsctp_listen");
	}
	while (1) {
		addr_len = 0;
		if ((conn_sock = usrsctp_accept(sock, NULL, &addr_len)) == NULL) {
			continue;
		}
		time(&now);
#ifdef _WIN32
		if (_snprintf(buffer, sizeof(buffer), "%s", ctime(&now)) < 0) {
#else
		if (snprintf(buffer, sizeof(buffer), "%s", ctime(&now)) < 0) {
#endif
			buffer[0] = '\0';
		}
		sndinfo.snd_sid = 0;
		sndinfo.snd_flags = 0;
		sndinfo.snd_ppid = htonl(DAYTIME_PPID);
		sndinfo.snd_context = 0;
		sndinfo.snd_assoc_id = 0;
		if (usrsctp_sendv(conn_sock, buffer, strlen(buffer), NULL, 0, (void *)&sndinfo,
		                  (socklen_t)sizeof(struct sctp_sndinfo), SCTP_SENDV_SNDINFO, 0) < 0) {
			perror("usrsctp_sendv");
		}
		usrsctp_close(conn_sock);
	}
	return (0);
}
