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

/*
 * Usage: ekr_loop [client_port] [server_port] [crc32c offloading <0/1>]
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

#define MAX_PACKET_SIZE (1<<16)
#define LINE_LENGTH (1<<20)
#define DISCARD_PPID 39
#define NUMBER_OF_STEPS 10

static uint8_t crc32c_offloading = 0;

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
	char *dump_buf;
	struct sctp_common_header *hdr;
	ssize_t length;
	char buf[MAX_PACKET_SIZE];
	uint32_t received_crc32c, computed_crc32c;

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
				/* debug_printf_clean("%s", dump_buf); */
				usrsctp_freedumpbuffer(dump_buf);
			}

			if (crc32c_offloading) {
				if ((size_t)length >= sizeof(struct sctp_common_header)) {
					hdr = (struct sctp_common_header *)buf;
					received_crc32c = hdr->crc32c;
					hdr->crc32c = htonl(0);
					computed_crc32c = usrsctp_crc32c(buf, (size_t)length);
					hdr->crc32c = received_crc32c;
					if (received_crc32c == computed_crc32c) {
						usrsctp_conninput(fdp, buf, (size_t)length, 0);
					} else {
						debug_printf("Wrong CRC32c: expected %08x received %08x\n",
						             ntohl(computed_crc32c), ntohl(received_crc32c));
					}
				} else {
					debug_printf("Packet too short: length %zd", length);
				}
			} else {
				usrsctp_conninput(fdp, buf, (size_t)length, 0);
			}
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
	struct sctp_common_header *hdr;
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
	if (crc32c_offloading && length >= sizeof(struct sctp_common_header)) {
		hdr = (struct sctp_common_header *)buf;
		hdr->crc32c = usrsctp_crc32c(buf, (size_t)length);
	}

	if ((dump_buf = usrsctp_dumppacket(buf, length, SCTP_DUMP_OUTBOUND)) != NULL) {
		/* debug_printf_clean("%s", dump_buf); */
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
receive_cb(struct socket *sock, union sctp_sockstore addr, void *data,
           size_t datalen, struct sctp_rcvinfo rcv, int flags, void *ulp_info)
{
	debug_printf("MSG RCV: %p received on sock = %p.\n", data, (void *)sock);
	if (data) {
		if ((flags & MSG_NOTIFICATION) == 0) {
			debug_printf("MSG RCV: length %d, addr %p:%u, stream %u, SSN %u, TSN %u, PPID %u, context %u, %s%s.\n",
			       (int)datalen,
			       addr.sconn.sconn_addr,
			       ntohs(addr.sconn.sconn_port),
			       rcv.rcv_sid,
			       rcv.rcv_ssn,
			       rcv.rcv_tsn,
			       ntohl(rcv.rcv_ppid),
			       rcv.rcv_context,
			       (rcv.rcv_flags & SCTP_UNORDERED) ? "unordered" : "ordered",
				   (flags & MSG_EOR) ? ", EOR" : "");
		}
		free(data);
	} else {
		usrsctp_deregister_address(ulp_info);
		usrsctp_close(sock);
	}
	return (1);
}

static void
print_addresses(struct socket *sock)
{
	int i, n;
	struct sockaddr *addrs, *addr;
#if !defined(HAVE_SA_LEN)
	int sa_len;
#endif

	debug_printf("Addresses: ");
	n = usrsctp_getladdrs(sock, 0, &addrs);
	addr = addrs;
	for (i = 0; i < n; i++) {
		switch (addr->sa_family) {
		case AF_INET:
		{
			struct sockaddr_in *sin;
			char buf[INET_ADDRSTRLEN];
			const char *name;

			sin = (struct sockaddr_in *)addr;
			name = inet_ntop(AF_INET, &sin->sin_addr, buf, INET_ADDRSTRLEN);
			debug_printf_clean("%s:%d", name, ntohs(sin->sin_port));
#if !defined(HAVE_SA_LEN)
			sa_len = (int)sizeof(struct sockaddr_in);
#endif
			break;
		}
		case AF_INET6:
		{
			struct sockaddr_in6 *sin6;
			char buf[INET6_ADDRSTRLEN];
			const char *name;

			sin6 = (struct sockaddr_in6 *)addr;
			name = inet_ntop(AF_INET6, &sin6->sin6_addr, buf, INET6_ADDRSTRLEN);
			debug_printf_clean("%s:%d", name, ntohs(sin6->sin6_port));
#if !defined(HAVE_SA_LEN)
			sa_len = (int)sizeof(struct sockaddr_in6);
#endif
			break;
		}
		case AF_CONN:
		{
			struct sockaddr_conn *sconn;

			sconn = (struct sockaddr_conn *)addr;
			debug_printf_clean("%p:%d", sconn->sconn_addr, ntohs(sconn->sconn_port));
#if !defined(HAVE_SA_LEN)
			sa_len = (int)sizeof(struct sockaddr_conn);
#endif
			break;
		}
		default:
			debug_printf_clean("Unknown family: %d", addr->sa_family);
#if !defined(HAVE_SA_LEN)
			sa_len = (int)sizeof(struct sockaddr);
#endif
			break;
		}
#if !defined(HAVE_SA_LEN)
		addr = (struct sockaddr *)((char *)addr + sa_len);
#else
		addr = (struct sockaddr *)((caddr_t)addr + addr->sa_len);
#endif
		if (i != n - 1) {
			debug_printf_clean(",");
		}
	}
	if (n > 0) {
		usrsctp_freeladdrs(addrs);
	}
	debug_printf_clean("<->");
	n = usrsctp_getpaddrs(sock, 0, &addrs);
	addr = addrs;
	for (i = 0; i < n; i++) {
		switch (addr->sa_family) {
		case AF_INET:
		{
			struct sockaddr_in *sin;
			char buf[INET_ADDRSTRLEN];
			const char *name;

			sin = (struct sockaddr_in *)addr;
			name = inet_ntop(AF_INET, &sin->sin_addr, buf, INET_ADDRSTRLEN);
			debug_printf_clean("%s:%d", name, ntohs(sin->sin_port));
#if !defined(HAVE_SA_LEN)
			sa_len = (int)sizeof(struct sockaddr_in);
#endif
			break;
		}
		case AF_INET6:
		{
			struct sockaddr_in6 *sin6;
			char buf[INET6_ADDRSTRLEN];
			const char *name;

			sin6 = (struct sockaddr_in6 *)addr;
			name = inet_ntop(AF_INET6, &sin6->sin6_addr, buf, INET6_ADDRSTRLEN);
			debug_printf_clean("%s:%d", name, ntohs(sin6->sin6_port));
#if !defined(HAVE_SA_LEN)
			sa_len = (int)sizeof(struct sockaddr_in6);
#endif
			break;
		}
		case AF_CONN:
		{
			struct sockaddr_conn *sconn;

			sconn = (struct sockaddr_conn *)addr;
			debug_printf_clean("%p:%d", sconn->sconn_addr, ntohs(sconn->sconn_port));
#if !defined(HAVE_SA_LEN)
			sa_len = (int)sizeof(struct sockaddr_conn);
#endif
			break;
		}
		default:
			debug_printf_clean("Unknown family: %d", addr->sa_family);
#if !defined(HAVE_SA_LEN)
			sa_len = (int)sizeof(struct sockaddr);
#endif
			break;
		}
#if !defined(HAVE_SA_LEN)
		addr = (struct sockaddr *)((char *)addr + sa_len);
#else
		addr = (struct sockaddr *)((caddr_t)addr + addr->sa_len);
#endif
		if (i != n - 1) {
			debug_printf_clean(",");
		}
	}
	if (n > 0) {
		usrsctp_freepaddrs(addrs);
	}
	debug_printf_clean("\n");
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in sin_s, sin_c;
	struct sockaddr_conn sconn;
	struct sctp_paddrparams paddrparams;
#ifdef _WIN32
	SOCKET fd_c, fd_s;
#else
	int fd_c, fd_s, rc;
#endif
	struct socket *s_c, *s_s, *s_l;
#ifdef _WIN32
	HANDLE tid_c, tid_s;
#else
	pthread_t tid_c, tid_s;
#endif
	int i, j, cur_buf_size, snd_buf_size, rcv_buf_size, sendv_retries_left;
	socklen_t opt_len;
	struct sctp_sndinfo sndinfo;
	char *line;
#ifdef _WIN32
	WSADATA wsaData;
#endif
	uint16_t client_port = 9900;
	uint16_t server_port = 9901;

	if (argc > 1) {
		client_port = atoi(argv[1]);
	}

	if (argc > 2) {
		server_port = atoi(argv[2]);
	}

	if (argc > 3) {
		crc32c_offloading = atoi(argv[3]);
	}

	debug_printf("Starting program\n");
	debug_printf("Config:\n\tClient Port:\t%d\n\tServer Port:\t%d\n\tCRC32C Calc:\t%s\n", client_port, server_port, crc32c_offloading ? "offloaded" : "NOT offloaded");

#ifdef _WIN32
	if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
		debug_printf("WSAStartup failed\n");
		exit (EXIT_FAILURE);
	}
#endif
	usrsctp_init(0, conn_output, debug_printf_stack);

#ifdef SCTP_DEBUG
	usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_NONE);
#endif

	if (crc32c_offloading) {
		usrsctp_enable_crc32c_offload();
	}

	/* set up a connected UDP socket */
#ifdef _WIN32
	if ((fd_c = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
		debug_printf("socket() failed with error: %d\n", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	if ((fd_s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
		debug_printf("socket() failed with error: %d\n", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
#else
	if ((fd_c = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
	if ((fd_s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
#endif
	memset(&sin_c, 0, sizeof(struct sockaddr_in));
	sin_c.sin_family = AF_INET;
#ifdef HAVE_SIN_LEN
	sin_c.sin_len = sizeof(struct sockaddr_in);
#endif
	sin_c.sin_port = htons(client_port);
	sin_c.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	memset(&sin_s, 0, sizeof(struct sockaddr_in));
	sin_s.sin_family = AF_INET;
#ifdef HAVE_SIN_LEN
	sin_s.sin_len = sizeof(struct sockaddr_in);
#endif
	sin_s.sin_port = htons(server_port);
	sin_s.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#ifdef _WIN32
	if (bind(fd_c, (struct sockaddr *)&sin_c, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
		debug_printf("bind() failed with error: %d\n", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	if (bind(fd_s, (struct sockaddr *)&sin_s, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
		debug_printf("bind() failed with error: %d\n", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
#else
	if (bind(fd_c, (struct sockaddr *)&sin_c, sizeof(struct sockaddr_in)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}
	if (bind(fd_s, (struct sockaddr *)&sin_s, sizeof(struct sockaddr_in)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}
#endif
#ifdef _WIN32
	if (connect(fd_c, (struct sockaddr *)&sin_s, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
		debug_printf("connect() failed with error: %d\n", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	if (connect(fd_s, (struct sockaddr *)&sin_c, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
		debug_printf("connect() failed with error: %d\n", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
#else
	if (connect(fd_c, (struct sockaddr *)&sin_s, sizeof(struct sockaddr_in)) < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}
	if (connect(fd_s, (struct sockaddr *)&sin_c, sizeof(struct sockaddr_in)) < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}
#endif
#ifdef _WIN32
	if ((tid_c = CreateThread(NULL, 0, &handle_packets, (void *)&fd_c, 0, NULL)) == NULL) {
		debug_printf("CreateThread() failed with error: %d\n", GetLastError());
		exit(EXIT_FAILURE);
	}
	if ((tid_s = CreateThread(NULL, 0, &handle_packets, (void *)&fd_s, 0, NULL)) == NULL) {
		debug_printf("CreateThread() failed with error: %d\n", GetLastError());
		exit(EXIT_FAILURE);
	}
#else
	if ((rc = pthread_create(&tid_c, NULL, &handle_packets, (void *)&fd_c)) != 0) {
		debug_printf_clean("pthread_create tid_c: %s\n", strerror(rc));
		exit(EXIT_FAILURE);
	}

	if ((rc = pthread_create(&tid_s, NULL, &handle_packets, (void *)&fd_s)) != 0) {
		debug_printf_clean("pthread_create tid_s: %s\n", strerror(rc));
		exit(EXIT_FAILURE);
	};
#endif

	usrsctp_sysctl_set_sctp_ecn_enable(0);
	usrsctp_register_address((void *)&fd_c);
	usrsctp_register_address((void *)&fd_s);
	if ((s_c = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, receive_cb, NULL, 0, &fd_c)) == NULL) {
		perror("usrsctp_socket");
		exit(EXIT_FAILURE);
	}
	opt_len = (socklen_t)sizeof(int);
	cur_buf_size = 0;
	if (usrsctp_getsockopt(s_c, SOL_SOCKET, SO_SNDBUF, &cur_buf_size, &opt_len) < 0) {
		perror("usrsctp_getsockopt");
		exit(EXIT_FAILURE);
	}
	debug_printf("Change send socket buffer size from %d ", cur_buf_size);
	snd_buf_size = 1<<22; /* 4 MB */
	if (usrsctp_setsockopt(s_c, SOL_SOCKET, SO_SNDBUF, &snd_buf_size, sizeof(int)) < 0) {
		perror("usrsctp_setsockopt");
		exit(EXIT_FAILURE);
	}
	opt_len = (socklen_t)sizeof(int);
	cur_buf_size = 0;
	if (usrsctp_getsockopt(s_c, SOL_SOCKET, SO_SNDBUF, &cur_buf_size, &opt_len) < 0) {
		perror("usrsctp_getsockopt");
		exit(EXIT_FAILURE);
	}
	debug_printf_clean("to %d.\n", cur_buf_size);
	memset(&paddrparams, 0, sizeof(struct sctp_paddrparams));
	paddrparams.spp_address.ss_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
	paddrparams.spp_address.ss_len = sizeof(struct sockaddr_conn);
#endif
	paddrparams.spp_flags = SPP_PMTUD_DISABLE;
	paddrparams.spp_pathmtu = 9000;
	if (usrsctp_setsockopt(s_c, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS, &paddrparams, sizeof(struct sctp_paddrparams)) < 0) {
		perror("usrsctp_setsockopt");
		exit(EXIT_FAILURE);
	}
	if ((s_l = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, receive_cb, NULL, 0, &fd_s)) == NULL) {
		perror("usrsctp_socket");
		exit(EXIT_FAILURE);
	}
	opt_len = (socklen_t)sizeof(int);
	cur_buf_size = 0;
	if (usrsctp_getsockopt(s_l, SOL_SOCKET, SO_RCVBUF, &cur_buf_size, &opt_len) < 0) {
		perror("usrsctp_getsockopt");
		exit(EXIT_FAILURE);
	}
	debug_printf("Change receive socket buffer size from %d ", cur_buf_size);
	rcv_buf_size = 1<<16; /* 64 KB */
	if (usrsctp_setsockopt(s_l, SOL_SOCKET, SO_RCVBUF, &rcv_buf_size, sizeof(int)) < 0) {
		perror("usrsctp_setsockopt");
		exit(EXIT_FAILURE);
	}
	opt_len = (socklen_t)sizeof(int);
	cur_buf_size = 0;
	if (usrsctp_getsockopt(s_l, SOL_SOCKET, SO_RCVBUF, &cur_buf_size, &opt_len) < 0) {
		perror("usrsctp_getsockopt");
		exit(EXIT_FAILURE);
	}
	debug_printf_clean("to %d.\n", cur_buf_size);
	/* Bind the client side. */
	memset(&sconn, 0, sizeof(struct sockaddr_conn));
	sconn.sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
	sconn.sconn_len = sizeof(struct sockaddr_conn);
#endif
	sconn.sconn_port = htons(5001);
	sconn.sconn_addr = &fd_c;
	if (usrsctp_bind(s_c, (struct sockaddr *)&sconn, sizeof(struct sockaddr_conn)) < 0) {
		perror("usrsctp_bind");
		exit(EXIT_FAILURE);
	}
	/* Bind the server side. */
	memset(&sconn, 0, sizeof(struct sockaddr_conn));
	sconn.sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
	sconn.sconn_len = sizeof(struct sockaddr_conn);
#endif
	sconn.sconn_port = htons(5001);
	sconn.sconn_addr = &fd_s;
	if (usrsctp_bind(s_l, (struct sockaddr *)&sconn, sizeof(struct sockaddr_conn)) < 0) {
		perror("usrsctp_bind");
		exit(EXIT_FAILURE);
	}
	/* Make server side passive... */
	if (usrsctp_listen(s_l, 1) < 0) {
		perror("usrsctp_listen");
		exit(EXIT_FAILURE);
	}
	/* Initiate the handshake */
	memset(&sconn, 0, sizeof(struct sockaddr_conn));
	sconn.sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
	sconn.sconn_len = sizeof(struct sockaddr_conn);
#endif
	sconn.sconn_port = htons(5001);
	sconn.sconn_addr = &fd_c;
	if (usrsctp_connect(s_c, (struct sockaddr *)&sconn, sizeof(struct sockaddr_conn)) < 0) {
		perror("usrsctp_connect");
		exit(EXIT_FAILURE);
	}
	if ((s_s = usrsctp_accept(s_l, NULL, NULL)) == NULL) {
		perror("usrsctp_accept");
		exit(EXIT_FAILURE);
	}
	usrsctp_close(s_l);
	print_addresses(s_s);

	if ((line = malloc(LINE_LENGTH)) == NULL) {
		exit(EXIT_FAILURE);
	}
	memset(line, 'A', LINE_LENGTH);
	sndinfo.snd_sid = 1;
	sndinfo.snd_ppid = htonl(DISCARD_PPID);
	sndinfo.snd_context = 0;
	sndinfo.snd_assoc_id = 0;

	for (i = 0; i < NUMBER_OF_STEPS; i++) {
		if (i % 2) {
			sndinfo.snd_flags = SCTP_UNORDERED;
		} else {
			sndinfo.snd_flags = 0;
		}
		for (j = 0; j < 2; j++) {
			/* Send a 1 MB message */
			sendv_retries_left = 120;
			debug_printf("usrscp_sendv - step %d - call %d flags %x\n", i, j + 1, sndinfo.snd_flags);
			while (usrsctp_sendv(s_c, line, LINE_LENGTH, NULL, 0, (void *)&sndinfo,
					 (socklen_t)sizeof(struct sctp_sndinfo), SCTP_SENDV_SNDINFO, 0) < 0) {
				debug_printf("usrsctp_sendv - errno: %d - %s\n", errno, strerror(errno));
				if (errno != EWOULDBLOCK || !sendv_retries_left) {
					exit(EXIT_FAILURE);
				}
				sendv_retries_left--;
#ifdef _WIN32
				Sleep(1000);
#else
				sleep(1);
#endif
			}
		}
		debug_printf("Sending done, sleeping\n");
#ifdef _WIN32
		Sleep(1000);
#else
		sleep(1);
#endif
	}
	free(line);
	usrsctp_shutdown(s_c, SHUT_WR);

	while (usrsctp_finish() != 0) {
		debug_printf("Waiting for usrsctp_finish()\n");
#ifdef _WIN32
		Sleep(1000);
#else
		sleep(1);
#endif
	}
#ifdef _WIN32
	TerminateThread(tid_c, 0);
	WaitForSingleObject(tid_c, INFINITE);
	TerminateThread(tid_s, 0);
	WaitForSingleObject(tid_s, INFINITE);
	if (closesocket(fd_c) == SOCKET_ERROR) {
		debug_printf("closesocket() failed with error: %d\n", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	if (closesocket(fd_s) == SOCKET_ERROR) {
		debug_printf("closesocket() failed with error: %d\n", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	WSACleanup();
#else
	pthread_cancel(tid_c);
	pthread_join(tid_c, NULL);
	pthread_cancel(tid_s);
	pthread_join(tid_s, NULL);
	if (close(fd_c) < 0) {
		perror("close");
		exit(EXIT_FAILURE);
	}
	if (close(fd_s) < 0) {
		perror("close");
		exit(EXIT_FAILURE);
	}
#endif
	return (0);
}
