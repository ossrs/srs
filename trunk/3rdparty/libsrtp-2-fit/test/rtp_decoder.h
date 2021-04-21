/*
 * rtp_decoder.h
 *
 * decoder structures and functions for SRTP pcap decoder
 *
 * Bernardo Torres <bernardo@torresautomacao.com.br>
 *
 * Some structure and code from https://github.com/gteissier/srtp-decrypt
 *
 */
/*
 *
 * Copyright (c) 2001-2017 Cisco Systems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials provided
 *   with the distribution.
 *
 *   Neither the name of the Cisco Systems, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef RTP_DECODER_H
#define RTP_DECODER_H

#include "srtp_priv.h"
#include "rtp.h"

#define DEFAULT_RTP_OFFSET 42

typedef enum {
    mode_rtp = 0,
    mode_rtcp,
    mode_rtcp_mux,
} rtp_decoder_mode_t;

typedef struct rtp_decoder_ctx_t {
    srtp_policy_t policy;
    srtp_ctx_t *srtp_ctx;
    rtp_decoder_mode_t mode;
    int rtp_offset;
    struct timeval start_tv;
    int frame_nr;
    int error_cnt;
    int rtp_cnt;
    int rtcp_cnt;
} rtp_decoder_ctx_t;

typedef struct rtp_decoder_ctx_t *rtp_decoder_t;

/*
 * error to string
 */
void rtp_print_error(srtp_err_status_t status, char *message);

/*
 * prints the output of a random buffer in hexadecimal
 */
void hexdump(const void *ptr, size_t size);

/*
 * the function usage() prints an error message describing how this
 * program should be called, then calls exit()
 */
void usage(char *prog_name);

/*
 * transforms base64 key into octet
 */
char *decode_sdes(char *in, char *out);

/*
 * pcap handling
 */
void rtp_decoder_handle_pkt(u_char *arg,
                            const struct pcap_pkthdr *hdr,
                            const u_char *bytes);

rtp_decoder_t rtp_decoder_alloc(void);

void rtp_decoder_dealloc(rtp_decoder_t rtp_ctx);

int rtp_decoder_init(rtp_decoder_t dcdr,
                     srtp_policy_t policy,
                     rtp_decoder_mode_t mode,
                     int rtp_packet_offset);

int rtp_decoder_deinit(rtp_decoder_t decoder);

void rtp_decoder_srtp_log_handler(srtp_log_level_t level,
                                  const char *msg,
                                  void *data);

void rtp_decoder_srtp_log_handler(srtp_log_level_t level,
                                  const char *msg,
                                  void *data);

#endif /* RTP_DECODER_H */
