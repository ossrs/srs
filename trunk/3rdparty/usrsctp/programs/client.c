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
 * Usage: client remote_addr remote_port [local_port] [local_encaps_port] [remote_encaps_port]
 */

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else
#include <io.h>
#endif
#include <usrsctp.h>
#include "programs_helper.h"

int done = 0;

#ifdef _WIN32
typedef char* caddr_t;
#endif



static int
receive_cb(struct socket *sock, union sctp_sockstore addr, void *data,
           size_t datalen, struct sctp_rcvinfo rcv, int flags, void *ulp_info)
{
	if (data == NULL) {
		done = 1;
		usrsctp_close(sock);
	} else {
		if (flags & MSG_NOTIFICATION) {
			handle_notification((union sctp_notification *)data, datalen);
		} else {
#ifdef _WIN32
			_write(_fileno(stdout), data, (unsigned int)datalen);
#else
			if (write(fileno(stdout), data, datalen) < 0) {
				perror("write");
			}
#endif
		}
		free(data);
	}
	return (1);
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
	struct sctp_event event;
	uint16_t event_types[] = {SCTP_ASSOC_CHANGE,
	                          SCTP_PEER_ADDR_CHANGE,
	                          SCTP_SEND_FAILED_EVENT};
	char buffer[80];
	unsigned int i;
	int n;

	if (argc < 3) {
		printf("%s", "Usage: client remote_addr remote_port local_port local_encaps_port remote_encaps_port\n");
		return (-1);
	}
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

	if ((sock = usrsctp_socket(AF_INET6, SOCK_STREAM, IPPROTO_SCTP, receive_cb, NULL, 0, NULL)) == NULL) {
		perror("usrsctp_socket");
	}
	memset(&event, 0, sizeof(event));
	event.se_assoc_id = SCTP_ALL_ASSOC;
	event.se_on = 1;
	for (i = 0; i < sizeof(event_types)/sizeof(uint16_t); i++) {
		event.se_type = event_types[i];
		if (usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_EVENT, &event, sizeof(event)) < 0) {
			perror("setsockopt SCTP_EVENT");
		}
	}
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
		}
	}
	if (argc > 5) {
		memset(&encaps, 0, sizeof(struct sctp_udpencaps));
		encaps.sue_address.ss_family = AF_INET6;
		encaps.sue_port = htons(atoi(argv[5]));
		if (usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_REMOTE_UDP_ENCAPS_PORT, (const void*)&encaps, (socklen_t)sizeof(struct sctp_udpencaps)) < 0) {
			perror("setsockopt");
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
	if ((n = usrsctp_getladdrs(sock, 0, &addrs)) < 0) {
		perror("usrsctp_getladdrs");
	} else {
		addr = addrs;
		printf("Local addresses: ");
		for (i = 0; i < (unsigned int)n; i++) {
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
		for (i = 0; i < (unsigned int)n; i++) {
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
	while ((fgets(buffer, sizeof(buffer), stdin) != NULL) && !done) {
		usrsctp_sendv(sock, buffer, strlen(buffer), NULL, 0, NULL, 0, SCTP_SENDV_NOINFO, 0);
	}
	if (!done) {
		if (usrsctp_shutdown(sock, SHUT_WR) < 0) {
			perror("usrsctp_shutdown");
		}
	}
	while (!done) {
#ifdef _WIN32
		Sleep(1 * 1000);
#else
		sleep(1);
#endif
	}
	usrsctp_get_stat(&stat);
	printf("Number of packets (sent/received): (%u/%u).\n",
	       stat.sctps_outpackets, stat.sctps_inpackets);
	while (usrsctp_finish() != 0) {
#ifdef _WIN32
		Sleep(1 * 1000);
#else
		sleep(1);
#endif
	}
	return(0);
}
