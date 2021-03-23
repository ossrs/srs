/*
 * dtls_srtp_driver.c
 *
 * test driver for DTLS-SRTP functions
 *
 * David McGrew
 * Cisco Systems, Inc.
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

#include <stdio.h>    /* for printf()          */
#include "getopt_s.h" /* for local getopt()    */
#include "srtp_priv.h"

srtp_err_status_t test_dtls_srtp(void);

srtp_hdr_t *srtp_create_test_packet(int pkt_octet_len, uint32_t ssrc);

void usage(char *prog_name)
{
    printf("usage: %s [ -t ][ -c ][ -v ][-d <debug_module> ]* [ -l ]\n"
           "  -d <mod>   turn on debugging module <mod>\n"
           "  -l         list debugging modules\n",
           prog_name);
    exit(1);
}

int main(int argc, char *argv[])
{
    unsigned do_list_mods = 0;
    int q;
    srtp_err_status_t err;

    printf("dtls_srtp_driver\n");

    /* initialize srtp library */
    err = srtp_init();
    if (err) {
        printf("error: srtp init failed with error code %d\n", err);
        exit(1);
    }

    /* process input arguments */
    while (1) {
        q = getopt_s(argc, argv, "ld:");
        if (q == -1)
            break;
        switch (q) {
        case 'l':
            do_list_mods = 1;
            break;
        case 'd':
            err = srtp_crypto_kernel_set_debug_module(optarg_s, 1);
            if (err) {
                printf("error: set debug module (%s) failed\n", optarg_s);
                exit(1);
            }
            break;
        default:
            usage(argv[0]);
        }
    }

    if (do_list_mods) {
        err = srtp_crypto_kernel_list_debug_modules();
        if (err) {
            printf("error: list of debug modules failed\n");
            exit(1);
        }
    }

    printf("testing dtls_srtp...");
    err = test_dtls_srtp();
    if (err) {
        printf("\nerror (code %d)\n", err);
        exit(1);
    }
    printf("passed\n");

    /* shut down srtp library */
    err = srtp_shutdown();
    if (err) {
        printf("error: srtp shutdown failed with error code %d\n", err);
        exit(1);
    }

    return 0;
}

srtp_err_status_t test_dtls_srtp(void)
{
    srtp_hdr_t *test_packet;
    int test_packet_len = 80;
    srtp_t s;
    srtp_policy_t policy;
    uint8_t key[SRTP_MAX_KEY_LEN];
    uint8_t salt[SRTP_MAX_KEY_LEN];
    unsigned int key_len, salt_len;
    srtp_profile_t profile;
    srtp_err_status_t err;

    memset(&policy, 0x0, sizeof(srtp_policy_t));

    /* create a 'null' SRTP session */
    err = srtp_create(&s, NULL);
    if (err)
        return err;

    /*
     * verify that packet-processing functions behave properly - we
     * expect that these functions will return srtp_err_status_no_ctx
     */
    test_packet = srtp_create_test_packet(80, 0xa5a5a5a5);
    if (test_packet == NULL)
        return srtp_err_status_alloc_fail;

    err = srtp_protect(s, test_packet, &test_packet_len);
    if (err != srtp_err_status_no_ctx) {
        printf("wrong return value from srtp_protect() (got code %d)\n", err);
        return srtp_err_status_fail;
    }

    err = srtp_unprotect(s, test_packet, &test_packet_len);
    if (err != srtp_err_status_no_ctx) {
        printf("wrong return value from srtp_unprotect() (got code %d)\n", err);
        return srtp_err_status_fail;
    }

    err = srtp_protect_rtcp(s, test_packet, &test_packet_len);
    if (err != srtp_err_status_no_ctx) {
        printf("wrong return value from srtp_protect_rtcp() (got code %d)\n",
               err);
        return srtp_err_status_fail;
    }

    err = srtp_unprotect_rtcp(s, test_packet, &test_packet_len);
    if (err != srtp_err_status_no_ctx) {
        printf("wrong return value from srtp_unprotect_rtcp() (got code %d)\n",
               err);
        return srtp_err_status_fail;
    }

    /*
     * set keys to known values for testing
     */
    profile = srtp_profile_aes128_cm_sha1_80;
    key_len = srtp_profile_get_master_key_length(profile);
    salt_len = srtp_profile_get_master_salt_length(profile);
    memset(key, 0xff, key_len);
    memset(salt, 0xee, salt_len);
    srtp_append_salt_to_key(key, key_len, salt, salt_len);
    policy.key = key;

    /* initialize SRTP policy from profile  */
    err = srtp_crypto_policy_set_from_profile_for_rtp(&policy.rtp, profile);
    if (err)
        return err;
    err = srtp_crypto_policy_set_from_profile_for_rtcp(&policy.rtcp, profile);
    if (err)
        return err;
    policy.ssrc.type = ssrc_any_inbound;
    policy.ekt = NULL;
    policy.window_size = 128;
    policy.allow_repeat_tx = 0;
    policy.next = NULL;

    err = srtp_add_stream(s, &policy);
    if (err)
        return err;

    err = srtp_dealloc(s);
    if (err)
        return err;

    free(test_packet);

    return srtp_err_status_ok;
}

/*
 * srtp_create_test_packet(len, ssrc) returns a pointer to a
 * (malloced) example RTP packet whose data field has the length given
 * by pkt_octet_len and the SSRC value ssrc.  The total length of the
 * packet is twelve octets longer, since the header is at the
 * beginning.  There is room at the end of the packet for a trailer,
 * and the four octets following the packet are filled with 0xff
 * values to enable testing for overwrites.
 *
 * note that the location of the test packet can (and should) be
 * deallocated with the free() call once it is no longer needed.
 */

srtp_hdr_t *srtp_create_test_packet(int pkt_octet_len, uint32_t ssrc)
{
    int i;
    uint8_t *buffer;
    srtp_hdr_t *hdr;
    int bytes_in_hdr = 12;

    /* allocate memory for test packet */
    hdr = malloc(pkt_octet_len + bytes_in_hdr + SRTP_MAX_TRAILER_LEN + 4);
    if (!hdr)
        return NULL;

    hdr->version = 2;            /* RTP version two     */
    hdr->p = 0;                  /* no padding needed   */
    hdr->x = 0;                  /* no header extension */
    hdr->cc = 0;                 /* no CSRCs            */
    hdr->m = 0;                  /* marker bit          */
    hdr->pt = 0xf;               /* payload type        */
    hdr->seq = htons(0x1234);    /* sequence number     */
    hdr->ts = htonl(0xdecafbad); /* timestamp           */
    hdr->ssrc = htonl(ssrc);     /* synch. source       */

    buffer = (uint8_t *)hdr;
    buffer += bytes_in_hdr;

    /* set RTP data to 0xab */
    for (i = 0; i < pkt_octet_len; i++)
        *buffer++ = 0xab;

    /* set post-data value to 0xffff to enable overrun checking */
    for (i = 0; i < SRTP_MAX_TRAILER_LEN + 4; i++)
        *buffer++ = 0xff;

    return hdr;
}
