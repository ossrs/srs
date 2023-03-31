/*
 * Copyright (C) 2005-2013 Michael Tuexen
 * Copyright (C) 2011-2013 Irene Ruengeler
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

#include <sys/types.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <crtdbg.h>
#include <sys/timeb.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#ifdef LINUX
#include <getopt.h>
#endif
#include <usrsctp.h>
#include "programs_helper.h"

/* global for the send callback, but used in kernel version as well */
static unsigned long number_of_messages;
static char *buffer;
static int length;
static struct sockaddr_in remote_addr;
static int unordered;
uint32_t optval = 1;
struct socket *psock = NULL;

static struct timeval start_time;
unsigned int runtime = 0;
static int policy = 0;
static unsigned int timetolive = 0;
static unsigned long cb_messages = 0;
static unsigned long long cb_first_length = 0;
static unsigned long long cb_sum = 0;
static unsigned int use_cb = 0;

#ifdef _WIN32
static void
gettimeofday(struct timeval *tv, void *ignore)
{
	FILETIME filetime;
	ULARGE_INTEGER ularge;

	GetSystemTimeAsFileTime(&filetime);
	ularge.LowPart = filetime.dwLowDateTime;
	ularge.HighPart = filetime.dwHighDateTime;
	/* Change base from Jan 1 1601 00:00:00 to Jan 1 1970 00:00:00 */
#if defined(__MINGW32__)
	ularge.QuadPart -= 116444736000000000ULL;
#else
	ularge.QuadPart -= 116444736000000000UI64;
#endif
	/*
	 * ularge.QuadPart is now the number of 100-nanosecond intervals
	 * since Jan 1 1970 00:00:00.
	 */
#if defined(__MINGW32__)
	tv->tv_sec = (long)(ularge.QuadPart / 10000000ULL);
	tv->tv_usec = (long)((ularge.QuadPart % 10000000ULL) / 10ULL);
#else
	tv->tv_sec = (long)(ularge.QuadPart / 10000000UI64);
	tv->tv_usec = (long)((ularge.QuadPart % 10000000UI64) / 10UI64);
#endif
}
#endif


char Usage[] =
"Usage: tsctp [options] [address]\n"
"Options:\n"
"        -a             set adaptation layer indication\n"
"        -c             use callback API\n"
"        -d             time in seconds after which a status update is printed\n"
"        -D             turns Nagle off\n"
"        -E             local UDP encapsulation port (default 9899)\n"
"        -f             fragmentation point\n"
"        -l             message length\n"
"        -L             bind to local IP (default INADDR_ANY)\n"
"        -n             number of messages sent (0 means infinite)/received\n"
"        -p             port number\n"
"        -P             partial reliability policy to use (0=none (default), 1=ttl, 2=rtx, 3=buf)\n"
"        -R             socket recv buffer\n"
"        -S             socket send buffer\n"
"        -t             based on -P the time to live, number of retransmissions, or priority for messages\n"
"        -T             time to send messages\n"
"        -u             use unordered user messages\n"
"        -U             remote UDP encapsulation port\n"
"        -v             verbose\n"
"        -V             very verbose\n"
;

#define DEFAULT_LENGTH             1024
#define DEFAULT_NUMBER_OF_MESSAGES 1024
#define DEFAULT_PORT               5001
#define BUFFERSIZE                 (1<<16)

static int verbose, very_verbose;
static unsigned int done;
static unsigned int round_duration;

void stop_sender(int sig)
{
	done = 1;
}

static time_t calc_round_timeout(struct timeval round_start)
{
	time_t round_timeout = round_start.tv_sec + round_duration;
	if (round_start.tv_usec >= 500000) {
		round_timeout++;
	}
	return round_timeout;
}

#ifdef _WIN32
static DWORD WINAPI
#else
static void *
#endif
handle_connection(void *arg)
{
	ssize_t n;
	char *buf;

#if !defined(_WIN32)
	pthread_t tid;
#endif
	struct socket *conn_sock;
	struct timeval time_start, time_now, time_diff;
	double seconds;
	unsigned long recv_calls = 0;
	unsigned long notifications = 0;
	int flags;
	struct sockaddr_in addr;
	socklen_t len;
	union sctp_notification *snp;
	struct sctp_paddr_change *spc;
	struct timeval note_time;
	unsigned int infotype;
	struct sctp_recvv_rn rn;
	socklen_t infolen = sizeof(struct sctp_recvv_rn);
	unsigned long messages = 0;
	unsigned long long first_length = 0;
	unsigned long long sum = 0;
	unsigned long long round_bytes = 0;
	struct timeval round_start = (struct timeval){0};
	time_t round_timeout = 0;

	conn_sock = *(struct socket **)arg;

#if !defined(_WIN32)
	tid = pthread_self();
	pthread_detach(tid);
#endif

	buf = malloc(BUFFERSIZE);
	flags = 0;
	len = (socklen_t)sizeof(struct sockaddr_in);
	infotype = 0;
	memset(&rn, 0, sizeof(struct sctp_recvv_rn));
	n = usrsctp_recvv(conn_sock, buf, BUFFERSIZE, (struct sockaddr *) &addr, &len, (void *)&rn,
	                 &infolen, &infotype, &flags);

	gettimeofday(&time_start, NULL);
	if (round_duration > 0) {
		gettimeofday(&round_start, NULL);
		round_timeout = calc_round_timeout(round_start);
	}

	while (n > 0) {
		recv_calls++;
		if (flags & MSG_NOTIFICATION) {
			notifications++;
			gettimeofday(&note_time, NULL);
			printf("notification arrived at %f\n", note_time.tv_sec+(double)note_time.tv_usec/1000000.0);
			snp = (union sctp_notification *)buf;
			if (snp->sn_header.sn_type == SCTP_PEER_ADDR_CHANGE) {
				spc = &snp->sn_paddr_change;
				printf("SCTP_PEER_ADDR_CHANGE: state=%u, error=%u\n",spc->spc_state, spc->spc_error);
			}
		} else {
			if (very_verbose) {
				printf("Message received\n");
			}
			sum += n;
			if (flags & MSG_EOR) {
				messages++;
				if (first_length == 0) {
					first_length = sum;
				}
				if (round_duration > 0) {
					round_bytes += first_length;
				}
			}
		}
		if (round_duration > 0 && round_timeout <= time(NULL)) {
			gettimeofday(&time_now, NULL);
			timersub(&time_now, &round_start, &time_diff);
			seconds = time_diff.tv_sec + (double)time_diff.tv_usec/1000000.0;
			fprintf(stdout, "throughput for the last %f seconds: %f B/s\n", seconds, (double)round_bytes / seconds);

			round_bytes = 0;
			gettimeofday(&round_start, NULL);
			round_timeout = calc_round_timeout(round_start);
		}
		flags = 0;
		len = (socklen_t)sizeof(struct sockaddr_in);
		infolen = sizeof(struct sctp_recvv_rn);
		infotype = 0;
		memset(&rn, 0, sizeof(struct sctp_recvv_rn));
		n = usrsctp_recvv(conn_sock, (void *) buf, BUFFERSIZE, (struct sockaddr *) &addr, &len, (void *)&rn,
		                  &infolen, &infotype, &flags);
	}
	if (n < 0) {
		perror("sctp_recvv");
	}
	gettimeofday(&time_now, NULL);
	timersub(&time_now, &time_start, &time_diff);
	seconds = time_diff.tv_sec + (double)time_diff.tv_usec/1000000.0;
	printf("%llu, %lu, %lu, %llu, %f, %f, %lu\n",
	        first_length, messages, recv_calls, sum, seconds, (double)first_length * (double)messages / seconds, notifications);
	fflush(stdout);
	usrsctp_close(conn_sock);
	free(buf);
#ifdef _WIN32
	return 0;
#else
	return (NULL);
#endif
}

static int
send_cb(struct socket *sock, uint32_t sb_free, void *ulp_info) {
	struct sctp_sendv_spa sendv_spa;

	if ((cb_messages == 0) && verbose) {
		printf("Start sending ");
		if (number_of_messages > 0) {
			printf("%ld messages ", (long)number_of_messages);
		}
		if (runtime > 0) {
			printf("for %u seconds ...", runtime);
		}
		printf("\n");
		fflush(stdout);
	}

	sendv_spa.sendv_flags = SCTP_SEND_SNDINFO_VALID | SCTP_SEND_PRINFO_VALID;

	sendv_spa.sendv_sndinfo.snd_sid = 0;
	sendv_spa.sendv_sndinfo.snd_flags = 0;
	if (unordered != 0) {
		sendv_spa.sendv_sndinfo.snd_flags |= SCTP_UNORDERED;
	}
	sendv_spa.sendv_sndinfo.snd_ppid = 0;
	sendv_spa.sendv_sndinfo.snd_context = 0;
	sendv_spa.sendv_sndinfo.snd_assoc_id = 0;

	sendv_spa.sendv_prinfo.pr_policy = 0;
	switch (policy) {
#ifdef SCTP_PR_SCTP_NONE
	case 0:
		sendv_spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_NONE;
		break;
#endif
#ifdef SCTP_PR_SCTP_TTL
	case 1:
		sendv_spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_TTL;
		break;
#endif
#ifdef SCTP_PR_SCTP_RTX
	case 2:
		sendv_spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_RTX;
		break;
#endif
#ifdef SCTP_PR_SCTP_BUF
	case 3:
		sendv_spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_BUF;
		break;
#endif
	default:
		printf("Unknown PR-SCTP policy.\n");
		break;
	}
	sendv_spa.sendv_prinfo.pr_value = timetolive;

	while (!done && ((number_of_messages == 0) || (cb_messages < (number_of_messages - 1)))) {
		if (very_verbose) {
			printf("Sending message number %lu.\n", cb_messages + 1);
		}

		if (usrsctp_sendv(psock, buffer, length,
		                  (struct sockaddr *) &remote_addr, 1,
		                  (void *)&sendv_spa, (socklen_t)sizeof(struct sctp_sendv_spa), SCTP_SENDV_SPA,
		                  0) < 0) {
			if (errno != EWOULDBLOCK && errno != EAGAIN) {
				perror("usrsctp_sendv (cb)");
				exit(1);
			} else {
				if (very_verbose){
					printf("EWOULDBLOCK or EAGAIN for message number %lu - will retry\n", cb_messages + 1);
				}
				/* send until EWOULDBLOCK then exit callback. */
				return (1);
			}
		}
		cb_messages++;
	}
	if ((done == 1) || (cb_messages == (number_of_messages - 1))) {
		if (very_verbose) {
			printf("Sending final message number %lu.\n", cb_messages + 1);
		}

		sendv_spa.sendv_sndinfo.snd_flags |= SCTP_EOF;
		if (usrsctp_sendv(psock, buffer, length, (struct sockaddr *) &remote_addr, 1,
		                  (void *)&sendv_spa, (socklen_t)sizeof(struct sctp_sendv_spa), SCTP_SENDV_SPA,
		                  0) < 0) {
			if (errno != EWOULDBLOCK && errno != EAGAIN) {
				perror("usrsctp_sendv (cb)");
				exit(1);
			} else {
				if (very_verbose){
					printf("EWOULDBLOCK or EAGAIN for final message number %lu - will retry\n", cb_messages + 1);
				}
				/* send until EWOULDBLOCK then exit callback. */
				return (1);
			}
		}
		cb_messages++;
		done = 2;
	}

	return (1);
}

static int
server_receive_cb(struct socket *sock, union sctp_sockstore addr, void *data,
           size_t datalen, struct sctp_rcvinfo rcv, int flags, void *ulp_info)
{
	struct timeval now, diff_time;
	double seconds;

	if (data == NULL) {
		gettimeofday(&now, NULL);
		timersub(&now, &start_time, &diff_time);
		seconds = diff_time.tv_sec + (double)diff_time.tv_usec / 1000000.0;
		printf("%llu, %lu, %llu, %f, %f\n",
			cb_first_length, cb_messages, cb_sum, seconds, (double)cb_first_length * (double)cb_messages / seconds);
		usrsctp_close(sock);
		cb_first_length = 0;
		cb_sum = 0;
		cb_messages = 0;
		return (1);
	}
	if (cb_first_length == 0) {
		cb_first_length = (unsigned int)datalen;
		gettimeofday(&start_time, NULL);
	}
	cb_sum += datalen;
	cb_messages++;

	free(data);
	return (1);
}

static int
client_receive_cb(struct socket *sock, union sctp_sockstore addr, void *data,
           size_t datalen, struct sctp_rcvinfo rcv, int flags, void *ulp_info)
{
	free(data);
	return (1);
}

int main(int argc, char **argv)
{
#ifndef _WIN32
	int c, rc;
#endif
	socklen_t addr_len;
	struct sockaddr_in local_addr;
	struct timeval time_start, time_now, time_diff;
	int client;
	uint16_t local_port, remote_port, port, local_udp_port, remote_udp_port;
	int rcvbufsize=0, sndbufsize=0, myrcvbufsize, mysndbufsize;
	socklen_t intlen;
	double seconds;
	double throughput;
	int nodelay = 0;
	struct sctp_assoc_value av;
	struct sctp_udpencaps encaps;
	struct sctp_sendv_spa sendv_spa;
	unsigned long messages = 0;
#ifdef _WIN32
	unsigned long srcAddr;
	HANDLE tid;
#else
	in_addr_t srcAddr;
	pthread_t tid;
#endif
	int fragpoint = 0;
	struct sctp_setadaptation ind = {0};
#ifdef _WIN32
	char *opt;
	int optind;
#endif
	unordered = 0;

	length = DEFAULT_LENGTH;
	number_of_messages = DEFAULT_NUMBER_OF_MESSAGES;
	port = DEFAULT_PORT;
	remote_udp_port = 0;
	local_udp_port = 9899;
	verbose = 0;
	very_verbose = 0;
	srcAddr = htonl(INADDR_ANY);
	round_duration = 0;

	memset((void *) &remote_addr, 0, sizeof(struct sockaddr_in));
	memset((void *) &local_addr, 0, sizeof(struct sockaddr_in));

#ifndef _WIN32
	while ((c = getopt(argc, argv, "a:cd:DE:f:l:L:n:p:P:R:S:t:T:uU:vV")) != -1)
		switch(c) {
			case 'a':
				ind.ssb_adaptation_ind = atoi(optarg);
				break;
			case 'c':
				use_cb = 1;
				break;
			case 'd':
				round_duration = atoi(optarg);
				break;
			case 'D':
				nodelay = 1;
				break;
			case 'l':
				length = atoi(optarg);
				break;
			case 'n':
				number_of_messages = atoi(optarg);
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'P':
				policy = atoi(optarg);
				break;
			case 'E':
				local_udp_port = atoi(optarg);
				break;
			case 'f':
				fragpoint = atoi(optarg);
				break;
			case 'L':
				if (inet_pton(AF_INET, optarg, &srcAddr) != 1) {
					printf("Can't parse %s\n", optarg);
				}
				break;
			case 'R':
				rcvbufsize = atoi(optarg);
				break;
			case 'S':
				sndbufsize = atoi(optarg);
				break;
			case 't':
				timetolive = atoi(optarg);
				break;
			case 'T':
				runtime = atoi(optarg);
				number_of_messages = 0;
				break;
			case 'u':
				unordered = 1;
				break;
			case 'U':
				remote_udp_port = atoi(optarg);
				break;
			case 'v':
				verbose = 1;
				break;
			case 'V':
				verbose = 1;
				very_verbose = 1;
				break;
			default:
				fprintf(stderr, "%s", Usage);
				exit(1);
		}
#else
	for (optind = 1; optind < argc; optind++) {
		if (argv[optind][0] == '-') {
			switch (argv[optind][1]) {
				case 'a':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					ind.ssb_adaptation_ind = atoi(opt);
					break;
				case 'c':
					use_cb = 1;
					break;
				case 'l':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					length = atoi(opt);
					break;
				case 'p':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					port = atoi(opt);
					break;
				case 'P':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					policy = atoi(opt);
					break;
				case 'n':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					number_of_messages = atoi(opt);
					break;
				case 'f':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					fragpoint = atoi(opt);
					break;
				case 'L':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					inet_pton(AF_INET, opt, &srcAddr);
					break;
				case 'U':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					remote_udp_port = atoi(opt);
					break;
				case 'E':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					local_udp_port = atoi(opt);
					break;
				case 'R':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					rcvbufsize = atoi(opt);
					break;
				case 'S':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					sndbufsize = atoi(opt);
					break;
				case 't':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					timetolive = atoi(opt);
					break;
				case 'T':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					runtime = atoi(opt);
					number_of_messages = 0;
					break;
				case 'u':
					unordered = 1;
					break;
				case 'v':
					verbose = 1;
					break;
				case 'V':
					verbose = 1;
					very_verbose = 1;
					break;
				case 'D':
					nodelay = 1;
					break;
				default:
					printf("%s", Usage);
					exit(1);
			}
		} else {
			break;
		}
	}
#endif
	if (optind == argc) {
		client = 0;
		local_port = port;
		remote_port = 0;
	} else {
		client = 1;
		local_port = 0;
		remote_port = port;
	}
	local_addr.sin_family = AF_INET;
#ifdef HAVE_SIN_LEN
	local_addr.sin_len = sizeof(struct sockaddr_in);
#endif
	local_addr.sin_port = htons(local_port);
	local_addr.sin_addr.s_addr = srcAddr;

	usrsctp_init(local_udp_port, NULL, debug_printf_stack);
#ifdef SCTP_DEBUG
	usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_ALL);
#endif
	usrsctp_sysctl_set_sctp_blackhole(2);
	usrsctp_sysctl_set_sctp_no_csum_on_loopback(0);
	usrsctp_sysctl_set_sctp_enable_sack_immediately(1);

	if (client) {
		if (use_cb) {
			if (!(psock = usrsctp_socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP, client_receive_cb, send_cb, length, NULL))) {
				perror("user_socket");
				exit(1);
			}
		} else {
			if (!(psock = usrsctp_socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL))) {
				perror("user_socket");
				exit(1);
			}
		}
	} else {
		if (use_cb) {
			if (!(psock = usrsctp_socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP, server_receive_cb, NULL, 0, NULL))) {
				perror("user_socket");
				exit(1);
			}
		} else {
			if (!(psock = usrsctp_socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL))) {
				perror("user_socket");
				exit(1);
			}
		}
	}

	if (usrsctp_bind(psock, (struct sockaddr *)&local_addr, sizeof(struct sockaddr_in)) == -1) {
		perror("usrsctp_bind");
		exit(1);
	}

	if (usrsctp_setsockopt(psock, IPPROTO_SCTP, SCTP_ADAPTATION_LAYER, (const void*)&ind, (socklen_t)sizeof(struct sctp_setadaptation)) < 0) {
		perror("setsockopt");
	}

	if (!client) {
		if (rcvbufsize) {
			if (usrsctp_setsockopt(psock, SOL_SOCKET, SO_RCVBUF, &rcvbufsize, sizeof(int)) < 0) {
				perror("setsockopt: rcvbuf");
			}
		}
		if (verbose) {
			intlen = sizeof(int);
			if (usrsctp_getsockopt(psock, SOL_SOCKET, SO_RCVBUF, &myrcvbufsize, (socklen_t *)&intlen) < 0) {
				perror("getsockopt: rcvbuf");
			} else {
				fprintf(stdout,"Receive buffer size: %d.\n", myrcvbufsize);
			}
		}

		if (usrsctp_listen(psock, 1) < 0) {
			perror("usrsctp_listen");
			exit(1);
		}

		while (1) {
			memset(&remote_addr, 0, sizeof(struct sockaddr_in));
			addr_len = sizeof(struct sockaddr_in);
			if (use_cb) {
				if (usrsctp_accept(psock, (struct sockaddr *) &remote_addr, &addr_len) == NULL) {
					perror("usrsctp_accept");
					continue;
				}
			} else {
				struct socket **conn_sock;

				conn_sock = (struct socket **) malloc(sizeof(struct socket *));
				if ((*conn_sock = usrsctp_accept(psock, (struct sockaddr *) &remote_addr, &addr_len)) == NULL) {
					perror("usrsctp_accept");
					continue;
				}
#ifdef _WIN32
				if ((tid = CreateThread(NULL, 0, &handle_connection, (void *)conn_sock, 0, NULL)) == NULL) {
					fprintf(stderr, "CreateThread() failed with error: %lu\n", GetLastError());
#else
				if ((rc = pthread_create(&tid, NULL, &handle_connection, (void *)conn_sock)) != 0) {
					fprintf(stderr, "pthread_create: %s\n", strerror(rc));
#endif
					usrsctp_close(*conn_sock);
					continue;
				}
			}
			if (verbose) {
				/* const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
				inet_ntoa(remote_addr.sin_addr) */
				char addrbuf[INET_ADDRSTRLEN];
				printf("Connection accepted from %s:%d\n", inet_ntop(AF_INET, &(remote_addr.sin_addr), addrbuf, INET_ADDRSTRLEN), ntohs(remote_addr.sin_port));
			}
		}
		/* usrsctp_close(psock);  unreachable */
	} else {
		memset(&encaps, 0, sizeof(struct sctp_udpencaps));
		encaps.sue_address.ss_family = AF_INET;
		encaps.sue_port = htons(remote_udp_port);
		if (usrsctp_setsockopt(psock, IPPROTO_SCTP, SCTP_REMOTE_UDP_ENCAPS_PORT, (const void*)&encaps, (socklen_t)sizeof(struct sctp_udpencaps)) < 0) {
			perror("setsockopt");
		}

		remote_addr.sin_family = AF_INET;
#ifdef HAVE_SIN_LEN
		remote_addr.sin_len = sizeof(struct sockaddr_in);
#endif
		if (!inet_pton(AF_INET, argv[optind], &remote_addr.sin_addr.s_addr)){
			printf("error: invalid destination address\n");
			exit(1);
		}
		remote_addr.sin_port = htons(remote_port);

		/* TODO fragpoint stuff */
		if (nodelay == 1) {
			optval = 1;
		} else {
			optval = 0;
		}
		usrsctp_setsockopt(psock, IPPROTO_SCTP, SCTP_NODELAY, &optval, sizeof(int));

		if (fragpoint) {
			av.assoc_id = 0;
			av.assoc_value = fragpoint;
			if (usrsctp_setsockopt(psock, IPPROTO_SCTP, SCTP_MAXSEG, &av, sizeof(struct sctp_assoc_value)) < 0) {
				perror("setsockopt: SCTP_MAXSEG");
			}
		}

		if (sndbufsize) {
			if (usrsctp_setsockopt(psock, SOL_SOCKET, SO_SNDBUF, &sndbufsize, sizeof(int)) < 0) {
				perror("setsockopt: sndbuf");
			}
		}
		if (verbose) {
			intlen = sizeof(int);
			if (usrsctp_getsockopt(psock, SOL_SOCKET, SO_SNDBUF, &mysndbufsize, (socklen_t *)&intlen) < 0) {
				perror("setsockopt: SO_SNDBUF");
			} else {
				fprintf(stdout,"Send buffer size: %d.\n", mysndbufsize);
			}
		}

		buffer = malloc(length);
		memset(buffer, 'b', length);

		if (usrsctp_connect(psock, (struct sockaddr *) &remote_addr, sizeof(struct sockaddr_in)) == -1 ) {
			perror("usrsctp_connect");
			exit(1);
		}

		gettimeofday(&time_start, NULL);

		done = 0;

		if (runtime > 0) {
#ifndef _WIN32
			signal(SIGALRM, stop_sender);
			alarm(runtime);
#else
			fprintf(stderr, "You cannot set the runtime in Windows yet\n");
			exit(-1);
#endif
		}

		if (use_cb) {
			while (done < 2 && (cb_messages < (number_of_messages - 1))) {
#ifdef _WIN32
				Sleep(1000);
#else
				sleep(1);
#endif
			}
		} else {
			sendv_spa.sendv_flags = SCTP_SEND_SNDINFO_VALID | SCTP_SEND_PRINFO_VALID;

			sendv_spa.sendv_sndinfo.snd_sid = 0;
			sendv_spa.sendv_sndinfo.snd_flags = 0;
			if (unordered != 0) {
				sendv_spa.sendv_sndinfo.snd_flags |= SCTP_UNORDERED;
			}
			sendv_spa.sendv_sndinfo.snd_ppid = 0;
			sendv_spa.sendv_sndinfo.snd_context = 0;
			sendv_spa.sendv_sndinfo.snd_assoc_id = 0;

			sendv_spa.sendv_prinfo.pr_policy = 0;
			switch (policy) {
#ifdef SCTP_PR_SCTP_NONE
			case 0:
				sendv_spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_NONE;
				break;
#endif
#ifdef SCTP_PR_SCTP_TTL
			case 1:
				sendv_spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_TTL;
				break;
#endif
#ifdef SCTP_PR_SCTP_RTX
			case 2:
				sendv_spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_RTX;
				break;
#endif
#ifdef SCTP_PR_SCTP_BUF
			case 3:
				sendv_spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_BUF;
				break;
#endif
			default:
				printf("Unknown PR-SCTP policy.\n");
				break;
			}
			sendv_spa.sendv_prinfo.pr_value = timetolive;

			if (verbose) {
				printf("Start sending ");
				if (number_of_messages > 0) {
					printf("%ld messages ", (long)number_of_messages);
				}
				if (runtime > 0) {
					printf("for %u seconds ...", runtime);
				}
				printf("\n");
				fflush(stdout);
			}
			while (!done && ((number_of_messages == 0) || (messages < (number_of_messages - 1)))) {
				if (very_verbose) {
					printf("Sending message number %lu.\n", messages + 1);
				}

				if (usrsctp_sendv(psock, buffer, length, (struct sockaddr *) &remote_addr, 1,
				                  (void *)&sendv_spa, (socklen_t)sizeof(struct sctp_sendv_spa), SCTP_SENDV_SPA,
				                  0) < 0) {
					perror("usrsctp_sendv");
					exit(1);
				}
				messages++;
			}
			if (very_verbose) {
				printf("Sending message number %lu.\n", messages + 1);
			}

			sendv_spa.sendv_sndinfo.snd_flags |= SCTP_EOF;
			if (usrsctp_sendv(psock, buffer, length, (struct sockaddr *) &remote_addr, 1,
			                  (void *)&sendv_spa, (socklen_t)sizeof(struct sctp_sendv_spa), SCTP_SENDV_SPA,
			                  0) < 0) {
				perror("usrsctp_sendv");
				exit(1);
			}
			messages++;
		}
		free (buffer);

		if (verbose) {
			printf("Closing socket.\n");
		}

		usrsctp_close(psock);
		gettimeofday(&time_now, NULL);
		timersub(&time_now, &time_start, &time_diff);
		seconds = time_diff.tv_sec + (double)time_diff.tv_usec/1000000;
		printf("%s of %lu messages of length %d took %f seconds.\n",
		       "Sending", messages, length, seconds);
		throughput = (double)messages * (double)length / seconds;
		printf("Throughput was %f Byte/sec.\n", throughput);
	}

	while (usrsctp_finish() != 0) {
#ifdef _WIN32
		Sleep(1000);
#else
		sleep(1);
#endif
	}
	return 0;
}
