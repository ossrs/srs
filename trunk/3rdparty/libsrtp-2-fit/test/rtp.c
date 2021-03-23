/*
 * rtp.c
 *
 * library functions for the real-time transport protocol
 *
 * David A. McGrew
 * Cisco Systems, Inc.
 */

/*
 *
 * Copyright (c) 2001-2017, Cisco Systems, Inc.
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

#include "rtp.h"

#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#include "cipher_priv.h"

#define PRINT_DEBUG 0   /* set to 1 to print out debugging data */
#define VERBOSE_DEBUG 0 /* set to 1 to print out more data      */

int rtp_sendto(rtp_sender_t sender, const void *msg, int len)
{
    int octets_sent;
    srtp_err_status_t stat;
    int pkt_len = len + RTP_HEADER_LEN;

    /* marshal data */
    strncpy(sender->message.body, msg, len);

    /* update header */
    sender->message.header.seq = ntohs(sender->message.header.seq) + 1;
    sender->message.header.seq = htons(sender->message.header.seq);
    sender->message.header.ts = ntohl(sender->message.header.ts) + 1;
    sender->message.header.ts = htonl(sender->message.header.ts);

    /* apply srtp */
    stat = srtp_protect(sender->srtp_ctx, &sender->message.header, &pkt_len);
    if (stat) {
#if PRINT_DEBUG
        fprintf(stderr, "error: srtp protection failed with code %d\n", stat);
#endif
        return -1;
    }
#if VERBOSE_DEBUG
    srtp_print_packet(&sender->message.header, pkt_len);
#endif
    octets_sent =
        sendto(sender->socket, (void *)&sender->message, pkt_len, 0,
               (struct sockaddr *)&sender->addr, sizeof(struct sockaddr_in));

    if (octets_sent != pkt_len) {
#if PRINT_DEBUG
        fprintf(stderr, "error: couldn't send message %s", (char *)msg);
        perror("");
#endif
    }

    return octets_sent;
}

int rtp_recvfrom(rtp_receiver_t receiver, void *msg, int *len)
{
    int octets_recvd;
    srtp_err_status_t stat;

    octets_recvd = recvfrom(receiver->socket, (void *)&receiver->message, *len,
                            0, (struct sockaddr *)NULL, 0);

    if (octets_recvd == -1) {
        *len = 0;
        return -1;
    }

    /* verify rtp header */
    if (receiver->message.header.version != 2) {
        *len = 0;
        return -1;
    }

#if PRINT_DEBUG
    fprintf(stderr, "%d octets received from SSRC %u\n", octets_recvd,
            receiver->message.header.ssrc);
#endif
#if VERBOSE_DEBUG
    srtp_print_packet(&receiver->message.header, octets_recvd);
#endif

    /* apply srtp */
    stat = srtp_unprotect(receiver->srtp_ctx, &receiver->message.header,
                          &octets_recvd);
    if (stat) {
        fprintf(stderr, "error: srtp unprotection failed with code %d%s\n",
                stat,
                stat == srtp_err_status_replay_fail
                    ? " (replay check failed)"
                    : stat == srtp_err_status_auth_fail ? " (auth check failed)"
                                                        : "");
        return -1;
    }
    strncpy(msg, receiver->message.body, octets_recvd);

    return octets_recvd;
}

int rtp_sender_init(rtp_sender_t sender,
                    int sock,
                    struct sockaddr_in addr,
                    unsigned int ssrc)
{
    /* set header values */
    sender->message.header.ssrc = htonl(ssrc);
    sender->message.header.ts = 0;
    sender->message.header.seq = (uint16_t)srtp_cipher_rand_u32_for_tests();
    sender->message.header.m = 0;
    sender->message.header.pt = 0x1;
    sender->message.header.version = 2;
    sender->message.header.p = 0;
    sender->message.header.x = 0;
    sender->message.header.cc = 0;

    /* set other stuff */
    sender->socket = sock;
    sender->addr = addr;

    return 0;
}

int rtp_receiver_init(rtp_receiver_t rcvr,
                      int sock,
                      struct sockaddr_in addr,
                      unsigned int ssrc)
{
    /* set header values */
    rcvr->message.header.ssrc = htonl(ssrc);
    rcvr->message.header.ts = 0;
    rcvr->message.header.seq = 0;
    rcvr->message.header.m = 0;
    rcvr->message.header.pt = 0x1;
    rcvr->message.header.version = 2;
    rcvr->message.header.p = 0;
    rcvr->message.header.x = 0;
    rcvr->message.header.cc = 0;

    /* set other stuff */
    rcvr->socket = sock;
    rcvr->addr = addr;

    return 0;
}

int rtp_sender_init_srtp(rtp_sender_t sender, const srtp_policy_t *policy)
{
    return srtp_create(&sender->srtp_ctx, policy);
}

int rtp_sender_deinit_srtp(rtp_sender_t sender)
{
    return srtp_dealloc(sender->srtp_ctx);
}

int rtp_receiver_init_srtp(rtp_receiver_t sender, const srtp_policy_t *policy)
{
    return srtp_create(&sender->srtp_ctx, policy);
}

int rtp_receiver_deinit_srtp(rtp_receiver_t sender)
{
    return srtp_dealloc(sender->srtp_ctx);
}

rtp_sender_t rtp_sender_alloc(void)
{
    return (rtp_sender_t)malloc(sizeof(rtp_sender_ctx_t));
}

void rtp_sender_dealloc(rtp_sender_t rtp_ctx)
{
    free(rtp_ctx);
}

rtp_receiver_t rtp_receiver_alloc(void)
{
    return (rtp_receiver_t)malloc(sizeof(rtp_receiver_ctx_t));
}

void rtp_receiver_dealloc(rtp_receiver_t rtp_ctx)
{
    free(rtp_ctx);
}
