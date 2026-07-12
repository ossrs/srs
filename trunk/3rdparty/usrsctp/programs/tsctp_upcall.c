/*
 * Copyright (C) 2005-2013 Michael Tuexen
 * Copyright (C) 2011-2013 Irene Ruengeler
 * Copyright (C) 2014-2019 Felix Weinrank
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
#include <inttypes.h>
#ifdef LINUX
#include <getopt.h>
#endif
#include <usrsctp.h>
#include "programs_helper.h"

#define TSCTP_CLIENT 1
#define TSCTP_SERVER 2

#define DEFAULT_LENGTH             1024
#define DEFAULT_NUMBER_OF_MESSAGES 1024
#define DEFAULT_PORT               5001
#define BUFFERSIZE                 (1<<16)

static int par_verbose = 0;
static int par_very_verbose = 0;
static unsigned int done = 0;

struct tsctp_meta {
	uint8_t par_role;
	uint8_t par_stats_human;
	uint8_t par_ordered;
	uint64_t par_messages;
	uint64_t par_message_length;
	uint64_t par_runtime;

	uint64_t stat_messages;
	uint64_t stat_message_length;
	uint64_t stat_notifications;
	uint64_t stat_recv_calls;
	struct timeval stat_start;

	uint64_t stat_fragment_sum;

	char *buffer;
};

#ifndef timersub
#define timersub(tvp, uvp, vvp)                                   \
	do {                                                      \
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;    \
		(vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec; \
		if ((vvp)->tv_usec < 0) {                         \
			(vvp)->tv_sec--;                          \
			(vvp)->tv_usec += 1000000;                \
		}                                                 \
	} while (0)
#endif

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
"        -E             local UDP encapsulation port (default 9899)\n"
"        -f             fragmentation point\n"
"        -H             human readable statistics"
"        -l             message length\n"
"        -L             bind to local IP (default INADDR_ANY)\n"
"        -n             number of messages sent (0 means infinite)/received\n"
"        -D             turns Nagle off\n"
"        -R             socket recv buffer\n"
"        -S             socket send buffer\n"
"        -T             time to send messages\n"
"        -u             use unordered user messages\n"
"        -U             remote UDP encapsulation port\n"
"        -v             verbose\n"
"        -V             very verbose\n"
;

static void handle_upcall(struct socket *upcall_socket, void *upcall_data, int upcall_flags);

static const char *bytes2human(uint64_t bytes)
{
	char *suffix[] = {"", "K", "M", "G", "T"};
	char suffix_length = sizeof(suffix) / sizeof(suffix[0]);
	int i = 0;
	double human_size = bytes;
	static char output[200];

	if (bytes > 1024) {
		for (i = 0; (bytes / 1024) > 0 && i < suffix_length - 1; i++) {
			human_size = bytes / 1024.0;
			bytes /= 1024;
		}
	}

	if (snprintf(output, sizeof(output), "%.02lf %s", human_size, suffix[i]) < 0) {
		output[0] = '\0';
	}
	return output;
}


static void
handle_accept(struct socket *upcall_socket, void *upcall_data, int upcall_flags)
{
	struct socket *conn_sock;
	struct sockaddr_in remote_addr;
	socklen_t addr_len = sizeof(struct sockaddr_in);
	struct tsctp_meta *meta_listening, *meta_accepted;
	char addrbuf[INET_ADDRSTRLEN];

	meta_listening = (struct tsctp_meta *) upcall_data;

	memset(&remote_addr, 0, sizeof(struct sockaddr_in));
	if (((conn_sock = usrsctp_accept(upcall_socket, (struct sockaddr *) &remote_addr, &addr_len)) == NULL) && (errno != EINPROGRESS)) {
		perror("usrsctp_accept");
		exit(EXIT_FAILURE);
	}

	if (par_verbose) {
		printf("Connection accepted from %s:%d\n", inet_ntop(AF_INET, &(remote_addr.sin_addr), addrbuf, INET_ADDRSTRLEN), ntohs(remote_addr.sin_port));
	}

	meta_accepted = malloc(sizeof(struct tsctp_meta));
	if (!meta_accepted) {
		printf("malloc() failed!\n");
		exit(EXIT_FAILURE);
	}

	memset(meta_accepted, 0, sizeof(struct tsctp_meta));

	meta_accepted->par_role = meta_listening->par_role;
	meta_accepted->par_stats_human = meta_listening->par_stats_human;
	meta_accepted->buffer = malloc(BUFFERSIZE);

	if (!meta_accepted->buffer) {
		printf("malloc() failed!\n");
		exit(EXIT_FAILURE);
	}

	usrsctp_set_upcall(conn_sock, handle_upcall, meta_accepted);
}

static void
handle_upcall(struct socket *upcall_socket, void *upcall_data, int upcall_flags)
{
	int events = usrsctp_get_events(upcall_socket);
	struct tsctp_meta* tsctp_meta = (struct tsctp_meta*) upcall_data;

	struct sctp_recvv_rn rn;
	ssize_t n;
	struct sockaddr_storage addr;
	int recv_flags = 0;
	socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
	unsigned int infotype = 0;
	socklen_t infolen = sizeof(struct sctp_recvv_rn);
	struct sctp_rcvinfo *rcvinfo = (struct sctp_rcvinfo *) &rn;
	memset(&rn, 0, sizeof(struct sctp_recvv_rn));
	struct timeval note_time;
	union sctp_notification *snp;
	struct sctp_paddr_change *spc;
	struct timeval time_now;
	struct timeval time_diff;
	float seconds;
	struct sctp_sndinfo snd_info;

	if (events & SCTP_EVENT_READ) {
		while ((n = usrsctp_recvv(upcall_socket, tsctp_meta->buffer, BUFFERSIZE, (struct sockaddr *) &addr, &len, (void *)&rn, &infolen, &infotype, &recv_flags)) > 0) {

			if (!tsctp_meta->stat_recv_calls) {
				gettimeofday(&tsctp_meta->stat_start, NULL);
			}
			tsctp_meta->stat_recv_calls++;

			if (recv_flags & MSG_NOTIFICATION) {
				tsctp_meta->stat_notifications++;
				gettimeofday(&note_time, NULL);
				if (par_verbose) {
					printf("notification arrived at %f\n", note_time.tv_sec + (double)note_time.tv_usec / 1000000.0);

					snp = (union sctp_notification *)tsctp_meta->buffer;
					if (snp->sn_header.sn_type == SCTP_PEER_ADDR_CHANGE) {
						spc = &snp->sn_paddr_change;
						printf("SCTP_PEER_ADDR_CHANGE: state=%u, error=%u\n",spc->spc_state, spc->spc_error);
					}
				}
			} else {
				if (par_very_verbose) {
					if (infotype == SCTP_RECVV_RCVINFO) {
						printf("Message received - %zd bytes - %s - sid %u - tsn %u %s\n",
							n,
							(rcvinfo->rcv_flags & SCTP_UNORDERED) ? "unordered" : "ordered",
							rcvinfo->rcv_sid,
							rcvinfo->rcv_tsn,
							(recv_flags & MSG_EOR) ? "- EOR" : ""
						);

					} else {
						printf("Message received - %zd bytes %s\n", n, (recv_flags & MSG_EOR) ? "- EOR" : "");
					}
				}
				tsctp_meta->stat_fragment_sum += n;
				if (recv_flags & MSG_EOR) {
					tsctp_meta->stat_messages++;
					if (tsctp_meta->stat_message_length == 0) {
						tsctp_meta->stat_message_length = tsctp_meta->stat_fragment_sum;
					}
				}
			}
		}

		if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			perror("usrsctp_recvv");
			exit(EXIT_FAILURE);
		}

		if (n == 0) {
			done = 1;

			gettimeofday(&time_now, NULL);
			timersub(&time_now, &tsctp_meta->stat_start, &time_diff);
			seconds = time_diff.tv_sec + (double)time_diff.tv_usec / 1000000.0;

			if (tsctp_meta->par_stats_human) {
				printf("Connection closed - statistics\n");

				printf("\tmessage size  : %" PRIu64 "\n", tsctp_meta->stat_message_length);
				printf("\tmessages      : %" PRIu64 "\n", tsctp_meta->stat_messages);
				printf("\trecv() calls  : %" PRIu64 "\n", tsctp_meta->stat_recv_calls);
				printf("\tnotifications : %" PRIu64 "\n", tsctp_meta->stat_notifications);
				printf("\ttransferred   : %sByte\n", bytes2human(tsctp_meta->stat_message_length * tsctp_meta->stat_messages));
				printf("\truntime       : %.2f s\n", seconds);
				printf("\tgoodput       : %sBit/s\n", bytes2human((double) tsctp_meta->stat_message_length * (double) tsctp_meta->stat_messages / seconds * 8));

			} else {
				printf("%" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %f, %f, %" PRIu64 "\n",
						tsctp_meta->stat_message_length,
						tsctp_meta->stat_messages,
						tsctp_meta->stat_recv_calls,
						tsctp_meta->stat_message_length * tsctp_meta->stat_messages,
						seconds,
						(double) tsctp_meta->stat_message_length * (double) tsctp_meta->stat_messages / seconds,
						tsctp_meta->stat_notifications);
			}
			fflush(stdout);
			usrsctp_close(upcall_socket);

			free(tsctp_meta->buffer);
			free(tsctp_meta);
			return;
		}
	}

	if ((events & SCTP_EVENT_WRITE) && tsctp_meta->par_role == TSCTP_CLIENT && !done) {

		memset(&snd_info, 0, sizeof(struct sctp_sndinfo));
		if (tsctp_meta->par_ordered == 0) {
			snd_info.snd_flags |= SCTP_UNORDERED;
		}

		while (usrsctp_sendv(upcall_socket, tsctp_meta->buffer, tsctp_meta->par_message_length, NULL, 0, &snd_info, (socklen_t)sizeof(struct sctp_sndinfo), SCTP_SENDV_SNDINFO, 0) > 0) {
			if (tsctp_meta->stat_messages == 0) {
				gettimeofday(&tsctp_meta->stat_start, NULL);
			}
			tsctp_meta->stat_messages++;

			if (par_very_verbose) {
				printf("Message #%" PRIu64 " sent\n", tsctp_meta->stat_messages);
			}

			if (tsctp_meta->par_messages && tsctp_meta->par_messages == tsctp_meta->stat_messages) {
				break;
			}
		}

		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			done = 1;
			usrsctp_close(upcall_socket);
			printf("client socket %p closed\n", (void *)upcall_socket);
			free(tsctp_meta->buffer);
			free(tsctp_meta);
			return;
		}

		gettimeofday(&time_now, NULL);
		timersub(&time_now, &tsctp_meta->stat_start, &time_diff);
		seconds = time_diff.tv_sec + (double)time_diff.tv_usec / 1000000.0;

		if ((tsctp_meta->par_messages && tsctp_meta->par_messages == tsctp_meta->stat_messages) ||
			(tsctp_meta->par_runtime && tsctp_meta->par_runtime <= seconds)) {

			if (par_verbose) {
				printf("Runtime or max messages reached - finishing...\n");
			}

			done = 1;
			usrsctp_close(upcall_socket);
			free(tsctp_meta->buffer);
			free(tsctp_meta);
			return;
		}
	}

	return;
}

int main(int argc, char **argv)
{
#ifndef _WIN32
	int c;
#endif
	struct socket *psock = NULL;
	struct sockaddr_in local_addr;
	struct sockaddr_in remote_addr;
	int optval;
	uint16_t local_port;
	uint16_t remote_port;
	uint16_t local_udp_port;
	uint16_t remote_udp_port;
	int rcvbufsize = 0;
	int sndbufsize = 0;
	socklen_t intlen;
	int nodelay = 0;
	struct sctp_assoc_value av;
	struct sctp_udpencaps encaps;
	struct tsctp_meta *meta;

	uint16_t par_port = DEFAULT_PORT;
	uint8_t par_stats_human = 0;
	int par_ordered = 1;
	int par_message_length = DEFAULT_LENGTH;
	int par_messages = DEFAULT_NUMBER_OF_MESSAGES;
	int par_runtime = 0;

#ifdef _WIN32
	unsigned long src_addr;
#else
	in_addr_t src_addr;
#endif
	int fragpoint = 0;
	struct sctp_setadaptation ind = {0};
#ifdef _WIN32
	char *opt;
	int optind;
#endif

	remote_udp_port = 0;
	local_udp_port = 9899;
	src_addr = htonl(INADDR_ANY);

	memset((void *) &remote_addr, 0, sizeof(struct sockaddr_in));
	memset((void *) &local_addr, 0, sizeof(struct sockaddr_in));

#ifndef _WIN32
	while ((c = getopt(argc, argv, "a:DE:f:Hl:L:n:p:R:S:T:uU:vV")) != -1)
		switch(c) {
			case 'a':
				ind.ssb_adaptation_ind = atoi(optarg);
				break;
			case 'D':
				nodelay = 1;
				break;
			case 'E':
				local_udp_port = atoi(optarg);
				break;
			case 'f':
				fragpoint = atoi(optarg);
				break;
			case 'H':
				par_stats_human = 1;
				break;
			case 'l':
				par_message_length = atoi(optarg);
				break;
			case 'L':
				if (inet_pton(AF_INET, optarg, &src_addr) != 1) {
					printf("Can't parse %s\n", optarg);
					exit(EXIT_FAILURE);
				}
				break;
			case 'n':
				par_messages = atoi(optarg);
				break;
			case 'p':
				par_port = atoi(optarg);
				break;
			case 'R':
				rcvbufsize = atoi(optarg);
				break;
			case 'S':
				sndbufsize = atoi(optarg);
				break;
			case 'T':
				par_runtime = atoi(optarg);
				par_messages = 0;
				break;
			case 'u':
				par_ordered = 0;
				break;
			case 'U':
				remote_udp_port = atoi(optarg);
				break;
			case 'v':
				par_verbose = 1;
				break;
			case 'V':
				par_verbose = 1;
				par_very_verbose = 1;
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
				case 'D':
					nodelay = 1;
					break;
				case 'E':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					local_udp_port = atoi(opt);
					break;
				case 'f':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					fragpoint = atoi(opt);
					break;
				case 'H':
					par_stats_human = 1;
					break;
				case 'l':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					par_message_length = atoi(opt);
					break;
				case 'L':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					inet_pton(AF_INET, opt, &src_addr);
					break;
				case 'n':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					par_messages = atoi(opt);
					break;
				case 'p':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					par_port = atoi(opt);
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
				case 'T':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					par_runtime = atoi(opt);
					par_messages = 0;
					break;
				case 'u':
					par_ordered = 0;
					break;
				case 'U':
					if (++optind >= argc) {
						printf("%s", Usage);
						exit(1);
					}
					opt = argv[optind];
					remote_udp_port = atoi(opt);
					break;
				case 'v':
					par_verbose = 1;
					break;
				case 'V':
					par_verbose = 1;
					par_very_verbose = 1;
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

	meta = malloc(sizeof(struct tsctp_meta));
	if (!meta) {
		printf("malloc() failed!\n");
		exit(EXIT_FAILURE);
	}

	memset(meta, 0, sizeof(struct tsctp_meta));

	meta->buffer = malloc(BUFFERSIZE);
	if (!meta->buffer) {
		printf("malloc() failed!\n");
		exit(EXIT_FAILURE);
	}

	meta->par_stats_human = par_stats_human;
	meta->par_message_length = par_message_length;
	meta->par_messages = par_messages;
	meta->par_ordered = par_ordered;
	meta->par_runtime = par_runtime;

	if (optind == argc) {
		meta->par_role = TSCTP_SERVER;
		local_port = par_port;
		remote_port = 0;
	} else {
		meta->par_role = TSCTP_CLIENT;
		local_port = 0;
		remote_port = par_port;
	}
	local_addr.sin_family = AF_INET;
#ifdef HAVE_SIN_LEN
	local_addr.sin_len = sizeof(struct sockaddr_in);
#endif
	local_addr.sin_port = htons(local_port);
	local_addr.sin_addr.s_addr = src_addr;

	usrsctp_init(local_udp_port, NULL, debug_printf_stack);
#ifdef SCTP_DEBUG
	usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_ALL);
#endif
	usrsctp_sysctl_set_sctp_blackhole(2);
	usrsctp_sysctl_set_sctp_no_csum_on_loopback(0);
	usrsctp_sysctl_set_sctp_enable_sack_immediately(1);

	if (!(psock = usrsctp_socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL))) {
		perror("user_socket");
		exit(EXIT_FAILURE);
	}

	optval = 1;
	if (usrsctp_setsockopt(psock, IPPROTO_SCTP, SCTP_RECVRCVINFO, &optval, sizeof(optval)) < 0) {
		perror("usrsctp_setsockopt SCTP_RECVRCVINFO");
	}

	usrsctp_set_non_blocking(psock, 1);

	if (usrsctp_bind(psock, (struct sockaddr *) &local_addr, sizeof(struct sockaddr_in)) == -1) {
		perror("usrsctp_bind");
		exit(1);
	}

	if (usrsctp_setsockopt(psock, IPPROTO_SCTP, SCTP_ADAPTATION_LAYER, (const void*)&ind, (socklen_t)sizeof(struct sctp_setadaptation)) < 0) {
		perror("setsockopt");
	}

	if (meta->par_role == TSCTP_SERVER) {
		if (rcvbufsize) {
			if (usrsctp_setsockopt(psock, SOL_SOCKET, SO_RCVBUF, &rcvbufsize, sizeof(int)) < 0) {
				perror("setsockopt: rcvbuf");
			}
		}
		if (par_verbose) {
			intlen = sizeof(int);
			if (usrsctp_getsockopt(psock, SOL_SOCKET, SO_RCVBUF, &rcvbufsize, (socklen_t *)&intlen) < 0) {
				perror("getsockopt: rcvbuf");
			} else {
				fprintf(stdout, "Receive buffer size: %d.\n", rcvbufsize);
			}
		}

		if (usrsctp_listen(psock, 1) < 0) {
			perror("usrsctp_listen");
			exit(EXIT_FAILURE);
		}

		usrsctp_set_upcall(psock, handle_accept, meta);

		while (1) {
#ifdef _WIN32
			Sleep(1000);
#else
			sleep(1);
#endif
		}

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
			exit(EXIT_FAILURE);
		}
		remote_addr.sin_port = htons(remote_port);

		memset(meta->buffer, 'X', BUFFERSIZE);

		usrsctp_set_upcall(psock, handle_upcall, meta);

		usrsctp_setsockopt(psock, IPPROTO_SCTP, SCTP_NODELAY, &nodelay, sizeof(nodelay));

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

		if (par_verbose) {
			intlen = sizeof(int);
			if (usrsctp_getsockopt(psock, SOL_SOCKET, SO_SNDBUF, &sndbufsize, (socklen_t *)&intlen) < 0) {
				perror("setsockopt: SO_SNDBUF");
			} else {
				fprintf(stdout,"Send buffer size: %d.\n", sndbufsize);
			}
		}

		if (usrsctp_connect(psock, (struct sockaddr *) &remote_addr, sizeof(struct sockaddr_in)) == -1 ) {
			if (errno != EINPROGRESS) {
				perror("usrsctp_connect");
				exit(EXIT_FAILURE);
			}
		}

		while (!done) {
#ifdef _WIN32
			Sleep(1000);
#else
			sleep(1);
#endif
		}

		if (par_verbose) {
			printf("Finished... \n");
		}
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
