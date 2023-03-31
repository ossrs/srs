/*-
 * Copyright (c) 2020 Felix Weinrank
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __PROGRAMS_HELPER_H__
#define __PROGRAMS_HELPER_H__

#ifndef _WIN32
#define SCTP_PACKED __attribute__((packed))
#else
#pragma pack (push, 1)
#define SCTP_PACKED
#endif

struct sctp_chunk_header {
	uint8_t chunk_type;	/* chunk type */
	uint8_t chunk_flags;	/* chunk flags */
	uint16_t chunk_length;	/* chunk length */
	/* optional params follow */
} SCTP_PACKED;

struct sctp_init_chunk {
	struct sctp_chunk_header ch;
	uint32_t initiate_tag;	/* initiate tag */
	uint32_t a_rwnd;	/* a_rwnd */
	uint16_t num_outbound_streams;	/* OS */
	uint16_t num_inbound_streams;	/* MIS */
	uint32_t initial_tsn;	/* I-TSN */
	/* optional param's follow */
} SCTP_PACKED;

#ifdef _WIN32
#pragma pack(pop)
#endif

#undef SCTP_PACKED

void
handle_notification(union sctp_notification *notif, size_t n);
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

void
debug_printf(const char *format, ...);

void
debug_printf_clean(const char *format, ...);

void
debug_printf_stack(const char *format, ...);

void
debug_set_target(FILE *fp);

#define FUZZER_ASSERT(x) if (!(x)) { printf("USRSCTP assertion failed: function %s, file %s, line %d.\n", __PRETTY_FUNCTION__, __FILE__, __LINE__); abort(); }
 

#endif /* __PROGRAMS_HELPER_H__ */
