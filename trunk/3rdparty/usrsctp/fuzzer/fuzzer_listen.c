/*
 * Copyright (C) 2017-2020 Felix Weinrank
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <usrsctp.h>
#include "../programs/programs_helper.h"

#define FUZZ_FAST 1

#ifdef FUZZ_VERBOSE
#define fuzzer_printf(...) debug_printf(__VA_ARGS__)
#else
#define fuzzer_printf(...)
#endif

struct sockaddr_conn sconn;
struct socket *s_l;

static void
dump_packet(const void *buffer, size_t bufferlen, int inout)
{
#ifdef FUZZ_VERBOSE
	char *dump_buf;

	if ((dump_buf = usrsctp_dumppacket(buffer, bufferlen, inout)) != NULL) {
		fprintf(stderr, "%s", dump_buf);
		usrsctp_freedumpbuffer(dump_buf);
	}
#endif // FUZZ_VERBOSE
}

static int
conn_output(void *addr, void *buf, size_t length, uint8_t tos, uint8_t set_df)
{
	dump_packet(buf, length, SCTP_DUMP_OUTBOUND);
	return (0);
}

static void
handle_upcall(struct socket *sock, void *arg, int flgs)
{
	struct socket *conn_sock;
	struct sockaddr_in remote_addr;
	socklen_t addr_len = sizeof(struct sockaddr_in);

	memset(&remote_addr, 0, sizeof(struct sockaddr_in));
	if (((conn_sock = usrsctp_accept(sock, (struct sockaddr *) &remote_addr, &addr_len)) == NULL) && (errno != EINPROGRESS)) {
		perror("usrsctp_accept");
	}

	usrsctp_close(conn_sock);
}

int
init_fuzzer(void) {
	static uint8_t initialized = 0;
	struct sctp_event event;
	uint16_t event_types[] = {
		SCTP_ASSOC_CHANGE,
		SCTP_PEER_ADDR_CHANGE,
		SCTP_SEND_FAILED_EVENT,
		SCTP_REMOTE_ERROR,
		SCTP_SHUTDOWN_EVENT,
		SCTP_ADAPTATION_INDICATION,
		SCTP_PARTIAL_DELIVERY_EVENT};
	unsigned long i;
	struct linger so_linger;
	int result;

#if defined(FUZZ_FAST)
	if (initialized) {
		return 0;
	}
#endif

#ifdef FUZZ_VERBOSE
	usrsctp_init(0, conn_output, debug_printf_stack);
#else
	usrsctp_init(0, conn_output, NULL);
#endif

	usrsctp_enable_crc32c_offload();
	
#ifdef SCTP_DEBUG
	usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_ALL);
#endif
	usrsctp_register_address((void *)1);

	if ((s_l = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, 0)) == NULL) {
		perror("usrsctp_socket");
		exit(EXIT_FAILURE);
	}
	usrsctp_set_non_blocking(s_l, 1);

	/* Bind the server side. */
	memset(&sconn, 0, sizeof(struct sockaddr_conn));
	sconn.sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
	sconn.sconn_len = sizeof(struct sockaddr_conn);
#endif
	sconn.sconn_port = htons(5001);
	sconn.sconn_addr = (void *)1;
	if (usrsctp_bind(s_l, (struct sockaddr *)&sconn, sizeof(struct sockaddr_conn)) < 0) {
		perror("usrsctp_bind");
		exit(EXIT_FAILURE);
	}

	memset(&event, 0, sizeof(event));
	event.se_assoc_id = SCTP_FUTURE_ASSOC;
	event.se_on = 1;
	for (i = 0; i < sizeof(event_types)/sizeof(uint16_t); i++) {
		event.se_type = event_types[i];
		if (usrsctp_setsockopt(s_l, IPPROTO_SCTP, SCTP_EVENT, &event, sizeof(event)) < 0) {
			perror("setsockopt SCTP_EVENT s_l");
			exit(EXIT_FAILURE);
		}
	}

	/* Make server side passive... */
	if (usrsctp_listen(s_l, 1) < 0) {
		perror("usrsctp_listen");
		exit(EXIT_FAILURE);
	}

	so_linger.l_onoff = 1;
	so_linger.l_linger = 0;
	result = usrsctp_setsockopt(s_l, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(struct linger));
	FUZZER_ASSERT(result == 0);

	usrsctp_set_upcall(s_l, handle_upcall, NULL);

	initialized = 1;

	return (0);
}

int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t data_size)
{
	init_fuzzer();

	if (data_size < 8 || data_size > 65535) {
		// Skip too small and too large packets
		return (0);
	}

	dump_packet(data, data_size, SCTP_DUMP_INBOUND);

	usrsctp_conninput((void *)1, data, data_size, 0);

#if !defined(FUZZ_FAST)
	usrsctp_close(s_l);
	while (usrsctp_finish() != 0) {
		//sleep(1);
	}
#endif

	return (0);
}


