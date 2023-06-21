/*
 * Copyright (C) 2016-2019 Felix Weinrank
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
 * Usage: http_client_upcall remote_addr remote_port [local_port] [local_encaps_port] [remote_encaps_port] [uri]
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
#include <sys/time.h>
#else
#include <sys/types.h>
#include <sys/timeb.h>
#include <io.h>
#endif
#include <usrsctp.h>
#include "programs_helper.h"


#define RETVAL_CATCHALL     50
#define RETVAL_TIMEOUT      60
#define RETVAL_ECONNREFUSED 61

int done = 0;
int writePending = 1;
int result = 0;

static const char *request_prefix = "GET";
static const char *request_postfix = "HTTP/1.0\r\nUser-agent: libusrsctp\r\nConnection: close\r\n\r\n";
char request[512];

#ifdef _WIN32
typedef char* caddr_t;
#endif

#define BUFFERSIZE (1<<16)

static void handle_upcall(struct socket *sock, void *arg, int flgs)
{
	int events = usrsctp_get_events(sock);
	ssize_t bytesSent;
	char *buf;

	if ((events & SCTP_EVENT_READ) && !done) {
		struct sctp_recvv_rn rn;
		ssize_t n;
		struct sockaddr_in addr;
		buf = malloc(BUFFERSIZE);
		int flags = 0;
		socklen_t len = (socklen_t)sizeof(struct sockaddr_in);
		unsigned int infotype = 0;
		socklen_t infolen = sizeof(struct sctp_recvv_rn);

		memset(&rn, 0, sizeof(struct sctp_recvv_rn));
		n = usrsctp_recvv(sock, buf, BUFFERSIZE, (struct sockaddr *) &addr, &len, (void *)&rn,
		                  &infolen, &infotype, &flags);

		if (n < 0) {
			if (errno == ECONNREFUSED) {
				result = RETVAL_ECONNREFUSED;
			} else if (errno == ETIMEDOUT) {
				result = RETVAL_TIMEOUT;
			} else {
				result = RETVAL_CATCHALL;
			}
			perror("usrsctp_recvv");
		}

		if (n <= 0) {
			done = 1;
			usrsctp_close(sock);
		} else {
#ifdef _WIN32
			_write(_fileno(stdout), buf, (unsigned int)n);
#else
			if (write(fileno(stdout), buf, n) < 0) {
				perror("write");
			}
#endif
		}
		free(buf);
	}

	if ((events & SCTP_EVENT_WRITE) && writePending && !done) {
		writePending = 0;
		printf("\nHTTP request:\n%s\n", request);
		printf("\nHTTP response:\n");

		/* send GET request */
		bytesSent = usrsctp_sendv(sock, request, strlen(request), NULL, 0, NULL, 0, SCTP_SENDV_NOINFO, 0);
		if (bytesSent < 0) {
			perror("usrsctp_sendv");
			usrsctp_close(sock);
		} else {
			printf("%zd bytes sent\n", bytesSent);
		}
	}
}

int
main(int argc, char *argv[])
{
	struct socket *sock;
	struct sockaddr *addr;
	socklen_t addr_len;
	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;
	struct sockaddr_in bind4;
	struct sockaddr_in6 bind6;
	struct sctp_udpencaps encaps;
	struct sctp_rtoinfo rtoinfo;
	struct sctp_initmsg initmsg;
	uint8_t address_family = 0;

	if (argc < 3) {
		printf("Usage: http_client_upcall remote_addr remote_port [local_port] [local_encaps_port] [remote_encaps_port] [uri]\n");
		return(EXIT_FAILURE);
	}

	memset((void *)&addr4, 0, sizeof(struct sockaddr_in));
	memset((void *)&addr6, 0, sizeof(struct sockaddr_in6));

	if (inet_pton(AF_INET, argv[1], &addr4.sin_addr) == 1) {
		address_family = AF_INET;

		addr = (struct sockaddr *)&addr4;
		addr_len = sizeof(addr4);
#ifdef HAVE_SIN_LEN
		addr4.sin_len = sizeof(struct sockaddr_in);
#endif
		addr4.sin_family = AF_INET;
		addr4.sin_port = htons(atoi(argv[2]));
	} else if (inet_pton(AF_INET6, argv[1], &addr6.sin6_addr) == 1) {
		address_family = AF_INET6;

		addr = (struct sockaddr *)&addr6;
		addr_len = sizeof(addr6);
#ifdef HAVE_SIN6_LEN
		addr6.sin6_len = sizeof(struct sockaddr_in6);
#endif
		addr6.sin6_family = AF_INET6;
		addr6.sin6_port = htons(atoi(argv[2]));
	} else {
		printf("Unsupported destination address - use IPv4 or IPv6 address\n");
		result = RETVAL_CATCHALL;
		goto out;
	}

	if (argc > 4) {
		usrsctp_init(atoi(argv[4]), NULL, debug_printf_stack);
	} else {
		usrsctp_init(9899, NULL, debug_printf_stack);
	}

#ifdef SCTP_DEBUG
	usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_ALL);
#endif

	usrsctp_sysctl_set_sctp_blackhole(2);
	usrsctp_sysctl_set_sctp_no_csum_on_loopback(0);

	if ((sock = usrsctp_socket(address_family, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL)) == NULL) {
		perror("usrsctp_socket");
		result = RETVAL_CATCHALL;
		goto out;
	}

	/* usrsctp_set_non_blocking(sock, 1); */

	rtoinfo.srto_assoc_id = 0;
	rtoinfo.srto_initial = 1000;
	rtoinfo.srto_min = 1000;
	rtoinfo.srto_max = 8000;
	if (usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_RTOINFO, (const void *)&rtoinfo, (socklen_t)sizeof(struct sctp_rtoinfo)) < 0) {
		perror("setsockopt");
		usrsctp_close(sock);
		result = RETVAL_CATCHALL;
		goto out;
	}
	initmsg.sinit_num_ostreams = 1;
	initmsg.sinit_max_instreams = 1;
	initmsg.sinit_max_attempts = 5;
	initmsg.sinit_max_init_timeo = 4000;
	if (usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_INITMSG, (const void *)&initmsg, (socklen_t)sizeof(struct sctp_initmsg)) < 0) {
		perror("setsockopt");
		usrsctp_close(sock);
		result = RETVAL_CATCHALL;
		goto out;
	}

	if (argc > 3) {

		if (address_family == AF_INET) {
			memset((void *)&bind4, 0, sizeof(struct sockaddr_in));
#ifdef HAVE_SIN_LEN
			bind4.sin_len = sizeof(struct sockaddr_in6);
#endif
			bind4.sin_family = AF_INET;
			bind4.sin_port = htons(atoi(argv[3]));
			bind4.sin_addr.s_addr = htonl(INADDR_ANY);

			if (usrsctp_bind(sock, (struct sockaddr *)&bind4, sizeof(bind4)) < 0) {
				perror("bind");
				usrsctp_close(sock);
				result = RETVAL_CATCHALL;
				goto out;
			}
		} else {
			memset((void *)&bind6, 0, sizeof(struct sockaddr_in6));
#ifdef HAVE_SIN6_LEN
			bind6.sin6_len = sizeof(struct sockaddr_in6);
#endif
			bind6.sin6_family = AF_INET6;
			bind6.sin6_port = htons(atoi(argv[3]));
			bind6.sin6_addr = in6addr_any;
			if (usrsctp_bind(sock, (struct sockaddr *)&bind6, sizeof(bind6)) < 0) {
				perror("bind");
				usrsctp_close(sock);
				result = RETVAL_CATCHALL;
				goto out;
			}
		}
	}

	if (argc > 5) {
		memset(&encaps, 0, sizeof(struct sctp_udpencaps));
		encaps.sue_address.ss_family = address_family;
		encaps.sue_port = htons(atoi(argv[5]));
		if (usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_REMOTE_UDP_ENCAPS_PORT, (const void *)&encaps, (socklen_t)sizeof(struct sctp_udpencaps)) < 0) {
			perror("setsockopt");
			usrsctp_close(sock);
			result = RETVAL_CATCHALL;
			goto out;
		}
	}

	if (argc > 6) {
#ifdef _WIN32
		if (_snprintf(request, sizeof(request), "%s %s %s", request_prefix, argv[6], request_postfix) < 0) {
#else
		if (snprintf(request, sizeof(request), "%s %s %s", request_prefix, argv[6], request_postfix) < 0) {
#endif
			request[0] = '\0';
		}
	} else {
#ifdef _WIN32
		if (_snprintf(request, sizeof(request), "%s %s %s", request_prefix, "/", request_postfix) < 0) {
#else
		if (snprintf(request, sizeof(request), "%s %s %s", request_prefix, "/", request_postfix) < 0) {
#endif
			request[0] = '\0';
		}
	}

	printf("\nHTTP request:\n%s\n", request);
	printf("\nHTTP response:\n");


	usrsctp_set_upcall(sock, handle_upcall, NULL);
	usrsctp_set_non_blocking(sock, 1);

	if (usrsctp_connect(sock, addr, addr_len) < 0) {
		if (errno != EINPROGRESS) {
			if (errno == ECONNREFUSED) {
				result = RETVAL_ECONNREFUSED;
			} else if (errno == ETIMEDOUT) {
				result = RETVAL_TIMEOUT;
			} else {
				result = RETVAL_CATCHALL;
			}
			perror("usrsctp_connect");
		}
	}

	while (!done) {
#ifdef _WIN32
		Sleep(1*1000);
#else
		sleep(1);
#endif
	}
out:
	while (usrsctp_finish() != 0) {
#ifdef _WIN32
		Sleep(1000);
#else
		sleep(1);
#endif
	}

	printf("Finished, returning with %d\n", result);
	return (result);
}
