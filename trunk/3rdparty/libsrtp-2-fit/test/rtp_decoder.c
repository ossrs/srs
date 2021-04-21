/*
 * rtp_decoder.c
 *
 * decoder structures and functions for SRTP pcap decoder
 *
 * Example:
 * $ wget --no-check-certificate \
 *     https://raw.githubusercontent.com/gteissier/srtp-decrypt/master/marseillaise-srtp.pcap
 * $ ./test/rtp_decoder -a -t 10 -e 128 -b \
 *     aSBrbm93IGFsbCB5b3VyIGxpdHRsZSBzZWNyZXRz \
 *         < ~/marseillaise-srtp.pcap \
 *         | text2pcap -t "%M:%S." -u 10000,10000 - - \
 *         > ./marseillaise-rtp.pcap
 *
 * There is also a different way of setting up key size and tag size
 * based upon RFC 4568 crypto suite specification, i.e.:
 *
 * $ ./test/rtp_decoder -s AES_CM_128_HMAC_SHA1_80 -b \
 *     aSBrbm93IGFsbCB5b3VyIGxpdHRsZSBzZWNyZXRz ...
 *
 * Audio can be extracted using extractaudio utility from the RTPproxy
 * package:
 *
 * $ extractaudio -A ./marseillaise-rtp.pcap ./marseillaise-out.wav
 *
 * Bernardo Torres <bernardo@torresautomacao.com.br>
 *
 * Some structure and code from https://github.com/gteissier/srtp-decrypt
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
#include "getopt_s.h" /* for local getopt()  */
#include <assert.h>   /* for assert()  */

#include <pcap.h>
#include "rtp_decoder.h"
#include "util.h"

#ifndef timersub
#define timersub(a, b, result)                                                 \
    do {                                                                       \
        (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                          \
        (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                       \
        if ((result)->tv_usec < 0) {                                           \
            --(result)->tv_sec;                                                \
            (result)->tv_usec += 1000000;                                      \
        }                                                                      \
    } while (0)
#endif

#define MAX_KEY_LEN 96
#define MAX_FILTER 256
#define MAX_FILE 255

struct srtp_crypto_suite {
    const char *can_name;
    int gcm_on;
    int key_size;
    int tag_size;
};

static struct srtp_crypto_suite srtp_crypto_suites[] = {
#if 0
  {.can_name = "F8_128_HMAC_SHA1_32", .gcm_on = 0, .key_size = 128, .tag_size = 4},
#endif
    {.can_name = "AES_CM_128_HMAC_SHA1_32",
     .gcm_on = 0,
     .key_size = 128,
     .tag_size = 4 },
    {.can_name = "AES_CM_128_HMAC_SHA1_80",
     .gcm_on = 0,
     .key_size = 128,
     .tag_size = 10 },
    {.can_name = "AES_192_CM_HMAC_SHA1_32",
     .gcm_on = 0,
     .key_size = 192,
     .tag_size = 4 },
    {.can_name = "AES_192_CM_HMAC_SHA1_80",
     .gcm_on = 0,
     .key_size = 192,
     .tag_size = 10 },
    {.can_name = "AES_256_CM_HMAC_SHA1_32",
     .gcm_on = 0,
     .key_size = 256,
     .tag_size = 4 },
    {.can_name = "AES_256_CM_HMAC_SHA1_80",
     .gcm_on = 0,
     .key_size = 256,
     .tag_size = 10 },
    {.can_name = "AEAD_AES_128_GCM",
     .gcm_on = 1,
     .key_size = 128,
     .tag_size = 16 },
    {.can_name = "AEAD_AES_256_GCM",
     .gcm_on = 1,
     .key_size = 256,
     .tag_size = 16 },
    {.can_name = NULL }
};

void rtp_decoder_srtp_log_handler(srtp_log_level_t level,
                                  const char *msg,
                                  void *data)
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
    fprintf(stderr, "SRTP-LOG [%c]: %s\n", level_char, msg);
}

int main(int argc, char *argv[])
{
    char errbuf[PCAP_ERRBUF_SIZE];
    bpf_u_int32 pcap_net = 0;
    pcap_t *pcap_handle;
#if BEW
    struct sockaddr_in local;
#endif
    srtp_sec_serv_t sec_servs = sec_serv_none;
    int c;
    struct srtp_crypto_suite scs, *i_scsp;
    scs.key_size = 128;
    scs.tag_size = 0;
    int gcm_on = 0;
    char *input_key = NULL;
    int b64_input = 0;
    char key[MAX_KEY_LEN];
    struct bpf_program fp;
    char filter_exp[MAX_FILTER] = "";
    char pcap_file[MAX_FILE] = "-";
    int rtp_packet_offset = DEFAULT_RTP_OFFSET;
    rtp_decoder_t dec;
    srtp_policy_t policy = { { 0 } };
    rtp_decoder_mode_t mode = mode_rtp;
    srtp_err_status_t status;
    int len;
    int expected_len;
    int do_list_mods = 0;

    fprintf(stderr, "Using %s [0x%x]\n", srtp_get_version_string(),
            srtp_get_version());

    /* initialize srtp library */
    status = srtp_init();
    if (status) {
        fprintf(stderr,
                "error: srtp initialization failed with error code %d\n",
                status);
        exit(1);
    }

    status = srtp_install_log_handler(rtp_decoder_srtp_log_handler, NULL);
    if (status) {
        fprintf(stderr, "error: install log handler failed\n");
        exit(1);
    }

    /* check args */
    while (1) {
        c = getopt_s(argc, argv, "b:k:gt:ae:ld:f:s:m:p:o:");
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'b':
            b64_input = 1;
        /* fall thru */
        case 'k':
            input_key = optarg_s;
            break;
        case 'e':
            scs.key_size = atoi(optarg_s);
            if (scs.key_size != 128 && scs.key_size != 192 &&
                scs.key_size != 256) {
                fprintf(
                    stderr,
                    "error: encryption key size must be 128, 192 or 256 (%d)\n",
                    scs.key_size);
                exit(1);
            }
            input_key = malloc(scs.key_size);
            sec_servs |= sec_serv_conf;
            break;
        case 't':
            scs.tag_size = atoi(optarg_s);
            break;
        case 'a':
            sec_servs |= sec_serv_auth;
            break;
        case 'g':
            gcm_on = 1;
            sec_servs |= sec_serv_auth;
            break;
        case 'd':
            status = srtp_set_debug_module(optarg_s, 1);
            if (status) {
                fprintf(stderr, "error: set debug module (%s) failed\n",
                        optarg_s);
                exit(1);
            }
            break;
        case 'f':
            if (strlen(optarg_s) > MAX_FILTER) {
                fprintf(stderr, "error: filter bigger than %d characters\n",
                        MAX_FILTER);
                exit(1);
            }
            fprintf(stderr, "Setting filter as %s\n", optarg_s);
            strcpy(filter_exp, optarg_s);
            break;
        case 'l':
            do_list_mods = 1;
            break;
        case 's':
            for (i_scsp = &srtp_crypto_suites[0]; i_scsp->can_name != NULL;
                 i_scsp++) {
                if (strcasecmp(i_scsp->can_name, optarg_s) == 0) {
                    break;
                }
            }
            if (i_scsp->can_name == NULL) {
                fprintf(stderr, "Unknown/unsupported crypto suite name %s\n",
                        optarg_s);
                exit(1);
            }
            scs = *i_scsp;
            input_key = malloc(scs.key_size);
            sec_servs |= sec_serv_conf | sec_serv_auth;
            gcm_on = scs.gcm_on;
            break;
        case 'm':
            if (strcasecmp("rtp", optarg_s) == 0) {
                mode = mode_rtp;
            } else if (strcasecmp("rtcp", optarg_s) == 0) {
                mode = mode_rtcp;
            } else if (strcasecmp("rtcp-mux", optarg_s) == 0) {
                mode = mode_rtcp_mux;
            } else {
                fprintf(stderr, "Unknown/unsupported mode %s\n", optarg_s);
                exit(1);
            }
            break;
        case 'p':
            if (strlen(optarg_s) > MAX_FILE) {
                fprintf(stderr,
                        "error: pcap file path bigger than %d characters\n",
                        MAX_FILE);
                exit(1);
            }
            strcpy(pcap_file, optarg_s);
            break;
        case 'o':
            rtp_packet_offset = atoi(optarg_s);
            break;
        default:
            usage(argv[0]);
        }
    }

    if (scs.tag_size == 0) {
        if (gcm_on) {
            scs.tag_size = 16;
        } else {
            scs.tag_size = 10;
        }
    }

    if (gcm_on && scs.tag_size != 8 && scs.tag_size != 16) {
        fprintf(stderr, "error: GCM tag size must be 8 or 16 (%d)\n",
                scs.tag_size);
        exit(1);
    }

    if (!gcm_on && scs.tag_size != 4 && scs.tag_size != 10) {
        fprintf(stderr, "error: non GCM tag size must be 4 or 10 (%d)\n",
                scs.tag_size);
        exit(1);
    }

    if (do_list_mods) {
        status = srtp_list_debug_modules();
        if (status) {
            fprintf(stderr, "error: list of debug modules failed\n");
            exit(1);
        }
        return 0;
    }

    if ((sec_servs && !input_key) || (!sec_servs && input_key)) {
        /*
         * a key must be provided if and only if security services have
         * been requested
         */
        if (input_key == NULL) {
            fprintf(stderr, "key not provided\n");
        }
        if (!sec_servs) {
            fprintf(stderr, "no secservs\n");
        }
        fprintf(stderr, "provided\n");
        usage(argv[0]);
    }

    /* report security services selected on the command line */
    fprintf(stderr, "security services: ");
    if (sec_servs & sec_serv_conf)
        fprintf(stderr, "confidentiality ");
    if (sec_servs & sec_serv_auth)
        fprintf(stderr, "message authentication");
    if (sec_servs == sec_serv_none)
        fprintf(stderr, "none");
    fprintf(stderr, "\n");

    /* set up the srtp policy and master key */
    if (sec_servs) {
        /*
         * create policy structure, using the default mechanisms but
         * with only the security services requested on the command line,
         * using the right SSRC value
         */
        switch (sec_servs) {
        case sec_serv_conf_and_auth:
            if (gcm_on) {
#ifdef OPENSSL
                switch (scs.key_size) {
                case 128:
                    if (scs.tag_size == 16) {
                        srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy.rtp);
                        srtp_crypto_policy_set_aes_gcm_128_16_auth(
                            &policy.rtcp);
                    } else {
                        srtp_crypto_policy_set_aes_gcm_128_8_auth(&policy.rtp);
                        srtp_crypto_policy_set_aes_gcm_128_8_auth(&policy.rtcp);
                    }
                    break;
                case 256:
                    if (scs.tag_size == 16) {
                        srtp_crypto_policy_set_aes_gcm_256_16_auth(&policy.rtp);
                        srtp_crypto_policy_set_aes_gcm_256_16_auth(
                            &policy.rtcp);
                    } else {
                        srtp_crypto_policy_set_aes_gcm_256_8_auth(&policy.rtp);
                        srtp_crypto_policy_set_aes_gcm_256_8_auth(&policy.rtcp);
                    }
                    break;
                }
#else
                fprintf(stderr, "error: GCM mode only supported when using the "
                                "OpenSSL crypto engine.\n");
                return 0;
#endif
            } else {
                switch (scs.key_size) {
                case 128:
                    if (scs.tag_size == 4) {
                        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32(
                            &policy.rtp);
                        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(
                            &policy.rtcp);
                    } else {
                        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(
                            &policy.rtp);
                        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(
                            &policy.rtcp);
                    }
                    break;
                case 192:
#ifdef OPENSSL
                    if (scs.tag_size == 4) {
                        srtp_crypto_policy_set_aes_cm_192_hmac_sha1_32(
                            &policy.rtp);
                        srtp_crypto_policy_set_aes_cm_192_hmac_sha1_80(
                            &policy.rtcp);
                    } else {
                        srtp_crypto_policy_set_aes_cm_192_hmac_sha1_80(
                            &policy.rtp);
                        srtp_crypto_policy_set_aes_cm_192_hmac_sha1_80(
                            &policy.rtcp);
                    }
#else
                    fprintf(stderr,
                            "error: AES 192 mode only supported when using the "
                            "OpenSSL crypto engine.\n");
                    return 0;

#endif
                    break;
                case 256:
                    if (scs.tag_size == 4) {
                        srtp_crypto_policy_set_aes_cm_256_hmac_sha1_32(
                            &policy.rtp);
                        srtp_crypto_policy_set_aes_cm_256_hmac_sha1_80(
                            &policy.rtcp);
                    } else {
                        srtp_crypto_policy_set_aes_cm_256_hmac_sha1_80(
                            &policy.rtp);
                        srtp_crypto_policy_set_aes_cm_256_hmac_sha1_80(
                            &policy.rtcp);
                    }
                    break;
                }
            }
            break;
        case sec_serv_conf:
            if (gcm_on) {
                fprintf(
                    stderr,
                    "error: GCM mode must always be used with auth enabled\n");
                return -1;
            } else {
                switch (scs.key_size) {
                case 128:
                    srtp_crypto_policy_set_aes_cm_128_null_auth(&policy.rtp);
                    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(
                        &policy.rtcp);
                    break;
                case 192:
#ifdef OPENSSL
                    srtp_crypto_policy_set_aes_cm_192_null_auth(&policy.rtp);
                    srtp_crypto_policy_set_aes_cm_192_hmac_sha1_80(
                        &policy.rtcp);
#else
                    fprintf(stderr,
                            "error: AES 192 mode only supported when using the "
                            "OpenSSL crypto engine.\n");
                    return 0;

#endif
                    break;
                case 256:
                    srtp_crypto_policy_set_aes_cm_256_null_auth(&policy.rtp);
                    srtp_crypto_policy_set_aes_cm_256_hmac_sha1_80(
                        &policy.rtcp);
                    break;
                }
            }
            break;
        case sec_serv_auth:
            if (gcm_on) {
#ifdef OPENSSL
                switch (scs.key_size) {
                case 128:
                    srtp_crypto_policy_set_aes_gcm_128_8_only_auth(&policy.rtp);
                    srtp_crypto_policy_set_aes_gcm_128_8_only_auth(
                        &policy.rtcp);
                    break;
                case 256:
                    srtp_crypto_policy_set_aes_gcm_256_8_only_auth(&policy.rtp);
                    srtp_crypto_policy_set_aes_gcm_256_8_only_auth(
                        &policy.rtcp);
                    break;
                }
#else
                printf("error: GCM mode only supported when using the OpenSSL "
                       "crypto engine.\n");
                return 0;
#endif
            } else {
                srtp_crypto_policy_set_null_cipher_hmac_sha1_80(&policy.rtp);
                srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
            }
            break;
        default:
            fprintf(stderr, "error: unknown security service requested\n");
            return -1;
        }

        policy.key = (uint8_t *)key;
        policy.ekt = NULL;
        policy.next = NULL;
        policy.window_size = 128;
        policy.allow_repeat_tx = 0;
        policy.rtp.sec_serv = sec_servs;
        policy.rtcp.sec_serv =
            sec_servs; // sec_serv_none;  /* we don't do RTCP anyway */
        fprintf(stderr, "setting tag len %d\n", scs.tag_size);
        policy.rtp.auth_tag_len = scs.tag_size;

        if (gcm_on && scs.tag_size != 8) {
            fprintf(stderr, "set tag len %d\n", scs.tag_size);
            policy.rtp.auth_tag_len = scs.tag_size;
        }

        /*
         * read key from hexadecimal or base64 on command line into an octet
         * string
         */
        if (b64_input) {
            int pad;
            expected_len = policy.rtp.cipher_key_len * 4 / 3;
            len = base64_string_to_octet_string(key, &pad, input_key,
                                                strlen(input_key));
        } else {
            expected_len = policy.rtp.cipher_key_len * 2;
            len = hex_string_to_octet_string(key, input_key, expected_len);
        }
        /* check that hex string is the right length */
        if (len < expected_len) {
            fprintf(stderr, "error: too few digits in key/salt "
                            "(should be %d digits, found %d)\n",
                    expected_len, len);
            exit(1);
        }
        if (strlen(input_key) > policy.rtp.cipher_key_len * 2) {
            fprintf(stderr, "error: too many digits in key/salt "
                            "(should be %d hexadecimal digits, found %u)\n",
                    policy.rtp.cipher_key_len * 2, (unsigned)strlen(input_key));
            exit(1);
        }

        int key_octets = (scs.key_size / 8);
        int salt_octets = policy.rtp.cipher_key_len - key_octets;
        fprintf(stderr, "set master key/salt to %s/",
                octet_string_hex_string(key, key_octets));
        fprintf(stderr, "%s\n",
                octet_string_hex_string(key + key_octets, salt_octets));

    } else {
        fprintf(stderr,
                "error: neither encryption or authentication were selected\n");
        exit(1);
    }

    pcap_handle = pcap_open_offline(pcap_file, errbuf);

    if (!pcap_handle) {
        fprintf(stderr, "libpcap failed to open file '%s'\n", errbuf);
        exit(1);
    }
    assert(pcap_handle != NULL);
    if ((pcap_compile(pcap_handle, &fp, filter_exp, 1, pcap_net)) == -1) {
        fprintf(stderr, "Couldn't parse filter %s: %s\n", filter_exp,
                pcap_geterr(pcap_handle));
        return (2);
    }
    if (pcap_setfilter(pcap_handle, &fp) == -1) {
        fprintf(stderr, "couldn't install filter %s: %s\n", filter_exp,
                pcap_geterr(pcap_handle));
        return (2);
    }
    dec = rtp_decoder_alloc();
    if (dec == NULL) {
        fprintf(stderr, "error: malloc() failed\n");
        exit(1);
    }
    fprintf(stderr, "Starting decoder\n");
    if (rtp_decoder_init(dec, policy, mode, rtp_packet_offset)) {
        fprintf(stderr, "error: init failed\n");
        exit(1);
    }

    pcap_loop(pcap_handle, 0, rtp_decoder_handle_pkt, (u_char *)dec);

    if (dec->mode == mode_rtp || dec->mode == mode_rtcp_mux) {
        fprintf(stderr, "RTP packets decoded: %d\n", dec->rtp_cnt);
    }
    if (dec->mode == mode_rtcp || dec->mode == mode_rtcp_mux) {
        fprintf(stderr, "RTCP packets decoded: %d\n", dec->rtcp_cnt);
    }
    fprintf(stderr, "Packet decode errors: %d\n", dec->error_cnt);

    rtp_decoder_deinit(dec);
    rtp_decoder_dealloc(dec);

    status = srtp_shutdown();
    if (status) {
        fprintf(stderr, "error: srtp shutdown failed with error code %d\n",
                status);
        exit(1);
    }

    return 0;
}

void usage(char *string)
{
    fprintf(
        stderr,
        "usage: %s [-d <debug>]* [[-k][-b] <key>] [-a][-t][-e] [-s "
        "<srtp-crypto-suite>] [-m <mode>]\n"
        "or     %s -l\n"
        "where  -a use message authentication\n"
        "       -e <key size> use encryption (use 128 or 256 for key size)\n"
        "       -g Use AES-GCM mode (must be used with -e)\n"
        "       -t <tag size> Tag size to use (in GCM mode use 8 or 16)\n"
        "       -k <key>  sets the srtp master key given in hexadecimal\n"
        "       -b <key>  sets the srtp master key given in base64\n"
        "       -l list debug modules\n"
        "       -f \"<pcap filter>\" to filter only the desired SRTP packets\n"
        "       -d <debug> turn on debugging for module <debug>\n"
        "       -s \"<srtp-crypto-suite>\" to set both key and tag size based\n"
        "          on RFC4568-style crypto suite specification\n"
        "       -m <mode> set the mode to be one of [rtp]|rtcp|rtcp-mux\n"
        "       -p <pcap file> path to pcap file (defaults to stdin)\n"
        "       -o byte offset of RTP packet in capture (defaults to 42)\n",
        string, string);
    exit(1);
}

rtp_decoder_t rtp_decoder_alloc(void)
{
    return (rtp_decoder_t)malloc(sizeof(rtp_decoder_ctx_t));
}

void rtp_decoder_dealloc(rtp_decoder_t rtp_ctx)
{
    free(rtp_ctx);
}

int rtp_decoder_deinit(rtp_decoder_t decoder)
{
    if (decoder->srtp_ctx) {
        return srtp_dealloc(decoder->srtp_ctx);
    }
    return 0;
}

int rtp_decoder_init(rtp_decoder_t dcdr,
                     srtp_policy_t policy,
                     rtp_decoder_mode_t mode,
                     int rtp_packet_offset)
{
    dcdr->rtp_offset = rtp_packet_offset;
    dcdr->srtp_ctx = NULL;
    dcdr->start_tv.tv_usec = 0;
    dcdr->start_tv.tv_sec = 0;
    dcdr->frame_nr = -1;
    dcdr->error_cnt = 0;
    dcdr->rtp_cnt = 0;
    dcdr->rtcp_cnt = 0;
    dcdr->mode = mode;
    dcdr->policy = policy;
    dcdr->policy.ssrc.type = ssrc_any_inbound;

    if (srtp_create(&dcdr->srtp_ctx, &dcdr->policy)) {
        return 1;
    }
    return 0;
}

/*
 * decodes key as base64
 */

void hexdump(const void *ptr, size_t size)
{
    int i, j;
    const unsigned char *cptr = ptr;

    for (i = 0; i < size; i += 16) {
        fprintf(stdout, "%04x ", i);
        for (j = 0; j < 16 && i + j < size; j++) {
            fprintf(stdout, "%02x ", cptr[i + j]);
        }
        fprintf(stdout, "\n");
    }
}

void rtp_decoder_handle_pkt(u_char *arg,
                            const struct pcap_pkthdr *hdr,
                            const u_char *bytes)
{
    rtp_decoder_t dcdr = (rtp_decoder_t)arg;
    rtp_msg_t message;
    int rtp;
    int pktsize;
    struct timeval delta;
    int octets_recvd;
    srtp_err_status_t status;
    dcdr->frame_nr++;

    if ((dcdr->start_tv.tv_sec == 0) && (dcdr->start_tv.tv_usec == 0)) {
        dcdr->start_tv = hdr->ts;
    }

    if (hdr->caplen < dcdr->rtp_offset) {
        return;
    }
    const void *rtp_packet = bytes + dcdr->rtp_offset;

    memcpy((void *)&message, rtp_packet, hdr->caplen - dcdr->rtp_offset);
    pktsize = hdr->caplen - dcdr->rtp_offset;
    octets_recvd = pktsize;

    if (octets_recvd == -1) {
        return;
    }

    if (dcdr->mode == mode_rtp) {
        rtp = 1;
    } else if (dcdr->mode == mode_rtcp) {
        rtp = 0;
    } else {
        rtp = 1;
        if (octets_recvd >= 2) {
            /* rfc5761 */
            u_char payload_type = *(bytes + dcdr->rtp_offset + 1) & 0x7f;
            rtp = payload_type < 64 || payload_type > 95;
        }
    }

    if (rtp) {
        /* verify rtp header */
        if (message.header.version != 2) {
            return;
        }

        status = srtp_unprotect(dcdr->srtp_ctx, &message, &octets_recvd);
        if (status) {
            dcdr->error_cnt++;
            return;
        }
        dcdr->rtp_cnt++;
    } else {
        status = srtp_unprotect_rtcp(dcdr->srtp_ctx, &message, &octets_recvd);
        if (status) {
            dcdr->error_cnt++;
            return;
        }
        dcdr->rtcp_cnt++;
    }
    timersub(&hdr->ts, &dcdr->start_tv, &delta);
    fprintf(stdout, "%02ld:%02ld.%06ld\n", delta.tv_sec / 60, delta.tv_sec % 60,
            (long)delta.tv_usec);
    hexdump(&message, octets_recvd);
}

void rtp_print_error(srtp_err_status_t status, char *message)
{
    // clang-format off
    fprintf(stderr,
            "error: %s %d%s\n", message, status,
            status == srtp_err_status_replay_fail ? " (replay check failed)" :
            status == srtp_err_status_bad_param ? " (bad param)" :
            status == srtp_err_status_no_ctx ? " (no context)" :
            status == srtp_err_status_cipher_fail ? " (cipher failed)" :
            status == srtp_err_status_key_expired ? " (key expired)" :
            status == srtp_err_status_auth_fail ? " (auth check failed)" : "");
    // clang-format on
}
