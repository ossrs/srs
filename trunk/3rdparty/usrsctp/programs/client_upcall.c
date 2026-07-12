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
 * Usage: client_upcall remote_addr remote_port [local_port] [local_encaps_port] [remote_encaps_port]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#if !defined(_WIN32)
#include <unistd.h>
#include <sys/time.h>
#endif /* !defined(_WIN32) */

#include <sys/types.h>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else /* !defined(_WIN32) */
#include <io.h>
#endif

#include <usrsctp.h>
#include "programs_helper.h"
#include <fcntl.h>

#define BUFFERSIZE                 (1<<16)

int done = 0, input_done = 0, connected = 0;

#ifdef _WIN32
typedef char* caddr_t;
#endif

int inputAvailable(void)
{
	struct timeval tv;
	fd_set fds;
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	FD_ZERO(&fds);
#if defined(_WIN32) && !defined(__MINGW32__)
	FD_SET((SOCKET)_fileno(stdin), &fds);
  	select(_fileno(stdin) + 1, &fds, NULL, NULL, &tv);
#else
	FD_SET(STDIN_FILENO, &fds);
	select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
#endif
  return (FD_ISSET(0, &fds));
}

static void
handle_upcall(struct socket *sock, void *arg, int flgs)
{
	int events = usrsctp_get_events(sock);

	if (events & SCTP_EVENT_WRITE && !done && !connected) {
		connected = 1;
		printf("socket connected\n");
		return;
	}

	while (events & SCTP_EVENT_READ && !done && connected) {
		struct sctp_recvv_rn rn;
		ssize_t n;
		struct sockaddr_in addr;
		char *buf = calloc(1, BUFFERSIZE);
		int flags = 0;
		socklen_t len = (socklen_t)sizeof(struct sockaddr_in);
		unsigned int infotype = 0;
		socklen_t infolen = sizeof(struct sctp_recvv_rn);
		memset(&rn, 0, sizeof(struct sctp_recvv_rn));
		n = usrsctp_recvv(sock, buf, BUFFERSIZE, (struct sockaddr *) &addr, &len, (void *)&rn,
					 &infolen, &infotype, &flags);

		if (n > 0) {
#ifdef _WIN32
			_write(_fileno(stdout), buf, (unsigned int)n);
#else
			if (write(fileno(stdout), buf, n) < 0) {
				perror("write");
			}
#endif
		} else if (n == 0) {
			done = 1;
			input_done = 1;
			free(buf);
			break;
		} else {
			perror("\nusrsctp_recvv");
			free (buf);
			break;
		}
		free(buf);
		events = usrsctp_get_events(sock);
	}
	return;
}

int
main(int argc, char *argv[])
{
	struct socket *sock;
	struct sockaddr *addr, *addrs;
	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;
	struct sctp_udpencaps encaps;
	struct sctpstat stat;
	char buffer[200];
	int i, n;

	if (argc > 4) {
		usrsctp_init(atoi(argv[4]), NULL, debug_printf_stack);
	} else {
		usrsctp_init(9899, NULL, debug_printf_stack);
	}
#ifdef SCTP_DEBUG
	usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_NONE);
#endif
	usrsctp_sysctl_set_sctp_blackhole(2);
	usrsctp_sysctl_set_sctp_no_csum_on_loopback(0);

	if ((sock = usrsctp_socket(AF_INET6, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL)) == NULL) {
		perror("usrsctp_socket");
		exit(1);
	}

	usrsctp_set_non_blocking(sock, 1);

	if (argc > 3) {
		memset((void *)&addr6, 0, sizeof(struct sockaddr_in6));
#ifdef HAVE_SIN6_LEN
		addr6.sin6_len = sizeof(struct sockaddr_in6);
#endif
		addr6.sin6_family = AF_INET6;
		addr6.sin6_port = htons(atoi(argv[3]));
		addr6.sin6_addr = in6addr_any;
		if (usrsctp_bind(sock, (struct sockaddr *)&addr6, sizeof(struct sockaddr_in6)) < 0) {
			perror("bind");
			usrsctp_close(sock);
			exit(1);
		}
	}
	if (argc > 5) {
		memset(&encaps, 0, sizeof(struct sctp_udpencaps));
		encaps.sue_address.ss_family = AF_INET6;
		encaps.sue_port = htons(atoi(argv[5]));
		if (usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_REMOTE_UDP_ENCAPS_PORT, (const void*)&encaps, (socklen_t)sizeof(struct sctp_udpencaps)) < 0) {
			perror("setsockopt");
			usrsctp_close(sock);
			exit(1);
		}
	}

	memset((void *)&addr4, 0, sizeof(struct sockaddr_in));
	memset((void *)&addr6, 0, sizeof(struct sockaddr_in6));
#ifdef HAVE_SIN_LEN
	addr4.sin_len = sizeof(struct sockaddr_in);
#endif
#ifdef HAVE_SIN6_LEN
	addr6.sin6_len = sizeof(struct sockaddr_in6);
#endif
	addr4.sin_family = AF_INET;
	addr6.sin6_family = AF_INET6;
	addr4.sin_port = htons(atoi(argv[2]));
	addr6.sin6_port = htons(atoi(argv[2]));
	if (inet_pton(AF_INET6, argv[1], &addr6.sin6_addr) == 1) {
		if (usrsctp_connect(sock, (struct sockaddr *)&addr6, sizeof(struct sockaddr_in6)) < 0) {
			perror("usrsctp_connect");
		}
	} else if (inet_pton(AF_INET, argv[1], &addr4.sin_addr) == 1) {
		if (usrsctp_connect(sock, (struct sockaddr *)&addr4, sizeof(struct sockaddr_in)) < 0) {
			perror("usrsctp_connect");
		}
	} else {
		printf("Illegal destination address.\n");
	}

	usrsctp_set_upcall(sock, handle_upcall, NULL);
	if ((n = usrsctp_getladdrs(sock, 0, &addrs)) < 0) {
		perror("usrsctp_getladdrs");
	} else {
		addr = addrs;
		printf("Local addresses: ");
		for (i = 0; i < n; i++) {
			if (i > 0) {
				printf("%s", ", ");
			}
			switch (addr->sa_family) {
			case AF_INET:
			{
				struct sockaddr_in *sin;
				char buf[INET_ADDRSTRLEN];
				const char *name;

				sin = (struct sockaddr_in *)addr;
				name = inet_ntop(AF_INET, &sin->sin_addr, buf, INET_ADDRSTRLEN);
				printf("%s", name);
#ifndef HAVE_SA_LEN
				addr = (struct sockaddr *)((caddr_t)addr + sizeof(struct sockaddr_in));
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
				printf("%s", name);
#ifndef HAVE_SA_LEN
				addr = (struct sockaddr *)((caddr_t)addr + sizeof(struct sockaddr_in6));
#endif
				break;
			}
			default:
				break;
			}
#ifdef HAVE_SA_LEN
			addr = (struct sockaddr *)((caddr_t)addr + addr->sa_len);
#endif
		}
		printf(".\n");
		usrsctp_freeladdrs(addrs);
	}
	if ((n = usrsctp_getpaddrs(sock, 0, &addrs)) < 0) {
		perror("usrsctp_getpaddrs");
	} else {
		addr = addrs;
		printf("Peer addresses: ");
		for (i = 0; i < n; i++) {
			if (i > 0) {
				printf("%s", ", ");
			}
			switch (addr->sa_family) {
			case AF_INET:
			{
				struct sockaddr_in *sin;
				char buf[INET_ADDRSTRLEN];
				const char *name;

				sin = (struct sockaddr_in *)addr;
				name = inet_ntop(AF_INET, &sin->sin_addr, buf, INET_ADDRSTRLEN);
				printf("%s", name);
#ifndef HAVE_SA_LEN
				addr = (struct sockaddr *)((caddr_t)addr + sizeof(struct sockaddr_in));
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
				printf("%s", name);
#ifndef HAVE_SA_LEN
				addr = (struct sockaddr *)((caddr_t)addr + sizeof(struct sockaddr_in6));
#endif
				break;
			}
			default:
				break;
			}
#ifdef HAVE_SA_LEN
			addr = (struct sockaddr *)((caddr_t)addr + addr->sa_len);
#endif
		}
		printf(".\n");
		usrsctp_freepaddrs(addrs);
	}
	while (!done && !input_done) {
		if (inputAvailable()) {
			if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
				usrsctp_sendv(sock, buffer, strlen(buffer), NULL, 0, NULL, 0, SCTP_SENDV_NOINFO, 0);
			} else {
				if (usrsctp_shutdown(sock, SHUT_WR) < 0) {
					perror("usrsctp_shutdown");
				}
				break;
			}
		}
	}
#ifdef _WIN32
	Sleep(1000);
#else
	sleep(1);
#endif
	usrsctp_close(sock);

	usrsctp_get_stat(&stat);
	printf("Number of packets (sent/received): (%u/%u).\n",
	       stat.sctps_outpackets, stat.sctps_inpackets);
	while (usrsctp_finish() != 0) {
#ifdef _WIN32
		Sleep(1000);
#else
		sleep(1);
#endif
	}
	printf("Client finished\n");
	return(0);
}
