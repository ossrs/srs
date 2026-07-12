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
#include <stdarg.h>
#include <assert.h>
#include <usrsctp.h>
#include "../programs/programs_helper.h"

//#define FUZZ_ALWAYS_INITIALIZE

//#define FUZZ_VERBOSE
#define FUZZ_INTERLEAVING
#define FUZZ_STREAM_RESET

#define FUZZ_B_INJECT_INIT_ACK        (1 << 0)
#define FUZZ_B_INJECT_COOKIE_ACK      (1 << 1)
#define FUZZ_B_SEND_DATA              (1 << 2)
#define FUZZ_B_SEND_STREAM_RESET      (1 << 3)
#define FUZZ_B_INJECT_DATA            (1 << 4)
#define FUZZ_B_I_DATA_SUPPORT         (1 << 5)
#define FUZZ_B_SEND_DATA_FORCE        (1 << 6)
#define FUZZ_B_RESERVED               (1 << 7)

#define BUFFER_SIZE 4096
#define COMMON_HEADER_SIZE 12

static uint32_t assoc_vtag = 0;

#ifdef FUZZ_VERBOSE
#define fuzzer_printf(...) debug_printf(__VA_ARGS__)
#else
#define fuzzer_printf(...)
#endif

static void
dump_packet(const void *buffer, size_t bufferlen, int inout)
{
#ifdef FUZZ_VERBOSE
	static char *dump_buf;
	if ((dump_buf = usrsctp_dumppacket(buffer, bufferlen, inout)) != NULL) {
		fprintf(stderr, "%s", dump_buf);
		usrsctp_freedumpbuffer(dump_buf);
	}
#endif // FUZZ_VERBOSE
}


static int
conn_output(void *addr, void *buf, size_t length, uint8_t tos, uint8_t set_df)
{
	struct sctp_init_chunk *init_chunk;
	const char *init_chunk_first_bytes = "\x13\x88\x13\x89\x00\x00\x00\x00\x00\x00\x00\x00\x01";
	// Looking for the outgoing VTAG.
	// length >= (COMMON_HEADER_SIZE + 16 (min size of INIT))
	// If the common header has no VTAG (all zero), we're assuming it carries an INIT
	if ((length >= (COMMON_HEADER_SIZE + 16)) && (memcmp(buf, init_chunk_first_bytes, COMMON_HEADER_SIZE) == 0)) {
		init_chunk = (struct sctp_init_chunk*) ((char *)buf + sizeof(struct sctp_common_header));
		fuzzer_printf("Found outgoing INIT, extracting VTAG : %u\n", init_chunk->initiate_tag);
		assoc_vtag = init_chunk->initiate_tag;
	}

	dump_packet(buf, length, SCTP_DUMP_OUTBOUND);
	return (0);
}


static void
handle_upcall(struct socket *sock, void *arg, int flgs)
{
	fuzzer_printf("handle_upcall()\n");
	int events = usrsctp_get_events(sock);

	while (events & SCTP_EVENT_READ) {
		struct sctp_recvv_rn rn;
		ssize_t n;
		struct sockaddr_in addr;
		char *buf = calloc(1, BUFFER_SIZE);
		int flags = 0;
		socklen_t len = (socklen_t)sizeof(struct sockaddr_in);
		unsigned int infotype = 0;
		socklen_t infolen = sizeof(struct sctp_recvv_rn);
		memset(&rn, 0, sizeof(struct sctp_recvv_rn));
		n = usrsctp_recvv(sock, buf, BUFFER_SIZE, (struct sockaddr *) &addr, &len, (void *)&rn, &infolen, &infotype, &flags);
		fuzzer_printf("usrsctp_recvv() - returned %zd\n", n);

		if (flags & MSG_NOTIFICATION) {
			fuzzer_printf("NOTIFICATION received\n");
#ifdef FUZZ_VERBOSE
			handle_notification((union sctp_notification *)buf, n);
#endif // FUZZ_VERBOSE
		} else {
			fuzzer_printf("DATA received\n");
		}

		free(buf);

		if (n <= 0) {
			break;
		}

		events = usrsctp_get_events(sock);
	}
}


int
initialize_fuzzer(void) {
#ifdef FUZZ_VERBOSE
	usrsctp_init(0, conn_output, debug_printf_stack);
#else // FUZZ_VERBOSE
	usrsctp_init(0, conn_output, NULL);
#endif // FUZZ_VERBOSE

	usrsctp_enable_crc32c_offload();

#ifdef SCTP_DEBUG
	usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_ALL);
#endif // SCTP_DEBUG

	usrsctp_register_address((void *)1);
	usrsctp_sysctl_set_sctp_pktdrop_enable(1);
	usrsctp_sysctl_set_sctp_nrsack_enable(1);

	fuzzer_printf("usrsctp initialized\n");
	return (1);
}


int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t data_size)
{
	static int initialized;
	char *fuzz_packet_buffer;
	struct sockaddr_conn sconn;
	struct socket *socket_client;
	struct linger so_linger;
	struct sctp_event event;
	unsigned long i;
	struct sctp_common_header* common_header;
	uint16_t event_types[] = {
		SCTP_ASSOC_CHANGE,
		SCTP_PEER_ADDR_CHANGE,
		SCTP_REMOTE_ERROR,
		SCTP_SEND_FAILED,
		SCTP_SHUTDOWN_EVENT,
		SCTP_ADAPTATION_INDICATION,
		SCTP_PARTIAL_DELIVERY_EVENT,
		SCTP_AUTHENTICATION_EVENT,
		SCTP_STREAM_RESET_EVENT,
		SCTP_SENDER_DRY_EVENT,
		SCTP_ASSOC_RESET_EVENT,
		SCTP_STREAM_CHANGE_EVENT,
		SCTP_SEND_FAILED_EVENT
	};
	int optval;
	int result;
	struct sctp_initmsg initmsg;
#if defined(FUZZ_STREAM_RESET) || defined(FUZZ_INTERLEAVING)
	struct sctp_assoc_value assoc_val;
#endif // defined(FUZZ_STREAM_RESET) || defined(FUZZ_INTERLEAVING)

	// WITH COMMON HEADER!
	char fuzz_init_ack[] = "\x13\x89\x13\x88\x49\xa4\xac\xb2\x00\x00\x00\x00\x02\x00\x01\xb4" \
		"\x2b\xe8\x47\x40\x00\x1c\x71\xc7\xff\xff\xff\xff\xed\x69\x58\xec" \
		"\xc0\x06\x00\x08\x00\x00\x07\xc4\x80\x00\x00\x04\xc0\x00\x00\x04" \
		"\x80\x08\x00\x0b\xc0\xc2\x0f\xc1\x80\x82\x40\x00\x80\x02\x00\x24" \
		"\x40\x39\xcf\x32\xd6\x60\xcf\xfa\x3f\x2f\xa9\x52\xed\x2b\xf2\xe6" \
		"\x2f\xb7\x81\x96\xf8\xda\xe9\xa0\x62\x01\x79\xe1\x0d\x5f\x38\xaa" \
		"\x80\x04\x00\x08\x00\x03\x00\x01\x80\x03\x00\x06\x80\xc1\x00\x00" \
		"\x00\x07\x01\x50\x4b\x41\x4d\x45\x2d\x42\x53\x44\x20\x31\x2e\x31" \
		"\x00\x00\x00\x00\x64\xdb\x63\x00\x00\x00\x00\x00\xc9\x76\x03\x00" \
		"\x00\x00\x00\x00\x60\xea\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
		"\xb2\xac\xa4\x49\x2b\xe8\x47\x40\xd4\xc9\x79\x52\x00\x00\x00\x00" \
		"\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\xd4\xc9\x79\x53" \
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00" \
		"\x00\x00\x00\x00\x5a\x76\x13\x89\x01\x00\x00\x00\x00\x00\x00\x00" \
		"\x00\x00\x00\x00\x01\x00\x00\x62\x49\xa4\xac\xb2\x00\x1c\x71\xc7" \
		"\x00\x01\xff\xff\x82\xe6\xc8\x44\x80\x00\x00\x04\xc0\x00\x00\x04" \
		"\x80\x08\x00\x0b\xc0\xc2\x0f\xc1\x80\x82\x40\x00\x80\x02\x00\x24" \
		"\xb6\xbb\xb5\x7f\xbb\x4b\x0e\xb5\x42\xf6\x75\x18\x4f\x79\x0f\x24" \
		"\x1c\x44\x0b\xd6\x62\xa9\x84\xe7\x2c\x3c\x7f\xad\x1b\x67\x81\x57" \
		"\x80\x04\x00\x08\x00\x03\x00\x01\x80\x03\x00\x06\x80\xc1\x00\x00" \
		"\x00\x0c\x00\x06\x00\x05\x00\x00\x02\x00\x01\xb4\x2b\xe8\x47\x40" \
		"\x00\x1c\x71\xc7\x00\x01\xff\xff\xed\x69\x58\xec\xc0\x06\x00\x08" \
		"\x00\x00\x07\xc4\x80\x00\x00\x04\xc0\x00\x00\x04\x80\x08\x00\x0b" \
		"\xc0\xc2\x0f\xc1\x80\x82\x40\x00\x80\x02\x00\x24\x40\x39\xcf\x32" \
		"\xd6\x60\xcf\xfa\x3f\x2f\xa9\x52\xed\x2b\xf2\xe6\x2f\xb7\x81\x96" \
		"\xf8\xda\xe9\xa0\x62\x01\x79\xe1\x0d\x5f\x38\xaa\x80\x04\x00\x08" \
		"\x00\x03\x00\x01\x80\x03\x00\x06\x80\xc1\x00\x00\x81\xe1\x1e\x81" \
		"\xea\x41\xeb\xf0\x12\xd9\x74\xbe\x13\xfd\x4b\x6c\x5c\xa2\x8f\x00";

	// WITH COMMON HEADER!
	char fuzz_cookie_ack[] = "\x13\x89\x13\x88\x54\xc2\x7c\x46\x00\x00\x00\x00\x0b\x00\x00\x04";

	// WITH COMMON HEADER!
	char fuzz_i_data[] = "\x13\x89\x13\x88\x07\x01\x6c\xd3\x00\x00\x00\x00\x40\x03" \
		"\x00\xdc\x2d\x2b\x46\xd4\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
		"\x00\x27\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41";

	// WITH COMMON HEADER!
	char fuzz_data[] = "\x13\x89\x13\x88\x27\xc4\xbf\xdf\x00\x00\x00\x00\x00\x03" \
		"\x00\xd8\x79\x64\xb7\xc1\x00\x00\x00\x00\x00\x00\x00\x27\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41";


	char fuzz_common_header[] = "\x13\x89\x13\x88\x54\xc2\x7c\x46\x00\x00\x00\x00";

	fuzzer_printf("LLVMFuzzerTestOneInput()\n");

#ifdef FUZZ_ALWAYS_INITIALIZE
	initialize_fuzzer();
#else
	if (!initialized) {
		initialized = initialize_fuzzer();
	}
#endif

	if (data_size < 5 || data_size > 65535) {
		// Skip too small and too large packets
		fuzzer_printf("data_size %zu makes no sense, skipping\n", data_size);
		return (0);
	}

	socket_client = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, 0);
	FUZZER_ASSERT(socket_client != NULL);

	usrsctp_set_non_blocking(socket_client, 1);

	// all max!
	memset(&initmsg, 1, sizeof(struct sctp_initmsg));
	result = usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, sizeof(struct sctp_initmsg));
	FUZZER_ASSERT(result == 0);

	so_linger.l_onoff = 1;
	so_linger.l_linger = 0;
	result = usrsctp_setsockopt(socket_client, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(struct linger));
	FUZZER_ASSERT(result == 0);

	memset(&event, 0, sizeof(event));
	event.se_on = 1;
	for (i = 0; i < (sizeof(event_types) / sizeof(uint16_t)); i++) {
		event.se_type = event_types[i];
		result = usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_EVENT, &event, sizeof(event));
		FUZZER_ASSERT(result == 0);
	}

	optval = 1;
	result = usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_RECVRCVINFO, &optval, sizeof(optval));
	FUZZER_ASSERT(result == 0);

	optval = 1;
	result = usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_RECVNXTINFO, &optval, sizeof(optval));
	FUZZER_ASSERT(result == 0);

#if defined(FUZZ_STREAM_RESET)
	assoc_val.assoc_id = SCTP_ALL_ASSOC;
	assoc_val.assoc_value = SCTP_ENABLE_RESET_STREAM_REQ | SCTP_ENABLE_RESET_ASSOC_REQ | SCTP_ENABLE_CHANGE_ASSOC_REQ;
	result = usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET, &assoc_val, sizeof(struct sctp_assoc_value));
	FUZZER_ASSERT(result == 0);
#endif // defined(FUZZ_STREAM_RESET)

#if defined(FUZZ_INTERLEAVING)
#if !defined(SCTP_INTERLEAVING_SUPPORTED)
#define SCTP_INTERLEAVING_SUPPORTED 0x00001206
#endif // !defined(SCTP_INTERLEAVING_SUPPORTED)

	if (data[0] & FUZZ_B_I_DATA_SUPPORT) {
		optval = 2;
		result = usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_FRAGMENT_INTERLEAVE, &optval, sizeof(optval));
		FUZZER_ASSERT(result == 0);

		memset(&assoc_val, 0, sizeof(assoc_val));
		assoc_val.assoc_value = 1;
		result = usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_INTERLEAVING_SUPPORTED, &assoc_val, sizeof(assoc_val));
		FUZZER_ASSERT(result == 0);
	}
#endif // defined(FUZZ_INTERLEAVING)

	optval = 1;
	result = usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_REUSE_PORT, &optval, sizeof(optval));
	FUZZER_ASSERT(result == 0);

	memset(&sconn, 0, sizeof(struct sockaddr_conn));
	sconn.sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
	sconn.sconn_len = sizeof(struct sockaddr_conn);
#endif // HAVE_SCONN_LEN
	sconn.sconn_port = htons(5000);
	sconn.sconn_addr = (void *)1;

	result = usrsctp_bind(socket_client, (struct sockaddr *)&sconn, sizeof(struct sockaddr_conn));
	FUZZER_ASSERT(result == 0);

	// Disable Nagle.
	optval = 1;
	result = usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_NODELAY, &optval, sizeof(optval));
	FUZZER_ASSERT(result == 0);

	usrsctp_set_upcall(socket_client, handle_upcall, NULL);

	memset(&sconn, 0, sizeof(struct sockaddr_conn));
	sconn.sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
	sconn.sconn_len = sizeof(struct sockaddr_conn);
#endif // HAVE_SCONN_LEN
	sconn.sconn_port = htons(5001);
	sconn.sconn_addr = (void *)1;

	fuzzer_printf("Calling usrsctp_connect()\n");
	result = usrsctp_connect(socket_client, (struct sockaddr *)&sconn, sizeof(struct sockaddr_conn));
	FUZZER_ASSERT(result == 0 || errno == EINPROGRESS);

	if (data[0] & FUZZ_B_INJECT_INIT_ACK) {
		fuzzer_printf("Injecting INIT-ACK\n");

		common_header = (struct sctp_common_header*) fuzz_init_ack;
		common_header->verification_tag = assoc_vtag;

		dump_packet(fuzz_init_ack, 448, SCTP_DUMP_INBOUND);
		usrsctp_conninput((void *)1, fuzz_init_ack, 448, 0);
	}

	if (data[0] & FUZZ_B_INJECT_COOKIE_ACK) {
		fuzzer_printf("Injecting COOKIE-ACK\n");

		common_header = (struct sctp_common_header*) fuzz_cookie_ack;
		common_header->verification_tag = assoc_vtag;

		dump_packet(fuzz_cookie_ack, 16, SCTP_DUMP_INBOUND);
		usrsctp_conninput((void *)1, fuzz_cookie_ack, 16, 0);
	}

	if (data[0] & FUZZ_B_INJECT_INIT_ACK &&
		data[0] & FUZZ_B_INJECT_COOKIE_ACK &&
		data[0] & FUZZ_B_SEND_DATA) {
		const char *sendbuffer = "Geologie ist keine richtige Wissenschaft!";
		fuzzer_printf("Calling usrsctp_sendv()\n");
		usrsctp_sendv(socket_client, sendbuffer, strlen(sendbuffer), NULL, 0, NULL, 0, SCTP_SENDV_NOINFO, 0);
	}

	// Required: INIT-ACK and COOKIE-ACK
	if (data[0] & FUZZ_B_INJECT_INIT_ACK &&
		data[0] & FUZZ_B_INJECT_COOKIE_ACK &&
		data[0] & FUZZ_B_SEND_STREAM_RESET) {
		fuzzer_printf("Sending Stream Reset for all streams\n");

		struct sctp_reset_streams srs;
		memset(&srs, 0, sizeof(struct sctp_reset_streams));
		srs.srs_flags = SCTP_STREAM_RESET_INCOMING | SCTP_STREAM_RESET_OUTGOING;
		result = usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_RESET_STREAMS, &srs, sizeof(struct sctp_reset_streams));
		FUZZER_ASSERT(result == 0);
	}

	// Required: INIT-ACK and COOKIE-ACK
	if (data[0] & FUZZ_B_INJECT_INIT_ACK &&
		data[0] & FUZZ_B_INJECT_COOKIE_ACK &&
		data[0] & FUZZ_B_INJECT_DATA) {

		if (data[0] & FUZZ_B_I_DATA_SUPPORT) {
			fuzzer_printf("Injecting I-DATA\n");
			common_header = (struct sctp_common_header*) fuzz_i_data;
			common_header->verification_tag = assoc_vtag;
			dump_packet(fuzz_i_data, 232, SCTP_DUMP_INBOUND);
			usrsctp_conninput((void *)1, fuzz_i_data, 232, 0);
		} else {
			fuzzer_printf("Injecting DATA\n");
			common_header = (struct sctp_common_header*) fuzz_data;
			common_header->verification_tag = assoc_vtag;
			dump_packet(fuzz_data, 228, SCTP_DUMP_INBOUND);
			usrsctp_conninput((void *)1, fuzz_data, 228, 0);
		}
	}

	if (data[0] & FUZZ_B_I_DATA_SUPPORT &&
		data[0] & FUZZ_B_SEND_DATA_FORCE) {
			const char *sendbuffer = "Geologie ist keine richtige Wissenschaft!";
			fuzzer_printf("Calling usrsctp_sendv()\n");
			usrsctp_sendv(socket_client, sendbuffer, strlen(sendbuffer), NULL, 0, NULL, 0, SCTP_SENDV_NOINFO, 0);
		}
	

	fuzz_packet_buffer = malloc(data_size - 1 + COMMON_HEADER_SIZE);
	memcpy(fuzz_packet_buffer, fuzz_common_header, COMMON_HEADER_SIZE); // common header
	memcpy(fuzz_packet_buffer + COMMON_HEADER_SIZE, data + 1, data_size - 1);

	common_header = (struct sctp_common_header*) fuzz_packet_buffer;
	common_header->verification_tag = assoc_vtag;

	fuzzer_printf("Injecting FUZZER-Packet\n");
	dump_packet(fuzz_packet_buffer, data_size - 1 + COMMON_HEADER_SIZE, SCTP_DUMP_INBOUND);
	usrsctp_conninput((void *)1, fuzz_packet_buffer, data_size - 1 + COMMON_HEADER_SIZE, 0);

	free(fuzz_packet_buffer);

	fuzzer_printf("Calling usrsctp_close()\n");
	usrsctp_close(socket_client);

#ifdef FUZZ_ALWAYS_INITIALIZE
	fuzzer_printf("Calling usrsctp_finish()\n");
	while (usrsctp_finish() != 0) {}
	fuzzer_printf("Done!\n");
#endif

	return (0);
}



