/*
 * srtp_driver.c
 *
 * a test driver for libSRTP
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

#include <string.h>   /* for memcpy()          */
#include <time.h>     /* for clock()           */
#include <stdlib.h>   /* for malloc(), free()  */
#include <stdio.h>    /* for print(), fflush() */
#include "getopt_s.h" /* for local getopt()    */

#include "srtp_priv.h"
#include "util.h"

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#elif defined HAVE_WINSOCK2_H
#include <winsock2.h>
#endif

#define PRINT_REFERENCE_PACKET 1

srtp_err_status_t srtp_validate(void);

#ifdef GCM
srtp_err_status_t srtp_validate_gcm(void);
#endif

srtp_err_status_t srtp_validate_encrypted_extensions_headers(void);

#ifdef GCM
srtp_err_status_t srtp_validate_encrypted_extensions_headers_gcm(void);
#endif

srtp_err_status_t srtp_validate_aes_256(void);

srtp_err_status_t srtp_create_big_policy(srtp_policy_t **list);

srtp_err_status_t srtp_dealloc_big_policy(srtp_policy_t *list);

srtp_err_status_t srtp_test_empty_payload(void);

#ifdef GCM
srtp_err_status_t srtp_test_empty_payload_gcm(void);
#endif

srtp_err_status_t srtp_test_remove_stream(void);

srtp_err_status_t srtp_test_update(void);

srtp_err_status_t srtp_test_protect_trailer_length(void);

srtp_err_status_t srtp_test_protect_rtcp_trailer_length(void);

srtp_err_status_t srtp_test_get_roc(void);

srtp_err_status_t srtp_test_set_receiver_roc(void);

srtp_err_status_t srtp_test_set_sender_roc(void);

double srtp_bits_per_second(int msg_len_octets, const srtp_policy_t *policy);

double srtp_rejections_per_second(int msg_len_octets,
                                  const srtp_policy_t *policy);

void srtp_do_timing(const srtp_policy_t *policy);

void srtp_do_rejection_timing(const srtp_policy_t *policy);

srtp_err_status_t srtp_test(const srtp_policy_t *policy,
                            int extension_header,
                            int mki_index);

srtp_err_status_t srtcp_test(const srtp_policy_t *policy, int mki_index);

srtp_err_status_t srtp_session_print_policy(srtp_t srtp);

srtp_err_status_t srtp_print_policy(const srtp_policy_t *policy);

char *srtp_packet_to_string(srtp_hdr_t *hdr, int packet_len);

double mips_estimate(int num_trials, int *ignore);

#define TEST_MKI_ID_SIZE 4

extern uint8_t test_key[46];
extern uint8_t test_key_2[46];
extern uint8_t test_mki_id[TEST_MKI_ID_SIZE];
extern uint8_t test_mki_id_2[TEST_MKI_ID_SIZE];

// clang-format off
srtp_master_key_t master_key_1 = {
    test_key,
    test_mki_id,
    TEST_MKI_ID_SIZE
};

srtp_master_key_t master_key_2 = {
    test_key_2,
    test_mki_id_2,
    TEST_MKI_ID_SIZE
};

srtp_master_key_t *test_keys[2] = {
    &master_key_1,
    &master_key_2
};
// clang-format on

void usage(char *prog_name)
{
    printf("usage: %s [ -t ][ -c ][ -v ][ -o ][-d <debug_module> ]* [ -l ]\n"
           "  -t         run timing test\n"
           "  -r         run rejection timing test\n"
           "  -c         run codec timing test\n"
           "  -v         run validation tests\n"
           "  -o         output logging to stdout\n"
           "  -d <mod>   turn on debugging module <mod>\n"
           "  -l         list debugging modules\n",
           prog_name);
    exit(1);
}

void log_handler(srtp_log_level_t level, const char *msg, void *data)
{
    char level_char = '?';
    switch (level) {
    case srtp_log_level_error:
        level_char = 'e';
        break;
    case srtp_log_level_warning:
        level_char = 'w';
        break;
    case srtp_log_level_info:
        level_char = 'i';
        break;
    case srtp_log_level_debug:
        level_char = 'd';
        break;
    }
    printf("SRTP-LOG [%c]: %s\n", level_char, msg);
}

/*
 * The policy_array is a null-terminated array of policy structs. it
 * is declared at the end of this file
 */

extern const srtp_policy_t *policy_array[];

/* the wildcard_policy is declared below; it has a wildcard ssrc */

extern const srtp_policy_t wildcard_policy;

/*
 * mod_driver debug module - debugging module for this test driver
 *
 * we use the crypto_kernel debugging system in this driver, which
 * makes the interface uniform and increases portability
 */

srtp_debug_module_t mod_driver = {
    0,       /* debugging is off by default */
    "driver" /* printable name for module   */
};

int main(int argc, char *argv[])
{
    int q;
    unsigned do_timing_test = 0;
    unsigned do_rejection_test = 0;
    unsigned do_codec_timing = 0;
    unsigned do_validation = 0;
    unsigned do_list_mods = 0;
    unsigned do_log_stdout = 0;
    srtp_err_status_t status;

    /*
     * verify that the compiler has interpreted the header data
     * structure srtp_hdr_t correctly
     */
    if (sizeof(srtp_hdr_t) != 12) {
        printf("error: srtp_hdr_t has incorrect size"
               "(size is %ld bytes, expected 12)\n",
               (long)sizeof(srtp_hdr_t));
        exit(1);
    }

    /* initialize srtp library */
    status = srtp_init();
    if (status) {
        printf("error: srtp init failed with error code %d\n", status);
        exit(1);
    }

    /*  load srtp_driver debug module */
    status = srtp_crypto_kernel_load_debug_module(&mod_driver);
    if (status) {
        printf("error: load of srtp_driver debug module failed "
               "with error code %d\n",
               status);
        exit(1);
    }

    /* process input arguments */
    while (1) {
        q = getopt_s(argc, argv, "trcvold:");
        if (q == -1) {
            break;
        }
        switch (q) {
        case 't':
            do_timing_test = 1;
            break;
        case 'r':
            do_rejection_test = 1;
            break;
        case 'c':
            do_codec_timing = 1;
            break;
        case 'v':
            do_validation = 1;
            break;
        case 'o':
            do_log_stdout = 1;
            break;
        case 'l':
            do_list_mods = 1;
            break;
        case 'd':
            status = srtp_set_debug_module(optarg_s, 1);
            if (status) {
                printf("error: set debug module (%s) failed\n", optarg_s);
                exit(1);
            }
            break;
        default:
            usage(argv[0]);
        }
    }

    if (!do_validation && !do_timing_test && !do_codec_timing &&
        !do_list_mods && !do_rejection_test) {
        usage(argv[0]);
    }

    if (do_log_stdout) {
        status = srtp_install_log_handler(log_handler, NULL);
        if (status) {
            printf("error: install log handler failed\n");
            exit(1);
        }
    }

    if (do_list_mods) {
        status = srtp_list_debug_modules();
        if (status) {
            printf("error: list of debug modules failed\n");
            exit(1);
        }
    }

    if (do_validation) {
        const srtp_policy_t **policy = policy_array;
        srtp_policy_t *big_policy;

        /* loop over policy array, testing srtp and srtcp for each policy */
        while (*policy != NULL) {
            printf("testing srtp_protect and srtp_unprotect\n");
            if (srtp_test(*policy, 0, -1) == srtp_err_status_ok) {
                printf("passed\n\n");
            } else {
                printf("failed\n");
                exit(1);
            }

            printf("testing srtp_protect and srtp_unprotect with encrypted "
                   "extensions headers\n");
            if (srtp_test(*policy, 1, -1) == srtp_err_status_ok) {
                printf("passed\n\n");
            } else {
                printf("failed\n");
                exit(1);
            }
            printf("testing srtp_protect_rtcp and srtp_unprotect_rtcp\n");
            if (srtcp_test(*policy, -1) == srtp_err_status_ok) {
                printf("passed\n\n");
            } else {
                printf("failed\n");
                exit(1);
            }
            printf("testing srtp_protect_rtp and srtp_unprotect_rtp with MKI "
                   "index set to 0\n");
            if (srtp_test(*policy, 0, 0) == srtp_err_status_ok) {
                printf("passed\n\n");
            } else {
                printf("failed\n");
                exit(1);
            }
            printf("testing srtp_protect_rtp and srtp_unprotect_rtp with MKI "
                   "index set to 1\n");
            if (srtp_test(*policy, 0, 1) == srtp_err_status_ok) {
                printf("passed\n\n");
            } else {
                printf("failed\n");
                exit(1);
            }

            printf("testing srtp_protect_rtcp and srtp_unprotect_rtcp with MKI "
                   "index set to 0\n");
            if (srtcp_test(*policy, 0) == srtp_err_status_ok) {
                printf("passed\n\n");
            } else {
                printf("failed\n");
                exit(1);
            }
            printf("testing srtp_protect_rtcp and srtp_unprotect_rtcp with MKI "
                   "index set to 1\n");
            if (srtcp_test(*policy, 1) == srtp_err_status_ok) {
                printf("passed\n\n");
            } else {
                printf("failed\n");
                exit(1);
            }
            policy++;
        }

        /* create a big policy list and run tests on it */
        status = srtp_create_big_policy(&big_policy);
        if (status) {
            printf("unexpected failure with error code %d\n", status);
            exit(1);
        }
        printf("testing srtp_protect and srtp_unprotect with big policy\n");
        if (srtp_test(big_policy, 0, -1) == srtp_err_status_ok) {
            printf("passed\n\n");
        } else {
            printf("failed\n");
            exit(1);
        }
        printf("testing srtp_protect and srtp_unprotect with big policy and "
               "encrypted extensions headers\n");
        if (srtp_test(big_policy, 1, -1) == srtp_err_status_ok) {
            printf("passed\n\n");
        } else {
            printf("failed\n");
            exit(1);
        }
        status = srtp_dealloc_big_policy(big_policy);
        if (status) {
            printf("unexpected failure with error code %d\n", status);
            exit(1);
        }

        /* run test on wildcard policy */
        printf("testing srtp_protect and srtp_unprotect on "
               "wildcard ssrc policy\n");
        if (srtp_test(&wildcard_policy, 0, -1) == srtp_err_status_ok) {
            printf("passed\n\n");
        } else {
            printf("failed\n");
            exit(1);
        }
        printf("testing srtp_protect and srtp_unprotect on "
               "wildcard ssrc policy and encrypted extensions headers\n");
        if (srtp_test(&wildcard_policy, 1, -1) == srtp_err_status_ok) {
            printf("passed\n\n");
        } else {
            printf("failed\n");
            exit(1);
        }

        /*
         * run validation test against the reference packets - note
         * that this test only covers the default policy
         */
        printf("testing srtp_protect and srtp_unprotect against "
               "reference packet\n");
        if (srtp_validate() == srtp_err_status_ok) {
            printf("passed\n\n");
        } else {
            printf("failed\n");
            exit(1);
        }

#ifdef GCM
        printf("testing srtp_protect and srtp_unprotect against "
               "reference packet using GCM\n");
        if (srtp_validate_gcm() == srtp_err_status_ok) {
            printf("passed\n\n");
        } else {
            printf("failed\n");
            exit(1);
        }
#endif

        printf("testing srtp_protect and srtp_unprotect against "
               "reference packet with encrypted extensions headers\n");
        if (srtp_validate_encrypted_extensions_headers() == srtp_err_status_ok)
            printf("passed\n\n");
        else {
            printf("failed\n");
            exit(1);
        }

#ifdef GCM
        printf("testing srtp_protect and srtp_unprotect against "
               "reference packet with encrypted extension headers (GCM)\n");
        if (srtp_validate_encrypted_extensions_headers_gcm() ==
            srtp_err_status_ok) {
            printf("passed\n\n");
        } else {
            printf("failed\n");
            exit(1);
        }
#endif

        /*
         * run validation test against the reference packets for
         * AES-256
         */
        printf("testing srtp_protect and srtp_unprotect against "
               "reference packet (AES-256)\n");
        if (srtp_validate_aes_256() == srtp_err_status_ok) {
            printf("passed\n\n");
        } else {
            printf("failed\n");
            exit(1);
        }

        /*
         * test packets with empty payload
         */
        printf("testing srtp_protect and srtp_unprotect against "
               "packet with empty payload\n");
        if (srtp_test_empty_payload() == srtp_err_status_ok) {
            printf("passed\n");
        } else {
            printf("failed\n");
            exit(1);
        }
#ifdef GCM
        printf("testing srtp_protect and srtp_unprotect against "
               "packet with empty payload (GCM)\n");
        if (srtp_test_empty_payload_gcm() == srtp_err_status_ok) {
            printf("passed\n");
        } else {
            printf("failed\n");
            exit(1);
        }
#endif

        /*
         * test the function srtp_remove_stream()
         */
        printf("testing srtp_remove_stream()...");
        if (srtp_test_remove_stream() == srtp_err_status_ok) {
            printf("passed\n");
        } else {
            printf("failed\n");
            exit(1);
        }

        /*
         * test the function srtp_update()
         */
        printf("testing srtp_update()...");
        if (srtp_test_update() == srtp_err_status_ok) {
            printf("passed\n");
        } else {
            printf("failed\n");
            exit(1);
        }

        /*
         * test the functions srtp_get_protect_trailer_length
         * and srtp_get_protect_rtcp_trailer_length
         */
        printf("testing srtp_get_protect_trailer_length()...");
        if (srtp_test_protect_trailer_length() == srtp_err_status_ok) {
            printf("passed\n");
        } else {
            printf("failed\n");
            exit(1);
        }

        printf("testing srtp_get_protect_rtcp_trailer_length()...");
        if (srtp_test_protect_rtcp_trailer_length() == srtp_err_status_ok) {
            printf("passed\n");
        } else {
            printf("failed\n");
            exit(1);
        }

        printf("testing srtp_test_get_roc()...");
        if (srtp_test_get_roc() == srtp_err_status_ok) {
            printf("passed\n");
        } else {
            printf("failed\n");
            exit(1);
        }

        printf("testing srtp_test_set_receiver_roc()...");
        if (srtp_test_set_receiver_roc() == srtp_err_status_ok) {
            printf("passed\n");
        } else {
            printf("failed\n");
            exit(1);
        }

        printf("testing srtp_test_set_sender_roc()...");
        if (srtp_test_set_sender_roc() == srtp_err_status_ok) {
            printf("passed\n");
        } else {
            printf("failed\n");
            exit(1);
        }
    }

    if (do_timing_test) {
        const srtp_policy_t **policy = policy_array;

        /* loop over policies, run timing test for each */
        while (*policy != NULL) {
            srtp_print_policy(*policy);
            srtp_do_timing(*policy);
            policy++;
        }
    }

    if (do_rejection_test) {
        const srtp_policy_t **policy = policy_array;

        /* loop over policies, run rejection timing test for each */
        while (*policy != NULL) {
            srtp_print_policy(*policy);
            srtp_do_rejection_timing(*policy);
            policy++;
        }
    }

    if (do_codec_timing) {
        srtp_policy_t policy;
        int ignore;
        double mips_value = mips_estimate(1000000000, &ignore);

        memset(&policy, 0, sizeof(policy));
        srtp_crypto_policy_set_rtp_default(&policy.rtp);
        srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
        policy.ssrc.type = ssrc_specific;
        policy.ssrc.value = 0xdecafbad;
        policy.key = test_key;
        policy.ekt = NULL;
        policy.window_size = 128;
        policy.allow_repeat_tx = 0;
        policy.next = NULL;

        printf("mips estimate: %e\n", mips_value);

        printf("testing srtp processing time for voice codecs:\n");
        printf("codec\t\tlength (octets)\t\tsrtp instructions/second\n");
        printf("G.711\t\t%d\t\t\t%e\n", 80,
               (double)mips_value * (80 * 8) /
                   srtp_bits_per_second(80, &policy) / .01);
        printf("G.711\t\t%d\t\t\t%e\n", 160,
               (double)mips_value * (160 * 8) /
                   srtp_bits_per_second(160, &policy) / .02);
        printf("G.726-32\t%d\t\t\t%e\n", 40,
               (double)mips_value * (40 * 8) /
                   srtp_bits_per_second(40, &policy) / .01);
        printf("G.726-32\t%d\t\t\t%e\n", 80,
               (double)mips_value * (80 * 8) /
                   srtp_bits_per_second(80, &policy) / .02);
        printf("G.729\t\t%d\t\t\t%e\n", 10,
               (double)mips_value * (10 * 8) /
                   srtp_bits_per_second(10, &policy) / .01);
        printf("G.729\t\t%d\t\t\t%e\n", 20,
               (double)mips_value * (20 * 8) /
                   srtp_bits_per_second(20, &policy) / .02);
        printf("Wideband\t%d\t\t\t%e\n", 320,
               (double)mips_value * (320 * 8) /
                   srtp_bits_per_second(320, &policy) / .01);
        printf("Wideband\t%d\t\t\t%e\n", 640,
               (double)mips_value * (640 * 8) /
                   srtp_bits_per_second(640, &policy) / .02);
    }

    status = srtp_shutdown();
    if (status) {
        printf("error: srtp shutdown failed with error code %d\n", status);
        exit(1);
    }

    return 0;
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

srtp_hdr_t *srtp_create_test_packet(int pkt_octet_len,
                                    uint32_t ssrc,
                                    int *pkt_len)
{
    int i;
    uint8_t *buffer;
    srtp_hdr_t *hdr;
    int bytes_in_hdr = 12;

    /* allocate memory for test packet */
    hdr = (srtp_hdr_t *)malloc(pkt_octet_len + bytes_in_hdr +
                               SRTP_MAX_TRAILER_LEN + 4);
    if (!hdr) {
        return NULL;
    }

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
    for (i = 0; i < pkt_octet_len; i++) {
        *buffer++ = 0xab;
    }

    /* set post-data value to 0xffff to enable overrun checking */
    for (i = 0; i < SRTP_MAX_TRAILER_LEN + 4; i++) {
        *buffer++ = 0xff;
    }

    *pkt_len = bytes_in_hdr + pkt_octet_len;

    return hdr;
}

static srtp_hdr_t *srtp_create_test_packet_extended(int pkt_octet_len,
                                                    uint32_t ssrc,
                                                    uint16_t seq,
                                                    uint32_t ts,
                                                    int *pkt_len)
{
    srtp_hdr_t *hdr;

    hdr = srtp_create_test_packet(pkt_octet_len, ssrc, pkt_len);
    if (hdr == NULL)
        return hdr;

    hdr->seq = htons(seq);
    hdr->ts = htonl(ts);
    return hdr;
}

srtp_hdr_t *srtp_create_test_packet_ext_hdr(int pkt_octet_len,
                                            uint32_t ssrc,
                                            int *pkt_len)
{
    int i;
    uint8_t *buffer;
    srtp_hdr_t *hdr;
    int bytes_in_hdr = 12;
    uint8_t extension_header[12] = { /* one-byte header */
                                     0xbe, 0xde,
                                     /* size */
                                     0x00, 0x02,
                                     /* id 1, length 1 (i.e. 2 bytes) */
                                     0x11,
                                     /* payload */
                                     0xca, 0xfe,
                                     /* padding */
                                     0x00,
                                     /* id 2, length 0 (i.e. 1 byte) */
                                     0x20,
                                     /* payload */
                                     0xba,
                                     /* padding */
                                     0x00, 0x00
    };

    /* allocate memory for test packet */
    hdr = (srtp_hdr_t *)malloc(pkt_octet_len + bytes_in_hdr +
                               sizeof(extension_header) + SRTP_MAX_TRAILER_LEN +
                               4);
    if (!hdr)
        return NULL;

    hdr->version = 2;            /* RTP version two     */
    hdr->p = 0;                  /* no padding needed   */
    hdr->x = 1;                  /* no header extension */
    hdr->cc = 0;                 /* no CSRCs            */
    hdr->m = 0;                  /* marker bit          */
    hdr->pt = 0xf;               /* payload type        */
    hdr->seq = htons(0x1234);    /* sequence number     */
    hdr->ts = htonl(0xdecafbad); /* timestamp           */
    hdr->ssrc = htonl(ssrc);     /* synch. source       */

    buffer = (uint8_t *)hdr;
    buffer += bytes_in_hdr;

    memcpy(buffer, extension_header, sizeof(extension_header));
    buffer += sizeof(extension_header);

    /* set RTP data to 0xab */
    for (i = 0; i < pkt_octet_len; i++)
        *buffer++ = 0xab;

    /* set post-data value to 0xffff to enable overrun checking */
    for (i = 0; i < SRTP_MAX_TRAILER_LEN + 4; i++)
        *buffer++ = 0xff;

    *pkt_len = bytes_in_hdr + sizeof(extension_header) + pkt_octet_len;

    return hdr;
}

void srtp_do_timing(const srtp_policy_t *policy)
{
    int len;

    /*
     * note: the output of this function is formatted so that it
     * can be used in gnuplot.  '#' indicates a comment, and "\r\n"
     * terminates a record
     */

    printf("# testing srtp throughput:\r\n");
    printf("# mesg length (octets)\tthroughput (megabits per second)\r\n");

    for (len = 16; len <= 2048; len *= 2) {
        printf("%d\t\t\t%f\r\n", len,
               srtp_bits_per_second(len, policy) / 1.0E6);
    }

    /* these extra linefeeds let gnuplot know that a dataset is done */
    printf("\r\n\r\n");
}

void srtp_do_rejection_timing(const srtp_policy_t *policy)
{
    int len;

    /*
     * note: the output of this function is formatted so that it
     * can be used in gnuplot.  '#' indicates a comment, and "\r\n"
     * terminates a record
     */

    printf("# testing srtp rejection throughput:\r\n");
    printf("# mesg length (octets)\trejections per second\r\n");

    for (len = 8; len <= 2048; len *= 2) {
        printf("%d\t\t\t%e\r\n", len, srtp_rejections_per_second(len, policy));
    }

    /* these extra linefeeds let gnuplot know that a dataset is done */
    printf("\r\n\r\n");
}

#define MAX_MSG_LEN 1024

double srtp_bits_per_second(int msg_len_octets, const srtp_policy_t *policy)
{
    srtp_t srtp;
    srtp_hdr_t *mesg;
    int i;
    clock_t timer;
    int num_trials = 100000;
    int input_len, len;
    uint32_t ssrc;
    srtp_err_status_t status;

    /*
     * allocate and initialize an srtp session
     */
    status = srtp_create(&srtp, policy);
    if (status) {
        printf("error: srtp_create() failed with error code %d\n", status);
        exit(1);
    }

    /*
     * if the ssrc is unspecified, use a predetermined one
     */
    if (policy->ssrc.type != ssrc_specific) {
        ssrc = 0xdeadbeef;
    } else {
        ssrc = policy->ssrc.value;
    }

    /*
     * create a test packet
     */
    mesg = srtp_create_test_packet(msg_len_octets, ssrc, &input_len);
    if (mesg == NULL) {
        return 0.0; /* indicate failure by returning zero */
    }
    timer = clock();
    for (i = 0; i < num_trials; i++) {
        len = input_len;
        /* srtp protect message */
        status = srtp_protect(srtp, mesg, &len);
        if (status) {
            printf("error: srtp_protect() failed with error code %d\n", status);
            exit(1);
        }

        /* increment message number */
        {
            /* hack sequence to avoid problems with macros for htons/ntohs on
             * some systems */
            short new_seq = ntohs(mesg->seq) + 1;
            mesg->seq = htons(new_seq);
        }
    }
    timer = clock() - timer;

    free(mesg);

    status = srtp_dealloc(srtp);
    if (status) {
        printf("error: srtp_dealloc() failed with error code %d\n", status);
        exit(1);
    }

    return (double)(msg_len_octets)*8 * num_trials * CLOCKS_PER_SEC / timer;
}

double srtp_rejections_per_second(int msg_len_octets,
                                  const srtp_policy_t *policy)
{
    srtp_ctx_t *srtp;
    srtp_hdr_t *mesg;
    int i;
    int len;
    clock_t timer;
    int num_trials = 1000000;
    uint32_t ssrc = policy->ssrc.value;
    srtp_err_status_t status;

    /*
     * allocate and initialize an srtp session
     */
    status = srtp_create(&srtp, policy);
    if (status) {
        printf("error: srtp_create() failed with error code %d\n", status);
        exit(1);
    }

    mesg = srtp_create_test_packet(msg_len_octets, ssrc, &len);
    if (mesg == NULL) {
        return 0.0; /* indicate failure by returning zero */
    }
    srtp_protect(srtp, (srtp_hdr_t *)mesg, &len);

    timer = clock();
    for (i = 0; i < num_trials; i++) {
        len = msg_len_octets;
        srtp_unprotect(srtp, (srtp_hdr_t *)mesg, &len);
    }
    timer = clock() - timer;

    free(mesg);

    status = srtp_dealloc(srtp);
    if (status) {
        printf("error: srtp_dealloc() failed with error code %d\n", status);
        exit(1);
    }

    return (double)num_trials * CLOCKS_PER_SEC / timer;
}

void err_check(srtp_err_status_t s)
{
    if (s == srtp_err_status_ok) {
        return;
    } else {
        fprintf(stderr, "error: unexpected srtp failure (code %d)\n", s);
    }
    exit(1);
}

srtp_err_status_t srtp_test_call_protect(srtp_t srtp_sender,
                                         srtp_hdr_t *hdr,
                                         int *len,
                                         int mki_index)
{
    if (mki_index == -1) {
        return srtp_protect(srtp_sender, hdr, len);
    } else {
        return srtp_protect_mki(srtp_sender, hdr, len, 1, mki_index);
    }
}

srtp_err_status_t srtp_test_call_protect_rtcp(srtp_t srtp_sender,
                                              srtp_hdr_t *hdr,
                                              int *len,
                                              int mki_index)
{
    if (mki_index == -1) {
        return srtp_protect_rtcp(srtp_sender, hdr, len);
    } else {
        return srtp_protect_rtcp_mki(srtp_sender, hdr, len, 1, mki_index);
    }
}

srtp_err_status_t srtp_test_call_unprotect(srtp_t srtp_sender,
                                           srtp_hdr_t *hdr,
                                           int *len,
                                           int use_mki)
{
    if (use_mki == -1) {
        return srtp_unprotect(srtp_sender, hdr, len);
    } else {
        return srtp_unprotect_mki(srtp_sender, hdr, len, use_mki);
    }
}

srtp_err_status_t srtp_test_call_unprotect_rtcp(srtp_t srtp_sender,
                                                srtp_hdr_t *hdr,
                                                int *len,
                                                int use_mki)
{
    if (use_mki == -1) {
        return srtp_unprotect_rtcp(srtp_sender, hdr, len);
    } else {
        return srtp_unprotect_rtcp_mki(srtp_sender, hdr, len, use_mki);
    }
}

srtp_err_status_t srtp_test(const srtp_policy_t *policy,
                            int extension_header,
                            int mki_index)
{
    int i;
    srtp_t srtp_sender;
    srtp_t srtp_rcvr;
    srtp_err_status_t status = srtp_err_status_ok;
    srtp_hdr_t *hdr, *hdr2;
    uint8_t hdr_enc[64];
    uint8_t *pkt_end;
    int msg_len_octets, msg_len_enc, msg_len;
    int len, len2;
    uint32_t tag_length;
    uint32_t ssrc;
    srtp_policy_t *rcvr_policy;
    srtp_policy_t tmp_policy;
    int header = 1;
    int use_mki = 0;

    if (mki_index >= 0)
        use_mki = 1;

    if (extension_header) {
        memcpy(&tmp_policy, policy, sizeof(srtp_policy_t));
        tmp_policy.enc_xtn_hdr = &header;
        tmp_policy.enc_xtn_hdr_count = 1;
        err_check(srtp_create(&srtp_sender, &tmp_policy));
    } else {
        err_check(srtp_create(&srtp_sender, policy));
    }

    /* print out policy */
    err_check(srtp_session_print_policy(srtp_sender));

    /*
     * initialize data buffer, using the ssrc in the policy unless that
     * value is a wildcard, in which case we'll just use an arbitrary
     * one
     */
    if (policy->ssrc.type != ssrc_specific) {
        ssrc = 0xdecafbad;
    } else {
        ssrc = policy->ssrc.value;
    }
    msg_len_octets = 28;
    if (extension_header) {
        hdr = srtp_create_test_packet_ext_hdr(msg_len_octets, ssrc, &len);
        hdr2 = srtp_create_test_packet_ext_hdr(msg_len_octets, ssrc, &len2);
    } else {
        hdr = srtp_create_test_packet(msg_len_octets, ssrc, &len);
        hdr2 = srtp_create_test_packet(msg_len_octets, ssrc, &len2);
    }

    /* save original msg len */
    msg_len = len;

    if (hdr == NULL) {
        free(hdr2);
        return srtp_err_status_alloc_fail;
    }
    if (hdr2 == NULL) {
        free(hdr);
        return srtp_err_status_alloc_fail;
    }

    debug_print(mod_driver, "before protection:\n%s",
                srtp_packet_to_string(hdr, len));

#if PRINT_REFERENCE_PACKET
    debug_print(mod_driver, "reference packet before protection:\n%s",
                octet_string_hex_string((uint8_t *)hdr, len));
#endif
    err_check(srtp_test_call_protect(srtp_sender, hdr, &len, mki_index));

    debug_print(mod_driver, "after protection:\n%s",
                srtp_packet_to_string(hdr, len));
#if PRINT_REFERENCE_PACKET
    debug_print(mod_driver, "after protection:\n%s",
                octet_string_hex_string((uint8_t *)hdr, len));
#endif

    /* save protected message and length */
    memcpy(hdr_enc, hdr, len);
    msg_len_enc = len;

    /*
     * check for overrun of the srtp_protect() function
     *
     * The packet is followed by a value of 0xfffff; if the value of the
     * data following the packet is different, then we know that the
     * protect function is overwriting the end of the packet.
     */
    err_check(srtp_get_protect_trailer_length(srtp_sender, use_mki, mki_index,
                                              &tag_length));
    pkt_end = (uint8_t *)hdr + msg_len + tag_length;
    for (i = 0; i < 4; i++) {
        if (pkt_end[i] != 0xff) {
            fprintf(stdout, "overwrite in srtp_protect() function "
                            "(expected %x, found %x in trailing octet %d)\n",
                    0xff, ((uint8_t *)hdr)[i], i);
            free(hdr);
            free(hdr2);
            return srtp_err_status_algo_fail;
        }
    }

    /*
     * if the policy includes confidentiality, check that ciphertext is
     * different than plaintext
     *
     * Note that this check will give false negatives, with some small
     * probability, especially if the packets are short.  For that
     * reason, we skip this check if the plaintext is less than four
     * octets long.
     */
    if ((policy->rtp.sec_serv & sec_serv_conf) && (msg_len_octets >= 4)) {
        printf("testing that ciphertext is distinct from plaintext...");
        status = srtp_err_status_algo_fail;
        for (i = 12; i < msg_len_octets + 12; i++) {
            if (((uint8_t *)hdr)[i] != ((uint8_t *)hdr2)[i]) {
                status = srtp_err_status_ok;
            }
        }
        if (status) {
            printf("failed\n");
            free(hdr);
            free(hdr2);
            return status;
        }
        printf("passed\n");
    }

    /*
     * if the policy uses a 'wildcard' ssrc, then we need to make a copy
     * of the policy that changes the direction to inbound
     *
     * we always copy the policy into the rcvr_policy, since otherwise
     * the compiler would fret about the constness of the policy
     */
    rcvr_policy = (srtp_policy_t *)malloc(sizeof(srtp_policy_t));
    if (rcvr_policy == NULL) {
        free(hdr);
        free(hdr2);
        return srtp_err_status_alloc_fail;
    }
    if (extension_header) {
        memcpy(rcvr_policy, &tmp_policy, sizeof(srtp_policy_t));
        if (tmp_policy.ssrc.type == ssrc_any_outbound) {
            rcvr_policy->ssrc.type = ssrc_any_inbound;
        }
    } else {
        memcpy(rcvr_policy, policy, sizeof(srtp_policy_t));
        if (policy->ssrc.type == ssrc_any_outbound) {
            rcvr_policy->ssrc.type = ssrc_any_inbound;
        }
    }

    err_check(srtp_create(&srtp_rcvr, rcvr_policy));

    err_check(srtp_test_call_unprotect(srtp_rcvr, hdr, &len, use_mki));

    debug_print(mod_driver, "after unprotection:\n%s",
                srtp_packet_to_string(hdr, len));

    /* verify that the unprotected packet matches the origial one */
    for (i = 0; i < len; i++) {
        if (((uint8_t *)hdr)[i] != ((uint8_t *)hdr2)[i]) {
            fprintf(stdout, "mismatch at octet %d\n", i);
            status = srtp_err_status_algo_fail;
        }
    }
    if (status) {
        free(hdr);
        free(hdr2);
        free(rcvr_policy);
        return status;
    }

    /*
     * if the policy includes authentication, then test for false positives
     */
    if (policy->rtp.sec_serv & sec_serv_auth) {
        char *data = ((char *)hdr) + (extension_header ? 24 : 12);

        printf("testing for false positives in replay check...");

        /* unprotect a second time - should fail with a replay error */
        status =
            srtp_test_call_unprotect(srtp_rcvr, hdr, &msg_len_enc, use_mki);
        if (status != srtp_err_status_replay_fail) {
            printf("failed with error code %d\n", status);
            free(hdr);
            free(hdr2);
            free(rcvr_policy);
            return status;
        } else {
            printf("passed\n");
        }

        printf("testing for false positives in auth check...");

        /* increment sequence number in header */
        hdr->seq++;

        /* apply protection */
        err_check(srtp_test_call_protect(srtp_sender, hdr, &len, mki_index));

        /* flip bits in packet */
        data[0] ^= 0xff;

        /* unprotect, and check for authentication failure */
        status = srtp_test_call_unprotect(srtp_rcvr, hdr, &len, use_mki);
        if (status != srtp_err_status_auth_fail) {
            printf("failed\n");
            free(hdr);
            free(hdr2);
            free(rcvr_policy);
            return status;
        } else {
            printf("passed\n");
        }
    }

    err_check(srtp_dealloc(srtp_sender));
    err_check(srtp_dealloc(srtp_rcvr));

    free(hdr);
    free(hdr2);
    free(rcvr_policy);
    return srtp_err_status_ok;
}

srtp_err_status_t srtcp_test(const srtp_policy_t *policy, int mki_index)
{
    int i;
    srtp_t srtcp_sender;
    srtp_t srtcp_rcvr;
    srtp_err_status_t status = srtp_err_status_ok;
    srtp_hdr_t *hdr, *hdr2;
    uint8_t hdr_enc[64];
    uint8_t *pkt_end;
    int msg_len_octets, msg_len_enc, msg_len;
    int len, len2;
    uint32_t tag_length;
    uint32_t ssrc;
    srtp_policy_t *rcvr_policy;
    int use_mki = 0;

    if (mki_index >= 0)
        use_mki = 1;

    err_check(srtp_create(&srtcp_sender, policy));

    /* print out policy */
    err_check(srtp_session_print_policy(srtcp_sender));

    /*
     * initialize data buffer, using the ssrc in the policy unless that
     * value is a wildcard, in which case we'll just use an arbitrary
     * one
     */
    if (policy->ssrc.type != ssrc_specific) {
        ssrc = 0xdecafbad;
    } else {
        ssrc = policy->ssrc.value;
    }
    msg_len_octets = 28;
    hdr = srtp_create_test_packet(msg_len_octets, ssrc, &len);
    /* save message len */
    msg_len = len;

    if (hdr == NULL) {
        return srtp_err_status_alloc_fail;
    }
    hdr2 = srtp_create_test_packet(msg_len_octets, ssrc, &len2);
    if (hdr2 == NULL) {
        free(hdr);
        return srtp_err_status_alloc_fail;
    }

    debug_print(mod_driver, "before protection:\n%s",
                srtp_packet_to_string(hdr, len));

#if PRINT_REFERENCE_PACKET
    debug_print(mod_driver, "reference packet before protection:\n%s",
                octet_string_hex_string((uint8_t *)hdr, len));
#endif
    err_check(srtp_test_call_protect_rtcp(srtcp_sender, hdr, &len, mki_index));

    debug_print(mod_driver, "after protection:\n%s",
                srtp_packet_to_string(hdr, len));
#if PRINT_REFERENCE_PACKET
    debug_print(mod_driver, "after protection:\n%s",
                octet_string_hex_string((uint8_t *)hdr, len));
#endif

    /* save protected message and length */
    memcpy(hdr_enc, hdr, len);
    msg_len_enc = len;

    /*
     * check for overrun of the srtp_protect() function
     *
     * The packet is followed by a value of 0xfffff; if the value of the
     * data following the packet is different, then we know that the
     * protect function is overwriting the end of the packet.
     */
    srtp_get_protect_rtcp_trailer_length(srtcp_sender, use_mki, mki_index,
                                         &tag_length);
    pkt_end = (uint8_t *)hdr + msg_len + tag_length;
    for (i = 0; i < 4; i++) {
        if (pkt_end[i] != 0xff) {
            fprintf(stdout, "overwrite in srtp_protect_rtcp() function "
                            "(expected %x, found %x in trailing octet %d)\n",
                    0xff, ((uint8_t *)hdr)[i], i);
            free(hdr);
            free(hdr2);
            return srtp_err_status_algo_fail;
        }
    }

    /*
     * if the policy includes confidentiality, check that ciphertext is
     * different than plaintext
     *
     * Note that this check will give false negatives, with some small
     * probability, especially if the packets are short.  For that
     * reason, we skip this check if the plaintext is less than four
     * octets long.
     */
    if ((policy->rtcp.sec_serv & sec_serv_conf) && (msg_len_octets >= 4)) {
        printf("testing that ciphertext is distinct from plaintext...");
        status = srtp_err_status_algo_fail;
        for (i = 12; i < msg_len_octets + 12; i++) {
            if (((uint8_t *)hdr)[i] != ((uint8_t *)hdr2)[i]) {
                status = srtp_err_status_ok;
            }
        }
        if (status) {
            printf("failed\n");
            free(hdr);
            free(hdr2);
            return status;
        }
        printf("passed\n");
    }

    /*
     * if the policy uses a 'wildcard' ssrc, then we need to make a copy
     * of the policy that changes the direction to inbound
     *
     * we always copy the policy into the rcvr_policy, since otherwise
     * the compiler would fret about the constness of the policy
     */
    rcvr_policy = (srtp_policy_t *)malloc(sizeof(srtp_policy_t));
    if (rcvr_policy == NULL) {
        free(hdr);
        free(hdr2);
        return srtp_err_status_alloc_fail;
    }
    memcpy(rcvr_policy, policy, sizeof(srtp_policy_t));
    if (policy->ssrc.type == ssrc_any_outbound) {
        rcvr_policy->ssrc.type = ssrc_any_inbound;
    }

    err_check(srtp_create(&srtcp_rcvr, rcvr_policy));

    err_check(srtp_test_call_unprotect_rtcp(srtcp_rcvr, hdr, &len, use_mki));

    debug_print(mod_driver, "after unprotection:\n%s",
                srtp_packet_to_string(hdr, len));

    /* verify that the unprotected packet matches the origial one */
    for (i = 0; i < len; i++) {
        if (((uint8_t *)hdr)[i] != ((uint8_t *)hdr2)[i]) {
            fprintf(stdout, "mismatch at octet %d\n", i);
            status = srtp_err_status_algo_fail;
        }
    }
    if (status) {
        free(hdr);
        free(hdr2);
        free(rcvr_policy);
        return status;
    }

    /*
     * if the policy includes authentication, then test for false positives
     */
    if (policy->rtp.sec_serv & sec_serv_auth) {
        char *data = ((char *)hdr) + 12;

        printf("testing for false positives in replay check...");

        /* unprotect a second time - should fail with a replay error */
        status = srtp_test_call_unprotect_rtcp(srtcp_rcvr, hdr, &msg_len_enc,
                                               use_mki);
        if (status != srtp_err_status_replay_fail) {
            printf("failed with error code %d\n", status);
            free(hdr);
            free(hdr2);
            free(rcvr_policy);
            return status;
        } else {
            printf("passed\n");
        }

        printf("testing for false positives in auth check...");

        /* increment sequence number in header */
        hdr->seq++;

        /* apply protection */
        err_check(
            srtp_test_call_protect_rtcp(srtcp_sender, hdr, &len, mki_index));

        /* flip bits in packet */
        data[0] ^= 0xff;

        /* unprotect, and check for authentication failure */
        status = srtp_test_call_unprotect_rtcp(srtcp_rcvr, hdr, &len, use_mki);
        if (status != srtp_err_status_auth_fail) {
            printf("failed\n");
            free(hdr);
            free(hdr2);
            free(rcvr_policy);
            return status;
        } else {
            printf("passed\n");
        }
    }

    err_check(srtp_dealloc(srtcp_sender));
    err_check(srtp_dealloc(srtcp_rcvr));

    free(hdr);
    free(hdr2);
    free(rcvr_policy);
    return srtp_err_status_ok;
}

srtp_err_status_t srtp_session_print_policy(srtp_t srtp)
{
    char *serv_descr[4] = { "none", "confidentiality", "authentication",
                            "confidentiality and authentication" };
    char *direction[3] = { "unknown", "outbound", "inbound" };
    srtp_stream_t stream;
    srtp_session_keys_t *session_keys = NULL;

    /* sanity checking */
    if (srtp == NULL) {
        return srtp_err_status_fail;
    }

    /* if there's a template stream, print it out */
    if (srtp->stream_template != NULL) {
        stream = srtp->stream_template;
        session_keys = &stream->session_keys[0];
        printf("# SSRC:          any %s\r\n"
               "# rtp cipher:    %s\r\n"
               "# rtp auth:      %s\r\n"
               "# rtp services:  %s\r\n"
               "# rtcp cipher:   %s\r\n"
               "# rtcp auth:     %s\r\n"
               "# rtcp services: %s\r\n"
               "# window size:   %lu\r\n"
               "# tx rtx allowed:%s\r\n",
               direction[stream->direction],
               session_keys->rtp_cipher->type->description,
               session_keys->rtp_auth->type->description,
               serv_descr[stream->rtp_services],
               session_keys->rtcp_cipher->type->description,
               session_keys->rtcp_auth->type->description,
               serv_descr[stream->rtcp_services],
               srtp_rdbx_get_window_size(&stream->rtp_rdbx),
               stream->allow_repeat_tx ? "true" : "false");

        printf("# Encrypted extension headers: ");
        if (stream->enc_xtn_hdr && stream->enc_xtn_hdr_count > 0) {
            int *enc_xtn_hdr = stream->enc_xtn_hdr;
            int count = stream->enc_xtn_hdr_count;
            while (count > 0) {
                printf("%d ", *enc_xtn_hdr);
                enc_xtn_hdr++;
                count--;
            }
            printf("\n");
        } else {
            printf("none\n");
        }
    }

    /* loop over streams in session, printing the policy of each */
    stream = srtp->stream_list;
    while (stream != NULL) {
        if (stream->rtp_services > sec_serv_conf_and_auth) {
            return srtp_err_status_bad_param;
        }
        session_keys = &stream->session_keys[0];

        printf("# SSRC:          0x%08x\r\n"
               "# rtp cipher:    %s\r\n"
               "# rtp auth:      %s\r\n"
               "# rtp services:  %s\r\n"
               "# rtcp cipher:   %s\r\n"
               "# rtcp auth:     %s\r\n"
               "# rtcp services: %s\r\n"
               "# window size:   %lu\r\n"
               "# tx rtx allowed:%s\r\n",
               stream->ssrc, session_keys->rtp_cipher->type->description,
               session_keys->rtp_auth->type->description,
               serv_descr[stream->rtp_services],
               session_keys->rtcp_cipher->type->description,
               session_keys->rtcp_auth->type->description,
               serv_descr[stream->rtcp_services],
               srtp_rdbx_get_window_size(&stream->rtp_rdbx),
               stream->allow_repeat_tx ? "true" : "false");

        printf("# Encrypted extension headers: ");
        if (stream->enc_xtn_hdr && stream->enc_xtn_hdr_count > 0) {
            int *enc_xtn_hdr = stream->enc_xtn_hdr;
            int count = stream->enc_xtn_hdr_count;
            while (count > 0) {
                printf("%d ", *enc_xtn_hdr);
                enc_xtn_hdr++;
                count--;
            }
            printf("\n");
        } else {
            printf("none\n");
        }

        /* advance to next stream in the list */
        stream = stream->next;
    }
    return srtp_err_status_ok;
}

srtp_err_status_t srtp_print_policy(const srtp_policy_t *policy)
{
    srtp_err_status_t status;
    srtp_t session;

    status = srtp_create(&session, policy);
    if (status) {
        return status;
    }
    status = srtp_session_print_policy(session);
    if (status) {
        return status;
    }
    status = srtp_dealloc(session);
    if (status) {
        return status;
    }
    return srtp_err_status_ok;
}

/*
 * srtp_print_packet(...) is for debugging only
 * it prints an RTP packet to the stdout
 *
 * note that this function is *not* threadsafe
 */

#include <stdio.h>

#define MTU 2048

char packet_string[MTU];

char *srtp_packet_to_string(srtp_hdr_t *hdr, int pkt_octet_len)
{
    int octets_in_rtp_header = 12;
    uint8_t *data = ((uint8_t *)hdr) + octets_in_rtp_header;
    int hex_len = pkt_octet_len - octets_in_rtp_header;

    /* sanity checking */
    if ((hdr == NULL) || (pkt_octet_len > MTU)) {
        return NULL;
    }

    /* write packet into string */
    sprintf(packet_string, "(s)rtp packet: {\n"
                           "   version:\t%d\n"
                           "   p:\t\t%d\n"
                           "   x:\t\t%d\n"
                           "   cc:\t\t%d\n"
                           "   m:\t\t%d\n"
                           "   pt:\t\t%x\n"
                           "   seq:\t\t%x\n"
                           "   ts:\t\t%x\n"
                           "   ssrc:\t%x\n"
                           "   data:\t%s\n"
                           "} (%d octets in total)\n",
            hdr->version, hdr->p, hdr->x, hdr->cc, hdr->m, hdr->pt, hdr->seq,
            hdr->ts, hdr->ssrc, octet_string_hex_string(data, hex_len),
            pkt_octet_len);

    return packet_string;
}

/*
 * mips_estimate() is a simple function to estimate the number of
 * instructions per second that the host can perform.  note that this
 * function can be grossly wrong; you may want to have a manual sanity
 * check of its output!
 *
 * the 'ignore' pointer is there to convince the compiler to not just
 * optimize away the function
 */

double mips_estimate(int num_trials, int *ignore)
{
    clock_t t;
    volatile int i, sum;

    sum = 0;
    t = clock();
    for (i = 0; i < num_trials; i++) {
        sum += i;
    }
    t = clock() - t;
    if (t < 1) {
        t = 1;
    }

    /*   printf("%d\n", sum); */
    *ignore = sum;

    return (double)num_trials * CLOCKS_PER_SEC / t;
}

/*
 * srtp_validate() verifies the correctness of libsrtp by comparing
 * some computed packets against some pre-computed reference values.
 * These packets were made with the default SRTP policy.
 */

srtp_err_status_t srtp_validate()
{
    // clang-format off
    uint8_t srtp_plaintext_ref[28] = {
        0x80, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab
    };
    uint8_t srtp_plaintext[38] = {
        0x80, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    uint8_t srtp_ciphertext[38] = {
        0x80, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0x4e, 0x55, 0xdc, 0x4c,
        0xe7, 0x99, 0x78, 0xd8, 0x8c, 0xa4, 0xd2, 0x15,
        0x94, 0x9d, 0x24, 0x02, 0xb7, 0x8d, 0x6a, 0xcc,
        0x99, 0xea, 0x17, 0x9b, 0x8d, 0xbb
    };
    uint8_t rtcp_plaintext_ref[24] = {
        0x81, 0xc8, 0x00, 0x0b, 0xca, 0xfe, 0xba, 0xbe,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
    };
    uint8_t rtcp_plaintext[38] = {
        0x81, 0xc8, 0x00, 0x0b, 0xca, 0xfe, 0xba, 0xbe,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    uint8_t srtcp_ciphertext[38] = {
        0x81, 0xc8, 0x00, 0x0b, 0xca, 0xfe, 0xba, 0xbe,
        0x71, 0x28, 0x03, 0x5b, 0xe4, 0x87, 0xb9, 0xbd,
        0xbe, 0xf8, 0x90, 0x41, 0xf9, 0x77, 0xa5, 0xa8,
        0x80, 0x00, 0x00, 0x01, 0x99, 0x3e, 0x08, 0xcd,
        0x54, 0xd6, 0xc1, 0x23, 0x07, 0x98
    };
    // clang-format on

    srtp_t srtp_snd, srtp_recv;
    srtp_err_status_t status;
    int len;
    srtp_policy_t policy;

    /*
     * create a session with a single stream using the default srtp
     * policy and with the SSRC value 0xcafebabe
     */
    memset(&policy, 0, sizeof(policy));
    srtp_crypto_policy_set_rtp_default(&policy.rtp);
    srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
    policy.ssrc.type = ssrc_specific;
    policy.ssrc.value = 0xcafebabe;
    policy.key = test_key;
    policy.ekt = NULL;
    policy.window_size = 128;
    policy.allow_repeat_tx = 0;
    policy.next = NULL;

    status = srtp_create(&srtp_snd, &policy);
    if (status) {
        return status;
    }

    /*
     * protect plaintext, then compare with ciphertext
     */
    len = 28;
    status = srtp_protect(srtp_snd, srtp_plaintext, &len);
    if (status || (len != 38)) {
        return srtp_err_status_fail;
    }

    debug_print(mod_driver, "ciphertext:\n  %s",
                octet_string_hex_string(srtp_plaintext, len));
    debug_print(mod_driver, "ciphertext reference:\n  %s",
                octet_string_hex_string(srtp_ciphertext, len));

    if (srtp_octet_string_is_eq(srtp_plaintext, srtp_ciphertext, len)) {
        return srtp_err_status_fail;
    }

    /*
     * protect plaintext rtcp, then compare with srtcp ciphertext
     */
    len = 24;
    status = srtp_protect_rtcp(srtp_snd, rtcp_plaintext, &len);
    if (status || (len != 38)) {
        return srtp_err_status_fail;
    }

    debug_print(mod_driver, "srtcp ciphertext:\n  %s",
                octet_string_hex_string(rtcp_plaintext, len));
    debug_print(mod_driver, "srtcp ciphertext reference:\n  %s",
                octet_string_hex_string(srtcp_ciphertext, len));

    if (srtp_octet_string_is_eq(rtcp_plaintext, srtcp_ciphertext, len)) {
        return srtp_err_status_fail;
    }

    /*
     * create a receiver session context comparable to the one created
     * above - we need to do this so that the replay checking doesn't
     * complain
     */
    status = srtp_create(&srtp_recv, &policy);
    if (status) {
        return status;
    }

    /*
     * unprotect ciphertext, then compare with plaintext
     */
    status = srtp_unprotect(srtp_recv, srtp_ciphertext, &len);
    if (status || (len != 28)) {
        return status;
    }

    if (srtp_octet_string_is_eq(srtp_ciphertext, srtp_plaintext_ref, len)) {
        return srtp_err_status_fail;
    }

    /*
     * unprotect srtcp ciphertext, then compare with rtcp plaintext
     */
    len = 38;
    status = srtp_unprotect_rtcp(srtp_recv, srtcp_ciphertext, &len);
    if (status || (len != 24)) {
        return status;
    }

    if (srtp_octet_string_is_eq(srtcp_ciphertext, rtcp_plaintext_ref, len)) {
        return srtp_err_status_fail;
    }

    status = srtp_dealloc(srtp_snd);
    if (status) {
        return status;
    }

    status = srtp_dealloc(srtp_recv);
    if (status) {
        return status;
    }

    return srtp_err_status_ok;
}

#ifdef GCM
/*
 * srtp_validate_gcm() verifies the correctness of libsrtp by comparing
 * an computed packet against the known ciphertext for the plaintext.
 */
srtp_err_status_t srtp_validate_gcm()
{
    // clang-format off
    unsigned char test_key_gcm[28] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
        0xa8, 0xa9, 0xaa, 0xab
    };
    uint8_t rtp_plaintext_ref[28] = {
        0x80, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab
    };
    uint8_t rtp_plaintext[44] = {
        0x80, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    uint8_t srtp_ciphertext[44] = {
        0x80, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0xc5, 0x00, 0x2e, 0xde,
        0x04, 0xcf, 0xdd, 0x2e, 0xb9, 0x11, 0x59, 0xe0,
        0x88, 0x0a, 0xa0, 0x6e, 0xd2, 0x97, 0x68, 0x26,
        0xf7, 0x96, 0xb2, 0x01, 0xdf, 0x31, 0x31, 0xa1,
        0x27, 0xe8, 0xa3, 0x92
    };
    uint8_t rtcp_plaintext_ref[24] = {
        0x81, 0xc8, 0x00, 0x0b, 0xca, 0xfe, 0xba, 0xbe,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
    };
    uint8_t rtcp_plaintext[44] = {
        0x81, 0xc8, 0x00, 0x0b, 0xca, 0xfe, 0xba, 0xbe,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    uint8_t srtcp_ciphertext[44] = {
        0x81, 0xc8, 0x00, 0x0b, 0xca, 0xfe, 0xba, 0xbe,
        0xc9, 0x8b, 0x8b, 0x5d, 0xf0, 0x39, 0x2a, 0x55,
        0x85, 0x2b, 0x6c, 0x21, 0xac, 0x8e, 0x70, 0x25,
        0xc5, 0x2c, 0x6f, 0xbe, 0xa2, 0xb3, 0xb4, 0x46,
        0xea, 0x31, 0x12, 0x3b, 0xa8, 0x8c, 0xe6, 0x1e,
        0x80, 0x00, 0x00, 0x01
    };
    // clang-format on

    srtp_t srtp_snd, srtp_recv;
    srtp_err_status_t status;
    int len;
    srtp_policy_t policy;

    /*
     * create a session with a single stream using the default srtp
     * policy and with the SSRC value 0xcafebabe
     */
    memset(&policy, 0, sizeof(policy));
    srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy.rtp);
    srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy.rtcp);
    policy.ssrc.type = ssrc_specific;
    policy.ssrc.value = 0xcafebabe;
    policy.key = test_key_gcm;
    policy.ekt = NULL;
    policy.window_size = 128;
    policy.allow_repeat_tx = 0;
    policy.next = NULL;

    status = srtp_create(&srtp_snd, &policy);
    if (status) {
        return status;
    }

    /*
     * protect plaintext rtp, then compare with srtp ciphertext
     */
    len = 28;
    status = srtp_protect(srtp_snd, rtp_plaintext, &len);
    if (status || (len != 44)) {
        return srtp_err_status_fail;
    }

    debug_print(mod_driver, "srtp ciphertext:\n  %s",
                octet_string_hex_string(rtp_plaintext, len));
    debug_print(mod_driver, "srtp ciphertext reference:\n  %s",
                octet_string_hex_string(srtp_ciphertext, len));

    if (srtp_octet_string_is_eq(rtp_plaintext, srtp_ciphertext, len)) {
        return srtp_err_status_fail;
    }

    /*
     * protect plaintext rtcp, then compare with srtcp ciphertext
     */
    len = 24;
    status = srtp_protect_rtcp(srtp_snd, rtcp_plaintext, &len);
    if (status || (len != 44)) {
        return srtp_err_status_fail;
    }

    debug_print(mod_driver, "srtcp ciphertext:\n  %s",
                octet_string_hex_string(rtcp_plaintext, len));
    debug_print(mod_driver, "srtcp ciphertext reference:\n  %s",
                octet_string_hex_string(srtcp_ciphertext, len));

    if (srtp_octet_string_is_eq(rtcp_plaintext, srtcp_ciphertext, len)) {
        return srtp_err_status_fail;
    }

    /*
     * create a receiver session context comparable to the one created
     * above - we need to do this so that the replay checking doesn't
     * complain
     */
    status = srtp_create(&srtp_recv, &policy);
    if (status) {
        return status;
    }

    /*
     * unprotect srtp ciphertext, then compare with rtp plaintext
     */
    len = 44;
    status = srtp_unprotect(srtp_recv, srtp_ciphertext, &len);
    if (status || (len != 28)) {
        return status;
    }

    if (srtp_octet_string_is_eq(srtp_ciphertext, rtp_plaintext_ref, len)) {
        return srtp_err_status_fail;
    }

    /*
     * unprotect srtcp ciphertext, then compare with rtcp plaintext
     */
    len = 44;
    status = srtp_unprotect_rtcp(srtp_recv, srtcp_ciphertext, &len);
    if (status || (len != 24)) {
        return status;
    }

    if (srtp_octet_string_is_eq(srtcp_ciphertext, rtcp_plaintext_ref, len)) {
        return srtp_err_status_fail;
    }

    status = srtp_dealloc(srtp_snd);
    if (status) {
        return status;
    }

    status = srtp_dealloc(srtp_recv);
    if (status) {
        return status;
    }

    return srtp_err_status_ok;
}
#endif

/*
 * Test vectors taken from RFC 6904, Appendix A
 */
srtp_err_status_t srtp_validate_encrypted_extensions_headers()
{
    // clang-format off
    unsigned char test_key_ext_headers[30] = {
        0xe1, 0xf9, 0x7a, 0x0d, 0x3e, 0x01, 0x8b, 0xe0,
        0xd6, 0x4f, 0xa3, 0x2c, 0x06, 0xde, 0x41, 0x39,
        0x0e, 0xc6, 0x75, 0xad, 0x49, 0x8a, 0xfe, 0xeb,
        0xb6, 0x96, 0x0b, 0x3a, 0xab, 0xe6
    };
    uint8_t srtp_plaintext_ref[56] = {
        0x90, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0xBE, 0xDE, 0x00, 0x06,
        0x17, 0x41, 0x42, 0x73, 0xA4, 0x75, 0x26, 0x27,
        0x48, 0x22, 0x00, 0x00, 0xC8, 0x30, 0x8E, 0x46,
        0x55, 0x99, 0x63, 0x86, 0xB3, 0x95, 0xFB, 0x00,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab
    };
    uint8_t srtp_plaintext[66] = {
        0x90, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0xBE, 0xDE, 0x00, 0x06,
        0x17, 0x41, 0x42, 0x73, 0xA4, 0x75, 0x26, 0x27,
        0x48, 0x22, 0x00, 0x00, 0xC8, 0x30, 0x8E, 0x46,
        0x55, 0x99, 0x63, 0x86, 0xB3, 0x95, 0xFB, 0x00,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00
    };
    uint8_t srtp_ciphertext[66] = {
        0x90, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0xBE, 0xDE, 0x00, 0x06,
        0x17, 0x58, 0x8A, 0x92, 0x70, 0xF4, 0xE1, 0x5E,
        0x1C, 0x22, 0x00, 0x00, 0xC8, 0x30, 0x95, 0x46,
        0xA9, 0x94, 0xF0, 0xBC, 0x54, 0x78, 0x97, 0x00,
        0x4e, 0x55, 0xdc, 0x4c, 0xe7, 0x99, 0x78, 0xd8,
        0x8c, 0xa4, 0xd2, 0x15, 0x94, 0x9d, 0x24, 0x02,
        0x5a, 0x46, 0xb3, 0xca, 0x35, 0xc5, 0x35, 0xa8,
        0x91, 0xc7
    };
    // clang-format on

    srtp_t srtp_snd, srtp_recv;
    srtp_err_status_t status;
    int len;
    srtp_policy_t policy;
    int headers[3] = { 1, 3, 4 };

    /*
     * create a session with a single stream using the default srtp
     * policy and with the SSRC value 0xcafebabe
     */
    memset(&policy, 0, sizeof(policy));
    srtp_crypto_policy_set_rtp_default(&policy.rtp);
    srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
    policy.ssrc.type = ssrc_specific;
    policy.ssrc.value = 0xcafebabe;
    policy.key = test_key_ext_headers;
    policy.ekt = NULL;
    policy.window_size = 128;
    policy.allow_repeat_tx = 0;
    policy.enc_xtn_hdr = headers;
    policy.enc_xtn_hdr_count = sizeof(headers) / sizeof(headers[0]);
    policy.next = NULL;

    status = srtp_create(&srtp_snd, &policy);
    if (status)
        return status;

    /*
     * protect plaintext, then compare with ciphertext
     */
    len = sizeof(srtp_plaintext_ref);
    status = srtp_protect(srtp_snd, srtp_plaintext, &len);
    if (status || (len != sizeof(srtp_plaintext)))
        return srtp_err_status_fail;

    debug_print(mod_driver, "ciphertext:\n  %s",
                srtp_octet_string_hex_string(srtp_plaintext, len));
    debug_print(mod_driver, "ciphertext reference:\n  %s",
                srtp_octet_string_hex_string(srtp_ciphertext, len));

    if (srtp_octet_string_is_eq(srtp_plaintext, srtp_ciphertext, len))
        return srtp_err_status_fail;

    /*
     * create a receiver session context comparable to the one created
     * above - we need to do this so that the replay checking doesn't
     * complain
     */
    status = srtp_create(&srtp_recv, &policy);
    if (status)
        return status;

    /*
     * unprotect ciphertext, then compare with plaintext
     */
    status = srtp_unprotect(srtp_recv, srtp_ciphertext, &len);
    if (status) {
        return status;
    } else if (len != sizeof(srtp_plaintext_ref)) {
        return srtp_err_status_fail;
    }

    if (srtp_octet_string_is_eq(srtp_ciphertext, srtp_plaintext_ref, len))
        return srtp_err_status_fail;

    status = srtp_dealloc(srtp_snd);
    if (status)
        return status;

    status = srtp_dealloc(srtp_recv);
    if (status)
        return status;

    return srtp_err_status_ok;
}

#ifdef GCM

/*
 * Headers of test vectors taken from RFC 6904, Appendix A
 */
srtp_err_status_t srtp_validate_encrypted_extensions_headers_gcm()
{
    // clang-format off
    unsigned char test_key_ext_headers[30] = {
        0xe1, 0xf9, 0x7a, 0x0d, 0x3e, 0x01, 0x8b, 0xe0,
        0xd6, 0x4f, 0xa3, 0x2c, 0x06, 0xde, 0x41, 0x39,
        0x0e, 0xc6, 0x75, 0xad, 0x49, 0x8a, 0xfe, 0xeb,
        0xb6, 0x96, 0x0b, 0x3a, 0xab, 0xe6
    };
    uint8_t srtp_plaintext_ref[56] = {
        0x90, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0xBE, 0xDE, 0x00, 0x06,
        0x17, 0x41, 0x42, 0x73, 0xA4, 0x75, 0x26, 0x27,
        0x48, 0x22, 0x00, 0x00, 0xC8, 0x30, 0x8E, 0x46,
        0x55, 0x99, 0x63, 0x86, 0xB3, 0x95, 0xFB, 0x00,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab
    };
    uint8_t srtp_plaintext[64] = {
        0x90, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0xBE, 0xDE, 0x00, 0x06,
        0x17, 0x41, 0x42, 0x73, 0xA4, 0x75, 0x26, 0x27,
        0x48, 0x22, 0x00, 0x00, 0xC8, 0x30, 0x8E, 0x46,
        0x55, 0x99, 0x63, 0x86, 0xB3, 0x95, 0xFB, 0x00,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    uint8_t srtp_ciphertext[64] = {
        0x90, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0xBE, 0xDE, 0x00, 0x06,
        0x17, 0x12, 0xe0, 0x20, 0x5b, 0xfa, 0x94, 0x9b,
        0x1C, 0x22, 0x00, 0x00, 0xC8, 0x30, 0xbb, 0x46,
        0x73, 0x27, 0x78, 0xd9, 0x92, 0x9a, 0xab, 0x00,
        0x0e, 0xca, 0x0c, 0xf9, 0x5e, 0xe9, 0x55, 0xb2,
        0x6c, 0xd3, 0xd2, 0x88, 0xb4, 0x9f, 0x6c, 0xa9,
        0xf4, 0xb1, 0xb7, 0x59, 0x71, 0x9e, 0xb5, 0xbc
    };
    // clang-format on

    srtp_t srtp_snd, srtp_recv;
    srtp_err_status_t status;
    int len;
    srtp_policy_t policy;
    int headers[3] = { 1, 3, 4 };

    /*
     * create a session with a single stream using the default srtp
     * policy and with the SSRC value 0xcafebabe
     */
    memset(&policy, 0, sizeof(policy));
    srtp_crypto_policy_set_aes_gcm_128_8_auth(&policy.rtp);
    srtp_crypto_policy_set_aes_gcm_128_8_auth(&policy.rtcp);
    policy.ssrc.type = ssrc_specific;
    policy.ssrc.value = 0xcafebabe;
    policy.key = test_key_ext_headers;
    policy.ekt = NULL;
    policy.window_size = 128;
    policy.allow_repeat_tx = 0;
    policy.enc_xtn_hdr = headers;
    policy.enc_xtn_hdr_count = sizeof(headers) / sizeof(headers[0]);
    policy.next = NULL;

    status = srtp_create(&srtp_snd, &policy);
    if (status)
        return status;

    /*
     * protect plaintext, then compare with ciphertext
     */
    len = sizeof(srtp_plaintext_ref);
    status = srtp_protect(srtp_snd, srtp_plaintext, &len);
    if (status || (len != sizeof(srtp_plaintext)))
        return srtp_err_status_fail;

    debug_print(mod_driver, "ciphertext:\n  %s",
                srtp_octet_string_hex_string(srtp_plaintext, len));
    debug_print(mod_driver, "ciphertext reference:\n  %s",
                srtp_octet_string_hex_string(srtp_ciphertext, len));

    if (srtp_octet_string_is_eq(srtp_plaintext, srtp_ciphertext, len))
        return srtp_err_status_fail;

    /*
     * create a receiver session context comparable to the one created
     * above - we need to do this so that the replay checking doesn't
     * complain
     */
    status = srtp_create(&srtp_recv, &policy);
    if (status)
        return status;

    /*
     * unprotect ciphertext, then compare with plaintext
     */
    status = srtp_unprotect(srtp_recv, srtp_ciphertext, &len);
    if (status) {
        return status;
    } else if (len != sizeof(srtp_plaintext_ref)) {
        return srtp_err_status_fail;
    }

    if (srtp_octet_string_is_eq(srtp_ciphertext, srtp_plaintext_ref, len))
        return srtp_err_status_fail;

    status = srtp_dealloc(srtp_snd);
    if (status)
        return status;

    status = srtp_dealloc(srtp_recv);
    if (status)
        return status;

    return srtp_err_status_ok;
}
#endif

/*
 * srtp_validate_aes_256() verifies the correctness of libsrtp by comparing
 * some computed packets against some pre-computed reference values.
 * These packets were made with the AES-CM-256/HMAC-SHA-1-80 policy.
 */

srtp_err_status_t srtp_validate_aes_256()
{
    // clang-format off
    unsigned char aes_256_test_key[46] = {
        0xf0, 0xf0, 0x49, 0x14, 0xb5, 0x13, 0xf2, 0x76,
        0x3a, 0x1b, 0x1f, 0xa1, 0x30, 0xf1, 0x0e, 0x29,
        0x98, 0xf6, 0xf6, 0xe4, 0x3e, 0x43, 0x09, 0xd1,
        0xe6, 0x22, 0xa0, 0xe3, 0x32, 0xb9, 0xf1, 0xb6,

        0x3b, 0x04, 0x80, 0x3d, 0xe5, 0x1e, 0xe7, 0xc9,
        0x64, 0x23, 0xab, 0x5b, 0x78, 0xd2
    };
    uint8_t srtp_plaintext_ref[28] = {
        0x80, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab
    };
    uint8_t srtp_plaintext[38] = {
        0x80, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    uint8_t srtp_ciphertext[38] = {
        0x80, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad,
        0xca, 0xfe, 0xba, 0xbe, 0xf1, 0xd9, 0xde, 0x17,
        0xff, 0x25, 0x1f, 0xf1, 0xaa, 0x00, 0x77, 0x74,
        0xb0, 0xb4, 0xb4, 0x0d, 0xa0, 0x8d, 0x9d, 0x9a,
        0x5b, 0x3a, 0x55, 0xd8, 0x87, 0x3b
    };
    // clang-format on

    srtp_t srtp_snd, srtp_recv;
    srtp_err_status_t status;
    int len;
    srtp_policy_t policy;

    /*
     * create a session with a single stream using the default srtp
     * policy and with the SSRC value 0xcafebabe
     */
    memset(&policy, 0, sizeof(policy));
    srtp_crypto_policy_set_aes_cm_256_hmac_sha1_80(&policy.rtp);
    srtp_crypto_policy_set_aes_cm_256_hmac_sha1_80(&policy.rtcp);
    policy.ssrc.type = ssrc_specific;
    policy.ssrc.value = 0xcafebabe;
    policy.key = aes_256_test_key;
    policy.ekt = NULL;
    policy.window_size = 128;
    policy.allow_repeat_tx = 0;
    policy.next = NULL;

    status = srtp_create(&srtp_snd, &policy);
    if (status) {
        return status;
    }

    /*
     * protect plaintext, then compare with ciphertext
     */
    len = 28;
    status = srtp_protect(srtp_snd, srtp_plaintext, &len);
    if (status || (len != 38)) {
        return srtp_err_status_fail;
    }

    debug_print(mod_driver, "ciphertext:\n  %s",
                octet_string_hex_string(srtp_plaintext, len));
    debug_print(mod_driver, "ciphertext reference:\n  %s",
                octet_string_hex_string(srtp_ciphertext, len));

    if (srtp_octet_string_is_eq(srtp_plaintext, srtp_ciphertext, len)) {
        return srtp_err_status_fail;
    }

    /*
     * create a receiver session context comparable to the one created
     * above - we need to do this so that the replay checking doesn't
     * complain
     */
    status = srtp_create(&srtp_recv, &policy);
    if (status) {
        return status;
    }

    /*
     * unprotect ciphertext, then compare with plaintext
     */
    status = srtp_unprotect(srtp_recv, srtp_ciphertext, &len);
    if (status || (len != 28)) {
        return status;
    }

    if (srtp_octet_string_is_eq(srtp_ciphertext, srtp_plaintext_ref, len)) {
        return srtp_err_status_fail;
    }

    status = srtp_dealloc(srtp_snd);
    if (status) {
        return status;
    }

    status = srtp_dealloc(srtp_recv);
    if (status) {
        return status;
    }

    return srtp_err_status_ok;
}

srtp_err_status_t srtp_create_big_policy(srtp_policy_t **list)
{
    extern const srtp_policy_t *policy_array[];
    srtp_policy_t *p, *tmp;
    int i = 0;
    uint32_t ssrc = 0;

    /* sanity checking */
    if ((list == NULL) || (policy_array[0] == NULL)) {
        return srtp_err_status_bad_param;
    }

    /*
     * loop over policy list, mallocing a new list and copying values
     * into it (and incrementing the SSRC value as we go along)
     */
    tmp = NULL;
    while (policy_array[i] != NULL) {
        p = (srtp_policy_t *)malloc(sizeof(srtp_policy_t));
        if (p == NULL) {
            return srtp_err_status_bad_param;
        }
        memcpy(p, policy_array[i], sizeof(srtp_policy_t));
        p->ssrc.type = ssrc_specific;
        p->ssrc.value = ssrc++;
        p->next = tmp;
        tmp = p;
        i++;
    }
    *list = p;

    return srtp_err_status_ok;
}

srtp_err_status_t srtp_dealloc_big_policy(srtp_policy_t *list)
{
    srtp_policy_t *p, *next;

    for (p = list; p != NULL; p = next) {
        next = p->next;
        free(p);
    }

    return srtp_err_status_ok;
}

srtp_err_status_t srtp_test_empty_payload()
{
    srtp_t srtp_snd, srtp_recv;
    srtp_err_status_t status;
    int len;
    srtp_policy_t policy;
    srtp_hdr_t *mesg;

    /*
     * create a session with a single stream using the default srtp
     * policy and with the SSRC value 0xcafebabe
     */
    memset(&policy, 0, sizeof(policy));
    srtp_crypto_policy_set_rtp_default(&policy.rtp);
    srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
    policy.ssrc.type = ssrc_specific;
    policy.ssrc.value = 0xcafebabe;
    policy.key = test_key;
    policy.ekt = NULL;
    policy.window_size = 128;
    policy.allow_repeat_tx = 0;
    policy.next = NULL;

    status = srtp_create(&srtp_snd, &policy);
    if (status) {
        return status;
    }

    mesg = srtp_create_test_packet(0, policy.ssrc.value, &len);
    if (mesg == NULL) {
        return srtp_err_status_fail;
    }

    status = srtp_protect(srtp_snd, mesg, &len);
    if (status) {
        return status;
    } else if (len != 12 + 10) {
        return srtp_err_status_fail;
    }

    /*
     * create a receiver session context comparable to the one created
     * above - we need to do this so that the replay checking doesn't
     * complain
     */
    status = srtp_create(&srtp_recv, &policy);
    if (status) {
        return status;
    }

    /*
     * unprotect ciphertext, then compare with plaintext
     */
    status = srtp_unprotect(srtp_recv, mesg, &len);
    if (status) {
        return status;
    } else if (len != 12) {
        return srtp_err_status_fail;
    }

    status = srtp_dealloc(srtp_snd);
    if (status) {
        return status;
    }

    status = srtp_dealloc(srtp_recv);
    if (status) {
        return status;
    }

    free(mesg);

    return srtp_err_status_ok;
}

#ifdef GCM
srtp_err_status_t srtp_test_empty_payload_gcm()
{
    srtp_t srtp_snd, srtp_recv;
    srtp_err_status_t status;
    int len;
    srtp_policy_t policy;
    srtp_hdr_t *mesg;

    /*
     * create a session with a single stream using the default srtp
     * policy and with the SSRC value 0xcafebabe
     */
    memset(&policy, 0, sizeof(policy));
    srtp_crypto_policy_set_aes_gcm_128_8_auth(&policy.rtp);
    srtp_crypto_policy_set_aes_gcm_128_8_auth(&policy.rtcp);
    policy.ssrc.type = ssrc_specific;
    policy.ssrc.value = 0xcafebabe;
    policy.key = test_key;
    policy.ekt = NULL;
    policy.window_size = 128;
    policy.allow_repeat_tx = 0;
    policy.next = NULL;

    status = srtp_create(&srtp_snd, &policy);
    if (status) {
        return status;
    }

    mesg = srtp_create_test_packet(0, policy.ssrc.value, &len);
    if (mesg == NULL) {
        return srtp_err_status_fail;
    }

    status = srtp_protect(srtp_snd, mesg, &len);
    if (status) {
        return status;
    } else if (len != 12 + 8) {
        return srtp_err_status_fail;
    }

    /*
     * create a receiver session context comparable to the one created
     * above - we need to do this so that the replay checking doesn't
     * complain
     */
    status = srtp_create(&srtp_recv, &policy);
    if (status) {
        return status;
    }

    /*
     * unprotect ciphertext, then compare with plaintext
     */
    status = srtp_unprotect(srtp_recv, mesg, &len);
    if (status) {
        return status;
    } else if (len != 12) {
        return srtp_err_status_fail;
    }

    status = srtp_dealloc(srtp_snd);
    if (status) {
        return status;
    }

    status = srtp_dealloc(srtp_recv);
    if (status) {
        return status;
    }

    free(mesg);

    return srtp_err_status_ok;
}
#endif // GCM

srtp_err_status_t srtp_test_remove_stream()
{
    srtp_err_status_t status;
    srtp_policy_t *policy_list, policy;
    srtp_t session;
    srtp_stream_t stream;

    /*
     * srtp_get_stream() is a libSRTP internal function that we declare
     * here so that we can use it to verify the correct operation of the
     * library
     */
    extern srtp_stream_t srtp_get_stream(srtp_t srtp, uint32_t ssrc);

    status = srtp_create_big_policy(&policy_list);
    if (status) {
        return status;
    }

    status = srtp_create(&session, policy_list);
    if (status) {
        return status;
    }

    /*
     * check for false positives by trying to remove a stream that's not
     * in the session
     */
    status = srtp_remove_stream(session, htonl(0xaaaaaaaa));
    if (status != srtp_err_status_no_ctx) {
        return srtp_err_status_fail;
    }

    /*
     * check for false negatives by removing stream 0x1, then
     * searching for streams 0x0 and 0x2
     */
    status = srtp_remove_stream(session, htonl(0x1));
    if (status != srtp_err_status_ok) {
        return srtp_err_status_fail;
    }
    stream = srtp_get_stream(session, htonl(0x0));
    if (stream == NULL) {
        return srtp_err_status_fail;
    }
    stream = srtp_get_stream(session, htonl(0x2));
    if (stream == NULL) {
        return srtp_err_status_fail;
    }

    status = srtp_dealloc(session);
    if (status != srtp_err_status_ok) {
        return status;
    }

    status = srtp_dealloc_big_policy(policy_list);
    if (status != srtp_err_status_ok) {
        return status;
    }

    /* Now test adding and removing a single stream */
    memset(&policy, 0, sizeof(policy));
    srtp_crypto_policy_set_rtp_default(&policy.rtp);
    srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
    policy.ssrc.type = ssrc_specific;
    policy.ssrc.value = 0xcafebabe;
    policy.key = test_key;
    policy.ekt = NULL;
    policy.window_size = 128;
    policy.allow_repeat_tx = 0;
    policy.next = NULL;

    status = srtp_create(&session, NULL);
    if (status != srtp_err_status_ok) {
        return status;
    }

    status = srtp_add_stream(session, &policy);
    if (status != srtp_err_status_ok) {
        return status;
    }

    status = srtp_remove_stream(session, htonl(0xcafebabe));
    if (status != srtp_err_status_ok) {
        return status;
    }

    status = srtp_dealloc(session);
    if (status != srtp_err_status_ok) {
        return status;
    }

    return srtp_err_status_ok;
}

// clang-format off
unsigned char test_alt_key[46] = {
  0xe5, 0x19, 0x6f, 0x01, 0x5e, 0xf1, 0x9b, 0xe1,
  0xd7, 0x47, 0xa7, 0x27, 0x07, 0xd7, 0x47, 0x33,
  0x01, 0xc2, 0x35, 0x4d, 0x59, 0x6a, 0xf7, 0x84,
  0x96, 0x98, 0xeb, 0xaa, 0xac, 0xf6, 0xa1, 0x45,
  0xc7, 0x15, 0xe2, 0xea, 0xfe, 0x55, 0x67, 0x96,
  0xb6, 0x96, 0x0b, 0x3a, 0xab, 0xe6
};
// clang-format on

/*
 * srtp_test_update() verifies updating/rekeying exsisting streams.
 * As stated in https://tools.ietf.org/html/rfc3711#section-3.3.1
 * the value of the ROC must not be reset after a rekey, this test
 * atempts to prove that srtp_update does not reset the ROC.
 */

srtp_err_status_t srtp_test_update()
{
    srtp_err_status_t status;
    uint32_t ssrc = 0x12121212;
    int msg_len_octets = 32;
    int protected_msg_len_octets;
    srtp_hdr_t *msg;
    srtp_t srtp_snd, srtp_recv;
    srtp_policy_t policy;

    memset(&policy, 0, sizeof(policy));
    srtp_crypto_policy_set_rtp_default(&policy.rtp);
    srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
    policy.ekt = NULL;
    policy.window_size = 128;
    policy.allow_repeat_tx = 0;
    policy.next = NULL;
    policy.ssrc.type = ssrc_any_outbound;
    policy.key = test_key;

    /* create a send and recive ctx with defualt profile and test_key */
    status = srtp_create(&srtp_recv, &policy);
    if (status)
        return status;

    policy.ssrc.type = ssrc_any_inbound;
    status = srtp_create(&srtp_snd, &policy);
    if (status)
        return status;

    /* protect and unprotect two msg's that will cause the ROC to be equal to 1
     */
    msg = srtp_create_test_packet(msg_len_octets, ssrc,
                                  &protected_msg_len_octets);
    if (msg == NULL)
        return srtp_err_status_alloc_fail;
    msg->seq = htons(65535);

    status = srtp_protect(srtp_snd, msg, &protected_msg_len_octets);
    if (status)
        return srtp_err_status_fail;

    status = srtp_unprotect(srtp_recv, msg, &protected_msg_len_octets);
    if (status)
        return status;

    free(msg);

    msg = srtp_create_test_packet(msg_len_octets, ssrc,
                                  &protected_msg_len_octets);
    if (msg == NULL)
        return srtp_err_status_alloc_fail;
    msg->seq = htons(1);

    status = srtp_protect(srtp_snd, msg, &protected_msg_len_octets);
    if (status)
        return srtp_err_status_fail;

    status = srtp_unprotect(srtp_recv, msg, &protected_msg_len_octets);
    if (status)
        return status;

    free(msg);

    /* update send ctx with same test_key t verify update works*/
    policy.ssrc.type = ssrc_any_outbound;
    policy.key = test_key;
    status = srtp_update(srtp_snd, &policy);
    if (status)
        return status;

    msg = srtp_create_test_packet(msg_len_octets, ssrc,
                                  &protected_msg_len_octets);
    if (msg == NULL)
        return srtp_err_status_alloc_fail;
    msg->seq = htons(2);

    status = srtp_protect(srtp_snd, msg, &protected_msg_len_octets);
    if (status)
        return srtp_err_status_fail;

    status = srtp_unprotect(srtp_recv, msg, &protected_msg_len_octets);
    if (status)
        return status;

    free(msg);

    /* update send ctx to use test_alt_key */
    policy.ssrc.type = ssrc_any_outbound;
    policy.key = test_alt_key;
    status = srtp_update(srtp_snd, &policy);
    if (status)
        return status;

    /* create and protect msg with new key and ROC still equal to 1 */
    msg = srtp_create_test_packet(msg_len_octets, ssrc,
                                  &protected_msg_len_octets);
    if (msg == NULL)
        return srtp_err_status_alloc_fail;
    msg->seq = htons(3);

    status = srtp_protect(srtp_snd, msg, &protected_msg_len_octets);
    if (status)
        return srtp_err_status_fail;

    /* verify that recive ctx will fail to unprotect as it still uses test_key
     */
    status = srtp_unprotect(srtp_recv, msg, &protected_msg_len_octets);
    if (status == srtp_err_status_ok)
        return srtp_err_status_fail;

    /* create a new recvieve ctx with test_alt_key but since it is new it will
     * have ROC equal to 1
     * and therefore should fail to unprotected */
    {
        srtp_t srtp_recv_roc_0;

        policy.ssrc.type = ssrc_any_inbound;
        policy.key = test_alt_key;
        status = srtp_create(&srtp_recv_roc_0, &policy);
        if (status)
            return status;

        status =
            srtp_unprotect(srtp_recv_roc_0, msg, &protected_msg_len_octets);
        if (status == srtp_err_status_ok)
            return srtp_err_status_fail;

        status = srtp_dealloc(srtp_recv_roc_0);
        if (status)
            return status;
    }

    /* update recive ctx to use test_alt_key */
    policy.ssrc.type = ssrc_any_inbound;
    policy.key = test_alt_key;
    status = srtp_update(srtp_recv, &policy);
    if (status)
        return status;

    /* verify that can still unprotect, therfore key is updated and ROC value is
     * preserved */
    status = srtp_unprotect(srtp_recv, msg, &protected_msg_len_octets);
    if (status)
        return status;

    free(msg);

    status = srtp_dealloc(srtp_snd);
    if (status)
        return status;

    status = srtp_dealloc(srtp_recv);
    if (status)
        return status;

    return srtp_err_status_ok;
}

srtp_err_status_t srtp_test_setup_protect_trailer_streams(
    srtp_t *srtp_send,
    srtp_t *srtp_send_mki,
    srtp_t *srtp_send_aes_gcm,
    srtp_t *srtp_send_aes_gcm_mki)
{
    srtp_err_status_t status;
    srtp_policy_t policy;
    srtp_policy_t policy_mki;

#ifdef GCM
    srtp_policy_t policy_aes_gcm;
    srtp_policy_t policy_aes_gcm_mki;
#endif // GCM

    memset(&policy, 0, sizeof(policy));
    srtp_crypto_policy_set_rtp_default(&policy.rtp);
    srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
    policy.ekt = NULL;
    policy.window_size = 128;
    policy.allow_repeat_tx = 0;
    policy.next = NULL;
    policy.ssrc.type = ssrc_any_outbound;
    policy.key = test_key;

    memset(&policy_mki, 0, sizeof(policy_mki));
    srtp_crypto_policy_set_rtp_default(&policy_mki.rtp);
    srtp_crypto_policy_set_rtcp_default(&policy_mki.rtcp);
    policy_mki.ekt = NULL;
    policy_mki.window_size = 128;
    policy_mki.allow_repeat_tx = 0;
    policy_mki.next = NULL;
    policy_mki.ssrc.type = ssrc_any_outbound;
    policy_mki.key = NULL;
    policy_mki.keys = test_keys;
    policy_mki.num_master_keys = 2;

#ifdef GCM
    memset(&policy_aes_gcm, 0, sizeof(policy_aes_gcm));
    srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy_aes_gcm.rtp);
    srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy_aes_gcm.rtcp);
    policy_aes_gcm.ekt = NULL;
    policy_aes_gcm.window_size = 128;
    policy_aes_gcm.allow_repeat_tx = 0;
    policy_aes_gcm.next = NULL;
    policy_aes_gcm.ssrc.type = ssrc_any_outbound;
    policy_aes_gcm.key = test_key;

    memset(&policy_aes_gcm_mki, 0, sizeof(policy_aes_gcm_mki));
    srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy_aes_gcm_mki.rtp);
    srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy_aes_gcm_mki.rtcp);
    policy_aes_gcm_mki.ekt = NULL;
    policy_aes_gcm_mki.window_size = 128;
    policy_aes_gcm_mki.allow_repeat_tx = 0;
    policy_aes_gcm_mki.next = NULL;
    policy_aes_gcm_mki.ssrc.type = ssrc_any_outbound;
    policy_aes_gcm_mki.key = NULL;
    policy_aes_gcm_mki.keys = test_keys;
    policy_aes_gcm_mki.num_master_keys = 2;
#endif // GCM

    /* create a send ctx with defualt profile and test_key */
    status = srtp_create(srtp_send, &policy);
    if (status)
        return status;

    status = srtp_create(srtp_send_mki, &policy_mki);
    if (status)
        return status;

#ifdef GCM
    status = srtp_create(srtp_send_aes_gcm, &policy_aes_gcm);
    if (status)
        return status;

    status = srtp_create(srtp_send_aes_gcm_mki, &policy_aes_gcm_mki);
    if (status)
        return status;
#endif // GCM

    return srtp_err_status_ok;
}

srtp_err_status_t srtp_test_protect_trailer_length()
{
    srtp_t srtp_send;
    srtp_t srtp_send_mki;
    srtp_t srtp_send_aes_gcm;
    srtp_t srtp_send_aes_gcm_mki;
    uint32_t length = 0;
    srtp_err_status_t status;

    srtp_test_setup_protect_trailer_streams(
        &srtp_send, &srtp_send_mki, &srtp_send_aes_gcm, &srtp_send_aes_gcm_mki);

    status = srtp_get_protect_trailer_length(srtp_send, 0, 0, &length);
    if (status)
        return status;

    /*  TAG Length: 10 bytes */
    if (length != 10)
        return srtp_err_status_fail;

    status = srtp_get_protect_trailer_length(srtp_send_mki, 1, 1, &length);
    if (status)
        return status;

    /*  TAG Length: 10 bytes + MKI length: 4 bytes*/
    if (length != 14)
        return srtp_err_status_fail;

#ifdef GCM
    status = srtp_get_protect_trailer_length(srtp_send_aes_gcm, 0, 0, &length);
    if (status)
        return status;

    /*  TAG Length: 16 bytes */
    if (length != 16)
        return srtp_err_status_fail;

    status =
        srtp_get_protect_trailer_length(srtp_send_aes_gcm_mki, 1, 1, &length);
    if (status)
        return status;

    /*  TAG Length: 16 bytes + MKI length: 4 bytes*/
    if (length != 20)
        return srtp_err_status_fail;
#endif // GCM

    srtp_dealloc(srtp_send);
    srtp_dealloc(srtp_send_mki);
#ifdef GCM
    srtp_dealloc(srtp_send_aes_gcm);
    srtp_dealloc(srtp_send_aes_gcm_mki);
#endif

    return srtp_err_status_ok;
}

srtp_err_status_t srtp_test_protect_rtcp_trailer_length()
{
    srtp_t srtp_send;
    srtp_t srtp_send_mki;
    srtp_t srtp_send_aes_gcm;
    srtp_t srtp_send_aes_gcm_mki;
    uint32_t length = 0;
    srtp_err_status_t status;

    srtp_test_setup_protect_trailer_streams(
        &srtp_send, &srtp_send_mki, &srtp_send_aes_gcm, &srtp_send_aes_gcm_mki);

    status = srtp_get_protect_rtcp_trailer_length(srtp_send, 0, 0, &length);
    if (status)
        return status;

    /*  TAG Length: 10 bytes + SRTCP Trailer 4 bytes*/
    if (length != 14)
        return srtp_err_status_fail;

    status = srtp_get_protect_rtcp_trailer_length(srtp_send_mki, 1, 1, &length);
    if (status)
        return status;

    /*  TAG Length: 10 bytes + SRTCP Trailer 4 bytes + MKI 4 bytes*/
    if (length != 18)
        return srtp_err_status_fail;

#ifdef GCM
    status =
        srtp_get_protect_rtcp_trailer_length(srtp_send_aes_gcm, 0, 0, &length);
    if (status)
        return status;

    /*  TAG Length: 16 bytes + SRTCP Trailer 4 bytes*/
    if (length != 20)
        return srtp_err_status_fail;

    status = srtp_get_protect_rtcp_trailer_length(srtp_send_aes_gcm_mki, 1, 1,
                                                  &length);
    if (status)
        return status;

    /*  TAG Length: 16 bytes + SRTCP Trailer 4 bytes + MKI 4 bytes*/
    if (length != 24)
        return srtp_err_status_fail;
#endif // GCM

    srtp_dealloc(srtp_send);
    srtp_dealloc(srtp_send_mki);
#ifdef GCM
    srtp_dealloc(srtp_send_aes_gcm);
    srtp_dealloc(srtp_send_aes_gcm_mki);
#endif

    return srtp_err_status_ok;
}

srtp_err_status_t srtp_test_get_roc()
{
    srtp_err_status_t status;
    srtp_policy_t policy;
    srtp_t session;
    srtp_hdr_t *pkt;
    uint32_t i;
    uint32_t roc;
    uint32_t ts;
    uint16_t seq;

    int msg_len_octets = 32;
    int protected_msg_len_octets;

    memset(&policy, 0, sizeof(policy));
    srtp_crypto_policy_set_rtp_default(&policy.rtp);
    srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
    policy.ssrc.type = ssrc_specific;
    policy.ssrc.value = 0xcafebabe;
    policy.key = test_key;
    policy.window_size = 128;

    /* Create a sender session */
    status = srtp_create(&session, &policy);
    if (status) {
        return status;
    }

    /* Set start sequence so we roll over */
    seq = 65535;
    ts = 0;

    for (i = 0; i < 2; i++) {
        pkt = srtp_create_test_packet_extended(msg_len_octets,
                                               policy.ssrc.value, seq, ts,
                                               &protected_msg_len_octets);
        status = srtp_protect(session, pkt, &protected_msg_len_octets);
        free(pkt);
        if (status) {
            return status;
        }

        status = srtp_get_stream_roc(session, policy.ssrc.value, &roc);
        if (status) {
            return status;
        }

        if (roc != i) {
            return srtp_err_status_fail;
        }

        seq++;
        ts++;
    }

    /* Cleanup */
    status = srtp_dealloc(session);
    if (status) {
        return status;
    }

    return srtp_err_status_ok;
}

static srtp_err_status_t test_set_receiver_roc(uint32_t packets,
                                               uint32_t roc_to_set)
{
    srtp_err_status_t status;

    srtp_policy_t sender_policy;
    srtp_t sender_session;

    srtp_policy_t receiver_policy;
    srtp_t receiver_session;

    srtp_hdr_t *pkt_1;
    unsigned char *recv_pkt_1;

    srtp_hdr_t *pkt_2;
    unsigned char *recv_pkt_2;

    uint32_t i;
    uint32_t ts;
    uint16_t seq;

    int msg_len_octets = 32;
    int protected_msg_len_octets_1;
    int protected_msg_len_octets_2;

    /* Create sender */
    memset(&sender_policy, 0, sizeof(sender_policy));
    srtp_crypto_policy_set_rtp_default(&sender_policy.rtp);
    srtp_crypto_policy_set_rtcp_default(&sender_policy.rtcp);
    sender_policy.ssrc.type = ssrc_specific;
    sender_policy.ssrc.value = 0xcafebabe;
    sender_policy.key = test_key;
    sender_policy.window_size = 128;

    status = srtp_create(&sender_session, &sender_policy);
    if (status) {
        return status;
    }

    /* Create and protect packets */
    seq = 0;
    ts = 0;
    for (i = 0; i < packets; i++) {
        srtp_hdr_t *tmp_pkt;
        int tmp_len;

        tmp_pkt = srtp_create_test_packet_extended(
            msg_len_octets, sender_policy.ssrc.value, seq, ts, &tmp_len);
        status = srtp_protect(sender_session, tmp_pkt, &tmp_len);
        free(tmp_pkt);
        if (status) {
            return status;
        }

        seq++;
        ts++;
    }

    /* Create the first packet to decrypt and test for ROC change */
    pkt_1 = srtp_create_test_packet_extended(msg_len_octets,
                                             sender_policy.ssrc.value, seq, ts,
                                             &protected_msg_len_octets_1);
    status = srtp_protect(sender_session, pkt_1, &protected_msg_len_octets_1);
    if (status) {
        return status;
    }

    /* Create the second packet to decrypt and test for ROC change */
    seq++;
    ts++;
    pkt_2 = srtp_create_test_packet_extended(msg_len_octets,
                                             sender_policy.ssrc.value, seq, ts,
                                             &protected_msg_len_octets_2);
    status = srtp_protect(sender_session, pkt_2, &protected_msg_len_octets_2);
    if (status) {
        return status;
    }

    /* Create the receiver */
    memset(&receiver_policy, 0, sizeof(receiver_policy));
    srtp_crypto_policy_set_rtp_default(&receiver_policy.rtp);
    srtp_crypto_policy_set_rtcp_default(&receiver_policy.rtcp);
    receiver_policy.ssrc.type = ssrc_specific;
    receiver_policy.ssrc.value = sender_policy.ssrc.value;
    receiver_policy.key = test_key;
    receiver_policy.window_size = 128;

    status = srtp_create(&receiver_session, &receiver_policy);
    if (status) {
        return status;
    }

    /* Make a copy of the first sent protected packet */
    recv_pkt_1 = malloc(protected_msg_len_octets_1);
    if (recv_pkt_1 == NULL) {
        return srtp_err_status_fail;
    }
    memcpy(recv_pkt_1, pkt_1, protected_msg_len_octets_1);

    /* Make a copy of the second sent protected packet */
    recv_pkt_2 = malloc(protected_msg_len_octets_2);
    if (recv_pkt_2 == NULL) {
        return srtp_err_status_fail;
    }
    memcpy(recv_pkt_2, pkt_2, protected_msg_len_octets_2);

    /* Set the ROC to the wanted value */
    status = srtp_set_stream_roc(receiver_session, receiver_policy.ssrc.value,
                                 roc_to_set);
    if (status) {
        return status;
    }

    /* Unprotect the first packet */
    status = srtp_unprotect(receiver_session, recv_pkt_1,
                            &protected_msg_len_octets_1);
    if (status) {
        return status;
    }

    /* Unprotect the second packet */
    status = srtp_unprotect(receiver_session, recv_pkt_2,
                            &protected_msg_len_octets_2);
    if (status) {
        return status;
    }

    /* Cleanup */
    status = srtp_dealloc(sender_session);
    if (status) {
        return status;
    }

    status = srtp_dealloc(receiver_session);
    if (status) {
        return status;
    }

    free(pkt_1);
    free(recv_pkt_1);
    free(pkt_2);
    free(recv_pkt_2);

    return srtp_err_status_ok;
}

static srtp_err_status_t test_set_sender_roc(uint16_t seq, uint32_t roc_to_set)
{
    srtp_err_status_t status;

    srtp_policy_t sender_policy;
    srtp_t sender_session;

    srtp_policy_t receiver_policy;
    srtp_t receiver_session;

    srtp_hdr_t *pkt;
    unsigned char *recv_pkt;

    uint32_t ts;

    int msg_len_octets = 32;
    int protected_msg_len_octets;

    /* Create sender */
    memset(&sender_policy, 0, sizeof(sender_policy));
    srtp_crypto_policy_set_rtp_default(&sender_policy.rtp);
    srtp_crypto_policy_set_rtcp_default(&sender_policy.rtcp);
    sender_policy.ssrc.type = ssrc_specific;
    sender_policy.ssrc.value = 0xcafebabe;
    sender_policy.key = test_key;
    sender_policy.window_size = 128;

    status = srtp_create(&sender_session, &sender_policy);
    if (status) {
        return status;
    }

    /* Set the ROC before encrypting the first packet */
    status = srtp_set_stream_roc(sender_session, sender_policy.ssrc.value,
                                 roc_to_set);
    if (status != srtp_err_status_ok) {
        return status;
    }

    /* Create the packet to decrypt */
    ts = 0;
    pkt = srtp_create_test_packet_extended(msg_len_octets,
                                           sender_policy.ssrc.value, seq, ts,
                                           &protected_msg_len_octets);
    status = srtp_protect(sender_session, pkt, &protected_msg_len_octets);
    if (status) {
        return status;
    }

    /* Create the receiver */
    memset(&receiver_policy, 0, sizeof(receiver_policy));
    srtp_crypto_policy_set_rtp_default(&receiver_policy.rtp);
    srtp_crypto_policy_set_rtcp_default(&receiver_policy.rtcp);
    receiver_policy.ssrc.type = ssrc_specific;
    receiver_policy.ssrc.value = sender_policy.ssrc.value;
    receiver_policy.key = test_key;
    receiver_policy.window_size = 128;

    status = srtp_create(&receiver_session, &receiver_policy);
    if (status) {
        return status;
    }

    /* Make a copy of the sent protected packet */
    recv_pkt = malloc(protected_msg_len_octets);
    if (recv_pkt == NULL) {
        return srtp_err_status_fail;
    }
    memcpy(recv_pkt, pkt, protected_msg_len_octets);

    /* Set the ROC to the wanted value */
    status = srtp_set_stream_roc(receiver_session, receiver_policy.ssrc.value,
                                 roc_to_set);
    if (status) {
        return status;
    }

    status =
        srtp_unprotect(receiver_session, recv_pkt, &protected_msg_len_octets);
    if (status) {
        return status;
    }

    /* Cleanup */
    status = srtp_dealloc(sender_session);
    if (status) {
        return status;
    }

    status = srtp_dealloc(receiver_session);
    if (status) {
        return status;
    }

    free(pkt);
    free(recv_pkt);

    return srtp_err_status_ok;
}

srtp_err_status_t srtp_test_set_receiver_roc()
{
    int packets;
    uint32_t roc;
    srtp_err_status_t status;

    /* First test does not rollover */
    packets = 1;
    roc = 0;

    status = test_set_receiver_roc(packets - 1, roc);
    if (status) {
        return status;
    }

    status = test_set_receiver_roc(packets, roc);
    if (status) {
        return status;
    }

    status = test_set_receiver_roc(packets + 1, roc);
    if (status) {
        return status;
    }

    status = test_set_receiver_roc(packets + 60000, roc);
    if (status) {
        return status;
    }

    /* Second test should rollover */
    packets = 65535;
    roc = 0;

    status = test_set_receiver_roc(packets - 1, roc);
    if (status) {
        return status;
    }

    status = test_set_receiver_roc(packets, roc);
    if (status) {
        return status;
    }

    /* Now the rollover counter should be 1 */
    roc = 1;
    status = test_set_receiver_roc(packets + 1, roc);
    if (status) {
        return status;
    }

    status = test_set_receiver_roc(packets + 60000, roc);
    if (status) {
        return status;
    }

    return srtp_err_status_ok;
}

srtp_err_status_t srtp_test_set_sender_roc()
{
    uint32_t roc;
    uint16_t seq;
    srtp_err_status_t status;

    seq = 43210;
    roc = 0;
    status = test_set_sender_roc(seq, roc);
    if (status) {
        return status;
    }

    roc = 65535;
    status = test_set_sender_roc(seq, roc);
    if (status) {
        return status;
    }

    roc = 0xffff;
    status = test_set_sender_roc(seq, roc);
    if (status) {
        return status;
    }

    roc = 0xffff00;
    status = test_set_sender_roc(seq, roc);
    if (status) {
        return status;
    }

    roc = 0xfffffff0;
    status = test_set_sender_roc(seq, roc);
    if (status) {
        return status;
    }

    return srtp_err_status_ok;
}

/*
 * srtp policy definitions - these definitions are used above
 */

// clang-format off
unsigned char test_key[46] = {
    0xe1, 0xf9, 0x7a, 0x0d, 0x3e, 0x01, 0x8b, 0xe0,
    0xd6, 0x4f, 0xa3, 0x2c, 0x06, 0xde, 0x41, 0x39,
    0x0e, 0xc6, 0x75, 0xad, 0x49, 0x8a, 0xfe, 0xeb,
    0xb6, 0x96, 0x0b, 0x3a, 0xab, 0xe6, 0xc1, 0x73,
    0xc3, 0x17, 0xf2, 0xda, 0xbe, 0x35, 0x77, 0x93,
    0xb6, 0x96, 0x0b, 0x3a, 0xab, 0xe6
};

unsigned char test_key_2[46] = {
    0xf0, 0xf0, 0x49, 0x14, 0xb5, 0x13, 0xf2, 0x76,
    0x3a, 0x1b, 0x1f, 0xa1, 0x30, 0xf1, 0x0e, 0x29,
    0x98, 0xf6, 0xf6, 0xe4, 0x3e, 0x43, 0x09, 0xd1,
    0xe6, 0x22, 0xa0, 0xe3, 0x32, 0xb9, 0xf1, 0xb6,
    0xc3, 0x17, 0xf2, 0xda, 0xbe, 0x35, 0x77, 0x93,
    0xb6, 0x96, 0x0b, 0x3a, 0xab, 0xe6
};

unsigned char test_mki_id[TEST_MKI_ID_SIZE] = {
    0xe1, 0xf9, 0x7a, 0x0d
};

unsigned char test_mki_id_2[TEST_MKI_ID_SIZE] = {
    0xf3, 0xa1, 0x46, 0x71
};
// clang-format on

const srtp_policy_t default_policy = {
    { ssrc_any_outbound, 0 }, /* SSRC */
    {
        /* SRTP policy */
        SRTP_AES_ICM_128,               /* cipher type                 */
        SRTP_AES_ICM_128_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_HMAC_SHA1,                 /* authentication func type    */
        16,                             /* auth key length in octets   */
        10,                             /* auth tag length in octets   */
        sec_serv_conf_and_auth          /* security services flag      */
    },
    {
        /* SRTCP policy */
        SRTP_AES_ICM_128,               /* cipher type                 */
        SRTP_AES_ICM_128_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_HMAC_SHA1,                 /* authentication func type    */
        16,                             /* auth key length in octets   */
        10,                             /* auth tag length in octets   */
        sec_serv_conf_and_auth          /* security services flag      */
    },
    NULL,
    (srtp_master_key_t **)test_keys,
    2,    /* indicates the number of Master keys          */
    NULL, /* indicates that EKT is not in use             */
    128,  /* replay window size                           */
    0,    /* retransmission not allowed                   */
    NULL, /* no encrypted extension headers               */
    0,    /* list of encrypted extension headers is empty */
    NULL
};

const srtp_policy_t aes_only_policy = {
    { ssrc_any_outbound, 0 }, /* SSRC                        */
    {
        SRTP_AES_ICM_128,               /* cipher type                 */
        SRTP_AES_ICM_128_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_NULL_AUTH,                 /* authentication func type    */
        0,                              /* auth key length in octets   */
        0,                              /* auth tag length in octets   */
        sec_serv_conf                   /* security services flag      */
    },
    {
        SRTP_AES_ICM_128,               /* cipher type                 */
        SRTP_AES_ICM_128_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_NULL_AUTH,                 /* authentication func type    */
        0,                              /* auth key length in octets   */
        0,                              /* auth tag length in octets   */
        sec_serv_conf                   /* security services flag      */
    },
    NULL,
    (srtp_master_key_t **)test_keys,
    2,    /* indicates the number of Master keys          */
    NULL, /* indicates that EKT is not in use             */
    128,  /* replay window size                           */
    0,    /* retransmission not allowed                   */
    NULL, /* no encrypted extension headers               */
    0,    /* list of encrypted extension headers is empty */
    NULL
};

const srtp_policy_t hmac_only_policy = {
    { ssrc_any_outbound, 0 }, /* SSRC */
    {
        SRTP_NULL_CIPHER, /* cipher type                 */
        0,                /* cipher key length in octets */
        SRTP_HMAC_SHA1,   /* authentication func type    */
        20,               /* auth key length in octets   */
        4,                /* auth tag length in octets   */
        sec_serv_auth     /* security services flag      */
    },
    {
        SRTP_NULL_CIPHER, /* cipher type                 */
        0,                /* cipher key length in octets */
        SRTP_HMAC_SHA1,   /* authentication func type    */
        20,               /* auth key length in octets   */
        4,                /* auth tag length in octets   */
        sec_serv_auth     /* security services flag      */
    },
    NULL,
    (srtp_master_key_t **)test_keys,
    2,    /* Number of Master keys associated with the policy */
    NULL, /* indicates that EKT is not in use                 */
    128,  /* replay window size                               */
    0,    /* retransmission not allowed                       */
    NULL, /* no encrypted extension headers                   */
    0,    /* list of encrypted extension headers is empty     */
    NULL
};

#ifdef GCM
const srtp_policy_t aes128_gcm_8_policy = {
    { ssrc_any_outbound, 0 }, /* SSRC */
    {
        /* SRTP policy */
        SRTP_AES_GCM_128,               /* cipher type                 */
        SRTP_AES_GCM_128_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_NULL_AUTH,                 /* authentication func type    */
        0,                              /* auth key length in octets   */
        8,                              /* auth tag length in octets   */
        sec_serv_conf_and_auth          /* security services flag      */
    },
    {
        /* SRTCP policy */
        SRTP_AES_GCM_128,               /* cipher type                 */
        SRTP_AES_GCM_128_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_NULL_AUTH,                 /* authentication func type    */
        0,                              /* auth key length in octets   */
        8,                              /* auth tag length in octets   */
        sec_serv_conf_and_auth          /* security services flag      */
    },
    NULL,
    (srtp_master_key_t **)test_keys,
    2,    /* indicates the number of Master keys          */
    NULL, /* indicates that EKT is not in use             */
    128,  /* replay window size                           */
    0,    /* retransmission not allowed                   */
    NULL, /* no encrypted extension headers               */
    0,    /* list of encrypted extension headers is empty */
    NULL
};

const srtp_policy_t aes128_gcm_8_cauth_policy = {
    { ssrc_any_outbound, 0 }, /* SSRC */
    {
        /* SRTP policy */
        SRTP_AES_GCM_128,               /* cipher type                 */
        SRTP_AES_GCM_128_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_NULL_AUTH,                 /* authentication func type    */
        0,                              /* auth key length in octets   */
        8,                              /* auth tag length in octets   */
        sec_serv_conf_and_auth          /* security services flag      */
    },
    {
        /* SRTCP policy */
        SRTP_AES_GCM_128,               /* cipher type                 */
        SRTP_AES_GCM_128_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_NULL_AUTH,                 /* authentication func type    */
        0,                              /* auth key length in octets   */
        8,                              /* auth tag length in octets   */
        sec_serv_auth                   /* security services flag      */
    },
    NULL,
    (srtp_master_key_t **)test_keys,
    2,    /* indicates the number of Master keys          */
    NULL, /* indicates that EKT is not in use             */
    128,  /* replay window size                           */
    0,    /* retransmission not allowed                   */
    NULL, /* no encrypted extension headers               */
    0,    /* list of encrypted extension headers is empty */
    NULL
};

const srtp_policy_t aes256_gcm_8_policy = {
    { ssrc_any_outbound, 0 }, /* SSRC */
    {
        /* SRTP policy */
        SRTP_AES_GCM_256,               /* cipher type                 */
        SRTP_AES_GCM_256_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_NULL_AUTH,                 /* authentication func type    */
        0,                              /* auth key length in octets   */
        8,                              /* auth tag length in octets   */
        sec_serv_conf_and_auth          /* security services flag      */
    },
    {
        /* SRTCP policy */
        SRTP_AES_GCM_256,               /* cipher type                 */
        SRTP_AES_GCM_256_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_NULL_AUTH,                 /* authentication func type    */
        0,                              /* auth key length in octets   */
        8,                              /* auth tag length in octets   */
        sec_serv_conf_and_auth          /* security services flag      */
    },
    NULL,
    (srtp_master_key_t **)test_keys,
    2,    /* indicates the number of Master keys          */
    NULL, /* indicates that EKT is not in use             */
    128,  /* replay window size                           */
    0,    /* retransmission not allowed                   */
    NULL, /* no encrypted extension headers               */
    0,    /* list of encrypted extension headers is empty */
    NULL
};

const srtp_policy_t aes256_gcm_8_cauth_policy = {
    { ssrc_any_outbound, 0 }, /* SSRC */
    {
        /* SRTP policy */
        SRTP_AES_GCM_256,               /* cipher type                 */
        SRTP_AES_GCM_256_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_NULL_AUTH,                 /* authentication func type    */
        0,                              /* auth key length in octets   */
        8,                              /* auth tag length in octets   */
        sec_serv_conf_and_auth          /* security services flag      */
    },
    {
        /* SRTCP policy */
        SRTP_AES_GCM_256,               /* cipher type                 */
        SRTP_AES_GCM_256_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_NULL_AUTH,                 /* authentication func type    */
        0,                              /* auth key length in octets   */
        8,                              /* auth tag length in octets   */
        sec_serv_auth                   /* security services flag      */
    },
    NULL,
    (srtp_master_key_t **)test_keys,
    2,    /* indicates the number of Master keys          */
    NULL, /* indicates that EKT is not in use             */
    128,  /* replay window size                           */
    0,    /* retransmission not allowed                   */
    NULL, /* no encrypted extension headers               */
    0,    /* list of encrypted extension headers is empty */
    NULL
};
#endif

const srtp_policy_t null_policy = {
    { ssrc_any_outbound, 0 }, /* SSRC */
    {
        SRTP_NULL_CIPHER, /* cipher type                 */
        0,                /* cipher key length in octets */
        SRTP_NULL_AUTH,   /* authentication func type    */
        0,                /* auth key length in octets   */
        0,                /* auth tag length in octets   */
        sec_serv_none     /* security services flag      */
    },
    {
        SRTP_NULL_CIPHER, /* cipher type                 */
        0,                /* cipher key length in octets */
        SRTP_NULL_AUTH,   /* authentication func type    */
        0,                /* auth key length in octets   */
        0,                /* auth tag length in octets   */
        sec_serv_none     /* security services flag      */
    },
    NULL,
    (srtp_master_key_t **)test_keys,
    2,    /* indicates the number of Master keys          */
    NULL, /* indicates that EKT is not in use             */
    128,  /* replay window size                           */
    0,    /* retransmission not allowed                   */
    NULL, /* no encrypted extension headers               */
    0,    /* list of encrypted extension headers is empty */
    NULL
};

// clang-format off
unsigned char test_256_key[46] = {
    0xf0, 0xf0, 0x49, 0x14, 0xb5, 0x13, 0xf2, 0x76,
    0x3a, 0x1b, 0x1f, 0xa1, 0x30, 0xf1, 0x0e, 0x29,
    0x98, 0xf6, 0xf6, 0xe4, 0x3e, 0x43, 0x09, 0xd1,
    0xe6, 0x22, 0xa0, 0xe3, 0x32, 0xb9, 0xf1, 0xb6,

    0x3b, 0x04, 0x80, 0x3d, 0xe5, 0x1e, 0xe7, 0xc9,
    0x64, 0x23, 0xab, 0x5b, 0x78, 0xd2
};

unsigned char test_256_key_2[46] = {
    0xe1, 0xf9, 0x7a, 0x0d, 0x3e, 0x01, 0x8b, 0xe0,
    0xd6, 0x4f, 0xa3, 0x2c, 0x06, 0xde, 0x41, 0x39,
    0x0e, 0xc6, 0x75, 0xad, 0x49, 0x8a, 0xfe, 0xeb,
    0xb6, 0x96, 0x0b, 0x3a, 0xab, 0xe6, 0xc1, 0x73,
    0x3b, 0x04, 0x80, 0x3d, 0xe5, 0x1e, 0xe7, 0xc9,
    0x64, 0x23, 0xab, 0x5b, 0x78, 0xd2
};

srtp_master_key_t master_256_key_1 = {
    test_256_key,
    test_mki_id,
    TEST_MKI_ID_SIZE
};

srtp_master_key_t master_256_key_2 = {
    test_256_key_2,
    test_mki_id_2,
    TEST_MKI_ID_SIZE
};

srtp_master_key_t *test_256_keys[2] = {
    &master_key_1,
    &master_key_2
};
// clang-format on

const srtp_policy_t aes_256_hmac_policy = {
    { ssrc_any_outbound, 0 }, /* SSRC */
    {
        /* SRTP policy */
        SRTP_AES_ICM_256,               /* cipher type                 */
        SRTP_AES_ICM_256_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_HMAC_SHA1,                 /* authentication func type    */
        20,                             /* auth key length in octets   */
        10,                             /* auth tag length in octets   */
        sec_serv_conf_and_auth          /* security services flag      */
    },
    {
        /* SRTCP policy */
        SRTP_AES_ICM_256,               /* cipher type                 */
        SRTP_AES_ICM_256_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_HMAC_SHA1,                 /* authentication func type    */
        20,                             /* auth key length in octets   */
        10,                             /* auth tag length in octets   */
        sec_serv_conf_and_auth          /* security services flag      */
    },
    NULL,
    (srtp_master_key_t **)test_256_keys,
    2,    /* indicates the number of Master keys          */
    NULL, /* indicates that EKT is not in use             */
    128,  /* replay window size                           */
    0,    /* retransmission not allowed                   */
    NULL, /* no encrypted extension headers               */
    0,    /* list of encrypted extension headers is empty */
    NULL
};

// clang-format off
uint8_t ekt_test_key[16] = {
    0x77, 0x26, 0x9d, 0xac, 0x16, 0xa3, 0x28, 0xca,
    0x8e, 0xc9, 0x68, 0x4b, 0xcc, 0xc4, 0xd2, 0x1b
};
// clang-format on

#include "ekt.h"

// clang-format off
srtp_ekt_policy_ctx_t ekt_test_policy = {
    0xa5a5,                     /* SPI */
    SRTP_EKT_CIPHER_AES_128_ECB,
    ekt_test_key,
    NULL
};
// clang-format on

const srtp_policy_t hmac_only_with_ekt_policy = {
    { ssrc_any_outbound, 0 }, /* SSRC */
    {
        SRTP_NULL_CIPHER, /* cipher type                 */
        0,                /* cipher key length in octets */
        SRTP_HMAC_SHA1,   /* authentication func type    */
        20,               /* auth key length in octets   */
        4,                /* auth tag length in octets   */
        sec_serv_auth     /* security services flag      */
    },
    {
        SRTP_NULL_CIPHER, /* cipher type                 */
        0,                /* cipher key length in octets */
        SRTP_HMAC_SHA1,   /* authentication func type    */
        20,               /* auth key length in octets   */
        4,                /* auth tag length in octets   */
        sec_serv_auth     /* security services flag      */
    },
    NULL,
    (srtp_master_key_t **)test_keys,
    2,                /* indicates the number of Master keys          */
    &ekt_test_policy, /* indicates that EKT is not in use             */
    128,              /* replay window size                           */
    0,                /* retransmission not allowed                   */
    NULL,             /* no encrypted extension headers               */
    0,                /* list of encrypted extension headers is empty */
    NULL
};

/*
 * an array of pointers to the policies listed above
 *
 * This array is used to test various aspects of libSRTP for
 * different cryptographic policies.  The order of the elements
 * matters - the timing test generates output that can be used
 * in a plot (see the gnuplot script file 'timing').  If you
 * add to this list, you should do it at the end.
 */

// clang-format off
const srtp_policy_t *policy_array[] = {
    &hmac_only_policy,
    &aes_only_policy,
    &default_policy,
#ifdef GCM
    &aes128_gcm_8_policy,
    &aes128_gcm_8_cauth_policy,
    &aes256_gcm_8_policy,
    &aes256_gcm_8_cauth_policy,
#endif
    &null_policy,
    &aes_256_hmac_policy,
    &hmac_only_with_ekt_policy,
    NULL
};
// clang-format on

const srtp_policy_t wildcard_policy = {
    { ssrc_any_outbound, 0 }, /* SSRC */
    {
        /* SRTP policy */
        SRTP_AES_ICM_128,               /* cipher type                 */
        SRTP_AES_ICM_128_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_HMAC_SHA1,                 /* authentication func type    */
        16,                             /* auth key length in octets   */
        10,                             /* auth tag length in octets   */
        sec_serv_conf_and_auth          /* security services flag      */
    },
    {
        /* SRTCP policy */
        SRTP_AES_ICM_128,               /* cipher type                 */
        SRTP_AES_ICM_128_KEY_LEN_WSALT, /* cipher key length in octets */
        SRTP_HMAC_SHA1,                 /* authentication func type    */
        16,                             /* auth key length in octets   */
        10,                             /* auth tag length in octets   */
        sec_serv_conf_and_auth          /* security services flag      */
    },
    test_key,
    NULL,
    0,
    NULL,
    128,  /* replay window size                           */
    0,    /* retransmission not allowed                   */
    NULL, /* no encrypted extension headers               */
    0,    /* list of encrypted extension headers is empty */
    NULL
};
