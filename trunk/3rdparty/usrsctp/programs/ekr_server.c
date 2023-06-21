/*
 * Copyright (C) 2011-2013 Michael Tuexen
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

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdarg.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <usrsctp.h>
#include "programs_helper.h"

#define PORT 5001
#define MAX_PACKET_SIZE (1<<16)
#define SLEEP 1

#ifdef _WIN32
static DWORD WINAPI
#else
static void *
#endif
handle_packets(void *arg)
{
#ifdef _WIN32
	SOCKET *fdp;
#else
	int *fdp;
#endif
	char buf[MAX_PACKET_SIZE];
	char *dump_buf;
	ssize_t length;

#ifdef _WIN32
	fdp = (SOCKET *)arg;
#else
	fdp = (int *)arg;
#endif
	for (;;) {
#if defined(__NetBSD__)
		pthread_testcancel();
#endif
		length = recv(*fdp, buf, MAX_PACKET_SIZE, 0);
		if (length > 0) {
			if ((dump_buf = usrsctp_dumppacket(buf, (size_t)length, SCTP_DUMP_INBOUND)) != NULL) {
				fprintf(stderr, "%s", dump_buf);
				usrsctp_freedumpbuffer(dump_buf);
			}
			usrsctp_conninput(fdp, buf, (size_t)length, 0);
		}
	}
#ifdef _WIN32
	return 0;
#else
	return (NULL);
#endif
}

static int
conn_output(void *addr, void *buf, size_t length, uint8_t tos, uint8_t set_df)
{
	char *dump_buf;
#ifdef _WIN32
	SOCKET *fdp;
#else
	int *fdp;
#endif

#ifdef _WIN32
	fdp = (SOCKET *)addr;
#else
	fdp = (int *)addr;
#endif
	if ((dump_buf = usrsctp_dumppacket(buf, length, SCTP_DUMP_OUTBOUND)) != NULL) {
		fprintf(stderr, "%s", dump_buf);
		usrsctp_freedumpbuffer(dump_buf);
	}
#ifdef _WIN32
	if (send(*fdp, buf, (int)length, 0) == SOCKET_ERROR) {
		return (WSAGetLastError());
#else
	if (send(*fdp, buf, length, 0) < 0) {
		return (errno);
#endif
	} else {
		return (0);
	}
}

static int
receive_cb(struct socket *s, union sctp_sockstore addr, void *data,
           size_t datalen, struct sctp_rcvinfo rcv, int flags, void *ulp_info)
{

	if (data) {
		if (flags & MSG_NOTIFICATION) {
			printf("Notification of length %d received.\n", (int)datalen);
		} else {
			printf("Msg of length %d received via %p:%u on stream %u with SSN %u and TSN %u, PPID %u, context %u.\n",
			       (int)datalen,
			       addr.sconn.sconn_addr,
			       ntohs(addr.sconn.sconn_port),
			       rcv.rcv_sid,
			       rcv.rcv_ssn,
			       rcv.rcv_tsn,
			       (uint32_t)ntohl(rcv.rcv_ppid),
			       rcv.rcv_context);
		}
		free(data);
	} else {
		usrsctp_close(s);
	}
	return (1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in sin;
	struct sockaddr_conn sconn;
#ifdef _WIN32
	SOCKET fd;
#else
	int fd, rc;
#endif
	struct socket *s;
#ifdef _WIN32
	HANDLE tid;
#else
	pthread_t tid;
#endif
#ifdef _WIN32
	WSADATA wsaData;
#endif

	if (argc < 4) {
		fprintf(stderr, "error: this program requires 4 arguments!\n");
		exit(EXIT_FAILURE);
	}

#ifdef _WIN32
	if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
		fprintf(stderr, "WSAStartup failed\n");
		exit (EXIT_FAILURE);
	}
#endif
	usrsctp_init(0, conn_output, debug_printf_stack);
	/* set up a connected UDP socket */
#ifdef _WIN32
	if ((fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
		fprintf(stderr, "socket() failed with error: %d\n", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
#else
	if ((fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
#endif
	memset(&sin, 0, sizeof(struct sockaddr_in));
	sin.sin_family = AF_INET;
#ifdef HAVE_SIN_LEN
	sin.sin_len = sizeof(struct sockaddr_in);
#endif
	sin.sin_port = htons(atoi(argv[2]));
	if (!inet_pton(AF_INET, argv[1], &sin.sin_addr.s_addr)){
		fprintf(stderr, "error: invalid address\n");
		exit(EXIT_FAILURE);
	}
#ifdef _WIN32
	if (bind(fd, (struct sockaddr *)&sin, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
		fprintf(stderr, "bind() failed with error: %d\n", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
#else
	if (bind(fd, (struct sockaddr *)&sin, sizeof(struct sockaddr_in)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}
#endif
	memset(&sin, 0, sizeof(struct sockaddr_in));
	sin.sin_family = AF_INET;
#ifdef HAVE_SIN_LEN
	sin.sin_len = sizeof(struct sockaddr_in);
#endif
	sin.sin_port = htons(atoi(argv[4]));
	if (!inet_pton(AF_INET, argv[3], &sin.sin_addr.s_addr)){
		printf("error: invalid address\n");
		exit(EXIT_FAILURE);
	}
#ifdef _WIN32
	if (connect(fd, (struct sockaddr *)&sin, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
		fprintf(stderr, "connect() failed with error: %d\n", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
#else
	if (connect(fd, (struct sockaddr *)&sin, sizeof(struct sockaddr_in)) < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}
#endif
#ifdef SCTP_DEBUG
	usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_NONE);
#endif
	usrsctp_sysctl_set_sctp_ecn_enable(0);
	usrsctp_register_address((void *)&fd);
#ifdef _WIN32
	if ((tid = CreateThread(NULL, 0, &handle_packets, (void *)&fd, 0, NULL)) == NULL) {
		fprintf(stderr, "CreateThread() failed with error: %lu\n", GetLastError());
		exit(EXIT_FAILURE);
	}
#else
	if ((rc = pthread_create(&tid, NULL, &handle_packets, (void *)&fd)) != 0) {
		fprintf(stderr, "pthread_create: %s\n", strerror(rc));
		exit(EXIT_FAILURE);
	}
#endif
	if ((s = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, receive_cb, NULL, 0, NULL)) == NULL) {
		perror("usrsctp_socket");
	}
	memset(&sconn, 0, sizeof(struct sockaddr_conn));
	sconn.sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
	sconn.sconn_len = sizeof(struct sockaddr_conn);
#endif
	sconn.sconn_port = htons(PORT);
	sconn.sconn_addr = (void *)&fd;
	if (usrsctp_bind(s, (struct sockaddr *)&sconn, sizeof(struct sockaddr_conn)) < 0) {
		perror("usrsctp_bind");
	}
	if (usrsctp_listen(s, 1) < 0) {
		perror("usrsctp_listen");
	}
	while (1) {
		if (usrsctp_accept(s, NULL, NULL) == NULL) {
			perror("usrsctp_accept");
		}
	}
	return (0);
}
