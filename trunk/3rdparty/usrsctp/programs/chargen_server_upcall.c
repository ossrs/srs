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
 * Usage: chargen_server_upcall [local_encaps_port] [remote_encaps_port]
 */

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <usrsctp.h>
#include "programs_helper.h"

#define BUFFERSIZE 10240
#define PORT 19

char buffer[95];
int done = 0;
int send_done = 0;

static void
initBuffer(void) {
	int i, j;
	for (i = 32, j = 0; i < 126; i++, j++) {
		buffer[j] = i;
	}
}

unsigned int signCounter = 0;
static void
handle_upcall(struct socket *upcall_socket, void *upcall_data, int upcall_flags);

static void
handle_accept(struct socket *upcall_socket, void *upcall_data, int upcall_flags)
{
	struct socket *conn_sock;

	if (((conn_sock = usrsctp_accept(upcall_socket, NULL, NULL)) == NULL)
	    && (errno != EINPROGRESS)) {
		perror("usrsctp_accept");
		return;
	}
	done = 0;
	printf("connection accepted from socket %p\n", (void *)conn_sock);
	usrsctp_set_upcall(conn_sock, handle_upcall, NULL);
}

static void
handle_upcall(struct socket *upcall_socket, void *upcall_data, int upcall_flags)
{
	int events = usrsctp_get_events(upcall_socket);

	if (events & SCTP_EVENT_READ && !send_done) {
		char *buf;
		struct sctp_recvv_rn rn;
		ssize_t n;
		struct sockaddr_storage addr;
		buf = malloc(BUFFERSIZE);
		int recv_flags = 0;
		socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
		unsigned int infotype = 0;
		socklen_t infolen = sizeof(struct sctp_recvv_rn);
		memset(&rn, 0, sizeof(struct sctp_recvv_rn));

		n = usrsctp_recvv(upcall_socket, buf, BUFFERSIZE, (struct sockaddr *) &addr, &len, (void *)&rn,
				 &infolen, &infotype, &recv_flags);
		if (n < 0) {
			perror("usrsctp_recvv");
			done = 1;
			usrsctp_close(upcall_socket);
			printf("client socket %p closed\n", (void *)upcall_socket);
			upcall_socket = NULL;
			return;
		}
		if (n == 0) {
			done = 1;
			usrsctp_close(upcall_socket);
			printf("client socket %p closed\n", (void *)upcall_socket);
			upcall_socket = NULL;
			return;
		}
		if (n > 0) {
			if (recv_flags & MSG_NOTIFICATION) {
				printf("Notification of length %d received.\n", (int)n);
			} else {
				printf("data of size %d received\n", (int)n);
			}
		}
		free(buf);
	}

	if ((events & SCTP_EVENT_WRITE) && !done) {
		struct sctp_sndinfo snd_info;
		snd_info.snd_sid = 0;
		snd_info.snd_flags = 0;
		snd_info.snd_ppid = 0;
		snd_info.snd_context = 0;
		snd_info.snd_assoc_id = 0;
		if (usrsctp_sendv(upcall_socket, buffer, strlen(buffer), NULL, 0, &snd_info, (socklen_t)sizeof(struct sctp_sndinfo), SCTP_SENDV_SNDINFO, 0) < 0) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				send_done = 1;
				usrsctp_close(upcall_socket);
				printf("client socket %p closed\n", (void *)upcall_socket);
				return;
			}
		}
	}

	return;
}

int
main(int argc, char *argv[])
{
	struct socket *listening_socket;
	struct sockaddr_in6 addr;
	struct sctp_udpencaps encaps;
	struct sctp_assoc_value av;
	const int on = 1;

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

	if ((listening_socket = usrsctp_socket(AF_INET6, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL)) == NULL) {
		perror("usrsctp_socket");
	}
	usrsctp_set_non_blocking(listening_socket, 1);
	if (usrsctp_setsockopt(listening_socket, IPPROTO_SCTP, SCTP_I_WANT_MAPPED_V4_ADDR, (const void*)&on, (socklen_t)sizeof(int)) < 0) {
		perror("usrsctp_setsockopt SCTP_I_WANT_MAPPED_V4_ADDR");
	}
	memset(&av, 0, sizeof(struct sctp_assoc_value));
	av.assoc_id = SCTP_ALL_ASSOC;
	av.assoc_value = 47;

	if (usrsctp_setsockopt(listening_socket, IPPROTO_SCTP, SCTP_CONTEXT, (const void*)&av, (socklen_t)sizeof(struct sctp_assoc_value)) < 0) {
		perror("usrsctp_setsockopt SCTP_CONTEXT");
	}
	if (usrsctp_setsockopt(listening_socket, IPPROTO_SCTP, SCTP_RECVRCVINFO, &on, sizeof(int)) < 0) {
		perror("usrsctp_setsockopt SCTP_RECVRCVINFO");
	}
	if (argc > 2) {
		memset(&encaps, 0, sizeof(struct sctp_udpencaps));
		encaps.sue_address.ss_family = AF_INET6;
		encaps.sue_port = htons(atoi(argv[2]));
		if (usrsctp_setsockopt(listening_socket, IPPROTO_SCTP, SCTP_REMOTE_UDP_ENCAPS_PORT, (const void*)&encaps, (socklen_t)sizeof(struct sctp_udpencaps)) < 0) {
			perror("usrsctp_setsockopt SCTP_REMOTE_UDP_ENCAPS_PORT");
		}
	}

	initBuffer();

	memset((void *)&addr, 0, sizeof(struct sockaddr_in6));
#ifdef HAVE_SIN6_LEN
	addr.sin6_len = sizeof(struct sockaddr_in6);
#endif
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(PORT);
	addr.sin6_addr = in6addr_any;
	if (usrsctp_bind(listening_socket, (struct sockaddr *)&addr, sizeof(struct sockaddr_in6)) < 0) {
		perror("usrsctp_bind");
	}
	if (usrsctp_listen(listening_socket, 1) < 0) {
		perror("usrsctp_listen");
	}
	usrsctp_set_upcall(listening_socket, handle_accept, NULL);

	while (1) {
#ifdef _WIN32
		Sleep(1*1000);
#else
		sleep(1);
#endif
	}
	return (0);
}
