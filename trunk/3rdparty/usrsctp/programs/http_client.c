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
 * Usage: http_client remote_addr remote_port [local_port] [local_encaps_port] [remote_encaps_port] [uri]
 *
 * Example
 * Client: $ ./http_client 212.201.121.100 80 0 9899 9899 /cgi-bin/he
 *           ./http_client 2a02:c6a0:4015:10::100 80 0 9899 9899 /cgi-bin/he
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
#include <io.h>
#include <sys/types.h>
#include <sys/timeb.h>
#endif
#include <usrsctp.h>
#include "programs_helper.h"

#define RETVAL_CATCHALL     50
#define RETVAL_TIMEOUT      60
#define RETVAL_ECONNREFUSED 61

int done = 0;
static const char *request_prefix = "GET";
static const char *request_postfix = "HTTP/1.0\r\nUser-agent: libusrsctp\r\nConnection: close\r\n\r\n";
char request[512];

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
	struct sockaddr *addr;
	socklen_t addr_len;
	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;
	struct sockaddr_in bind4;
	struct sockaddr_in6 bind6;
	struct sctp_udpencaps encaps;
	struct sctp_sndinfo sndinfo;
	struct sctp_rtoinfo rtoinfo;
	struct sctp_initmsg initmsg;
	struct sctp_event event;
	int result = 0;
	unsigned int i;
	uint8_t address_family = 0;
	uint16_t event_types[] = {SCTP_ASSOC_CHANGE,
	                          SCTP_PEER_ADDR_CHANGE,
	                          SCTP_SEND_FAILED_EVENT,
 	                          SCTP_REMOTE_ERROR,
	                          SCTP_SHUTDOWN_EVENT,
	                          SCTP_ADAPTATION_INDICATION,
	                          SCTP_PARTIAL_DELIVERY_EVENT
	                        };

	if (argc < 3) {
		printf("Usage: http_client remote_addr remote_port [local_port] [local_encaps_port] [remote_encaps_port] [uri]\n");
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

	if ((sock = usrsctp_socket(address_family, SOCK_STREAM, IPPROTO_SCTP, receive_cb, NULL, 0, NULL)) == NULL) {
		perror("usrsctp_socket");
		result = RETVAL_CATCHALL;
		goto out;
	}

	memset(&event, 0, sizeof(event));
	event.se_assoc_id = SCTP_ALL_ASSOC;
	event.se_on = 1;
	for (i = 0; i < sizeof(event_types) / sizeof(uint16_t); i++) {
		event.se_type = event_types[i];
		if (usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_EVENT, &event, sizeof(event)) < 0) {
			perror("setsockopt SCTP_EVENT");
		}
	}

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

	if (usrsctp_connect(sock, addr, addr_len) < 0) {
		if (errno == ECONNREFUSED) {
			result = RETVAL_ECONNREFUSED;
		} else if (errno == ETIMEDOUT) {
			result = RETVAL_TIMEOUT;
		} else {
			result = RETVAL_CATCHALL;
		}
		perror("usrsctp_connect");
		usrsctp_close(sock);

		goto out;
	}

	memset(&sndinfo, 0, sizeof(struct sctp_sndinfo));
	sndinfo.snd_ppid = htonl(63); /* PPID for HTTP/SCTP */
	/* send GET request */
	if (usrsctp_sendv(sock, request, strlen(request), NULL, 0, &sndinfo, sizeof(struct sctp_sndinfo), SCTP_SENDV_SNDINFO, 0) < 0) {
		perror("usrsctp_sendv");
		usrsctp_close(sock);
		result = RETVAL_CATCHALL;
		goto out;
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
