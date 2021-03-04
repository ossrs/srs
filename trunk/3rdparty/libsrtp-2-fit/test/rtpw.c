/*
 * rtpw.c
 *
 * rtp word sender/receiver
 *
 * David A. McGrew
 * Cisco Systems, Inc.
 *
 * This app is a simple RTP application intended only for testing
 * libsrtp.  It reads one word at a time from words.txt (or
 * whatever file is specified as DICT_FILE or with -w), and sends one word out
 * each USEC_RATE microseconds.  Secure RTP protections can be
 * applied.  See the usage() function for more details.
 *
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "getopt_s.h" /* for local getopt()  */

#include <stdio.h>  /* for printf, fprintf */
#include <stdlib.h> /* for atoi()          */
#include <errno.h>
#include <signal.h> /* for signal()        */

#include <string.h> /* for strncpy()       */
#include <time.h>   /* for usleep()        */

#ifdef HAVE_UNISTD_H
#include <unistd.h> /* for close()         */
#elif defined(_MSC_VER)
#include <io.h> /* for _close()        */
#define close _close
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#elif defined HAVE_WINSOCK2_H
#include <winsock2.h>
#include <ws2tcpip.h>
#define RTPW_USE_WINSOCK2 1
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include "srtp.h"
#include "rtp.h"
#include "util.h"

#define DICT_FILE "words.txt"
#define USEC_RATE (5e5)
#define MAX_WORD_LEN 128
#define ADDR_IS_MULTICAST(a) IN_MULTICAST(htonl(a))
#define MAX_KEY_LEN 96

#ifndef HAVE_USLEEP
#ifdef HAVE_WINDOWS_H
#define usleep(us) Sleep((us) / 1000)
#else
#define usleep(us) sleep((us) / 1000000)
#endif
#endif

/*
 * the function usage() prints an error message describing how this
 * program should be called, then calls exit()
 */

void usage(char *prog_name);

/*
 * leave_group(...) de-registers from a multicast group
 */

void leave_group(int sock, struct ip_mreq mreq, char *name);

/*
 * setup_signal_handler() sets up a signal handler to trigger
 * cleanups after an interrupt
 */
int setup_signal_handler(char *name);

/*
 * handle_signal(...) handles interrupt signal to trigger cleanups
 */

volatile int interrupted = 0;

/*
 * program_type distinguishes the [s]rtp sender and receiver cases
 */

typedef enum { sender, receiver, unknown } program_type;

int main(int argc, char *argv[])
{
    char *dictfile = DICT_FILE;
    FILE *dict;
    char word[MAX_WORD_LEN];
    int sock, ret;
    struct in_addr rcvr_addr;
    struct sockaddr_in name;
    struct ip_mreq mreq;
#if BEW
    struct sockaddr_in local;
#endif
    program_type prog_type = unknown;
    srtp_sec_serv_t sec_servs = sec_serv_none;
    unsigned char ttl = 5;
    int c;
    int key_size = 128;
    int tag_size = 8;
    int gcm_on = 0;
    char *input_key = NULL;
    int b64_input = 0;
    char *address = NULL;
    char key[MAX_KEY_LEN];
    unsigned short port = 0;
    rtp_sender_t snd;
    srtp_policy_t policy;
    srtp_err_status_t status;
    int len;
    int expected_len;
    int do_list_mods = 0;
    uint32_t ssrc = 0xdeadbeef; /* ssrc value hardcoded for now */
#ifdef RTPW_USE_WINSOCK2
    WORD wVersionRequested = MAKEWORD(2, 0);
    WSADATA wsaData;

    ret = WSAStartup(wVersionRequested, &wsaData);
    if (ret != 0) {
        fprintf(stderr, "error: WSAStartup() failed: %d\n", ret);
        exit(1);
    }
#endif

    memset(&policy, 0x0, sizeof(srtp_policy_t));

    printf("Using %s [0x%x]\n", srtp_get_version_string(), srtp_get_version());

    if (setup_signal_handler(argv[0]) != 0) {
        exit(1);
    }

    /* initialize srtp library */
    status = srtp_init();
    if (status) {
        printf("error: srtp initialization failed with error code %d\n",
               status);
        exit(1);
    }

    /* check args */
    while (1) {
        c = getopt_s(argc, argv, "b:k:rsgt:ae:ld:w:");
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
            key_size = atoi(optarg_s);
            if (key_size != 128 && key_size != 256) {
                printf("error: encryption key size must be 128 or 256 (%d)\n",
                       key_size);
                exit(1);
            }
            sec_servs |= sec_serv_conf;
            break;
        case 't':
            tag_size = atoi(optarg_s);
            if (tag_size != 8 && tag_size != 16) {
                printf("error: GCM tag size must be 8 or 16 (%d)\n", tag_size);
                exit(1);
            }
            break;
        case 'a':
            sec_servs |= sec_serv_auth;
            break;
        case 'g':
            gcm_on = 1;
            sec_servs |= sec_serv_auth;
            break;
        case 'r':
            prog_type = receiver;
            break;
        case 's':
            prog_type = sender;
            break;
        case 'd':
            status = srtp_set_debug_module(optarg_s, 1);
            if (status) {
                printf("error: set debug module (%s) failed\n", optarg_s);
                exit(1);
            }
            break;
        case 'l':
            do_list_mods = 1;
            break;
        case 'w':
            dictfile = optarg_s;
            break;
        default:
            usage(argv[0]);
        }
    }

    if (prog_type == unknown) {
        if (do_list_mods) {
            status = srtp_list_debug_modules();
            if (status) {
                printf("error: list of debug modules failed\n");
                exit(1);
            }
            return 0;
        } else {
            printf("error: neither sender [-s] nor receiver [-r] specified\n");
            usage(argv[0]);
        }
    }

    if ((sec_servs && !input_key) || (!sec_servs && input_key)) {
        /*
         * a key must be provided if and only if security services have
         * been requested
         */
        usage(argv[0]);
    }

    if (argc != optind_s + 2) {
        /* wrong number of arguments */
        usage(argv[0]);
    }

    /* get address from arg */
    address = argv[optind_s++];

    /* get port from arg */
    port = atoi(argv[optind_s++]);

/* set address */
#ifdef HAVE_INET_ATON
    if (0 == inet_aton(address, &rcvr_addr)) {
        fprintf(stderr, "%s: cannot parse IP v4 address %s\n", argv[0],
                address);
        exit(1);
    }
    if (rcvr_addr.s_addr == INADDR_NONE) {
        fprintf(stderr, "%s: address error", argv[0]);
        exit(1);
    }
#else
    rcvr_addr.s_addr = inet_addr(address);
    if (0xffffffff == rcvr_addr.s_addr) {
        fprintf(stderr, "%s: cannot parse IP v4 address %s\n", argv[0],
                address);
        exit(1);
    }
#endif

    /* open socket */
    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        int err;
#ifdef RTPW_USE_WINSOCK2
        err = WSAGetLastError();
#else
        err = errno;
#endif
        fprintf(stderr, "%s: couldn't open socket: %d\n", argv[0], err);
        exit(1);
    }

    memset(&name, 0, sizeof(struct sockaddr_in));
    name.sin_addr = rcvr_addr;
    name.sin_family = PF_INET;
    name.sin_port = htons(port);

    if (ADDR_IS_MULTICAST(rcvr_addr.s_addr)) {
        if (prog_type == sender) {
            ret = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
                             sizeof(ttl));
            if (ret < 0) {
                fprintf(stderr, "%s: Failed to set TTL for multicast group",
                        argv[0]);
                perror("");
                exit(1);
            }
        }

        mreq.imr_multiaddr.s_addr = rcvr_addr.s_addr;
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        ret = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&mreq,
                         sizeof(mreq));
        if (ret < 0) {
            fprintf(stderr, "%s: Failed to join multicast group", argv[0]);
            perror("");
            exit(1);
        }
    }

    /* report security services selected on the command line */
    printf("security services: ");
    if (sec_servs & sec_serv_conf)
        printf("confidentiality ");
    if (sec_servs & sec_serv_auth)
        printf("message authentication");
    if (sec_servs == sec_serv_none)
        printf("none");
    printf("\n");

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
#ifdef GCM
                switch (key_size) {
                case 128:
                    srtp_crypto_policy_set_aes_gcm_128_8_auth(&policy.rtp);
                    srtp_crypto_policy_set_aes_gcm_128_8_auth(&policy.rtcp);
                    break;
                case 256:
                    srtp_crypto_policy_set_aes_gcm_256_8_auth(&policy.rtp);
                    srtp_crypto_policy_set_aes_gcm_256_8_auth(&policy.rtcp);
                    break;
                }
#else
                printf("error: GCM mode only supported when using the OpenSSL "
                       "or NSS crypto engine.\n");
                return 0;
#endif
            } else {
                switch (key_size) {
                case 128:
                    srtp_crypto_policy_set_rtp_default(&policy.rtp);
                    srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
                    break;
                case 256:
                    srtp_crypto_policy_set_aes_cm_256_hmac_sha1_80(&policy.rtp);
                    srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
                    break;
                }
            }
            break;
        case sec_serv_conf:
            if (gcm_on) {
                printf(
                    "error: GCM mode must always be used with auth enabled\n");
                return -1;
            } else {
                switch (key_size) {
                case 128:
                    srtp_crypto_policy_set_aes_cm_128_null_auth(&policy.rtp);
                    srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
                    break;
                case 256:
                    srtp_crypto_policy_set_aes_cm_256_null_auth(&policy.rtp);
                    srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
                    break;
                }
            }
            break;
        case sec_serv_auth:
            if (gcm_on) {
#ifdef GCM
                switch (key_size) {
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
            printf("error: unknown security service requested\n");
            return -1;
        }
        policy.ssrc.type = ssrc_specific;
        policy.ssrc.value = ssrc;
        policy.key = (uint8_t *)key;
        policy.ekt = NULL;
        policy.next = NULL;
        policy.window_size = 128;
        policy.allow_repeat_tx = 0;
        policy.rtp.sec_serv = sec_servs;
        policy.rtcp.sec_serv = sec_serv_none; /* we don't do RTCP anyway */

        if (gcm_on && tag_size != 8) {
            policy.rtp.auth_tag_len = tag_size;
        }

        /*
         * read key from hexadecimal or base64 on command line into an octet
         * string
         */
        if (b64_input) {
            int pad;
            expected_len = (policy.rtp.cipher_key_len * 4) / 3;
            len = base64_string_to_octet_string(key, &pad, input_key,
                                                expected_len);
            if (pad != 0) {
                fprintf(stderr, "error: padding in base64 unexpected\n");
                exit(1);
            }
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
        if ((int)strlen(input_key) > policy.rtp.cipher_key_len * 2) {
            fprintf(stderr, "error: too many digits in key/salt "
                            "(should be %d hexadecimal digits, found %u)\n",
                    policy.rtp.cipher_key_len * 2, (unsigned)strlen(input_key));
            exit(1);
        }

        printf("set master key/salt to %s/", octet_string_hex_string(key, 16));
        printf("%s\n", octet_string_hex_string(key + 16, 14));

    } else {
        /*
         * we're not providing security services, so set the policy to the
         * null policy
         *
         * Note that this policy does not conform to the SRTP
         * specification, since RTCP authentication is required.  However,
         * the effect of this policy is to turn off SRTP, so that this
         * application is now a vanilla-flavored RTP application.
         */
        srtp_crypto_policy_set_null_cipher_hmac_null(&policy.rtp);
        srtp_crypto_policy_set_null_cipher_hmac_null(&policy.rtcp);
        policy.key = (uint8_t *)key;
        policy.ssrc.type = ssrc_specific;
        policy.ssrc.value = ssrc;
        policy.window_size = 0;
        policy.allow_repeat_tx = 0;
        policy.ekt = NULL;
        policy.next = NULL;
    }

    if (prog_type == sender) {
#if BEW
        /* bind to local socket (to match crypto policy, if need be) */
        memset(&local, 0, sizeof(struct sockaddr_in));
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(port);
        ret = bind(sock, (struct sockaddr *)&local, sizeof(struct sockaddr_in));
        if (ret < 0) {
            fprintf(stderr, "%s: bind failed\n", argv[0]);
            perror("");
            exit(1);
        }
#endif /* BEW */

        /* initialize sender's rtp and srtp contexts */
        snd = rtp_sender_alloc();
        if (snd == NULL) {
            fprintf(stderr, "error: malloc() failed\n");
            exit(1);
        }
        rtp_sender_init(snd, sock, name, ssrc);
        status = rtp_sender_init_srtp(snd, &policy);
        if (status) {
            fprintf(stderr, "error: srtp_create() failed with code %d\n",
                    status);
            exit(1);
        }

        /* open dictionary */
        dict = fopen(dictfile, "r");
        if (dict == NULL) {
            fprintf(stderr, "%s: couldn't open file %s\n", argv[0], dictfile);
            if (ADDR_IS_MULTICAST(rcvr_addr.s_addr)) {
                leave_group(sock, mreq, argv[0]);
            }
            exit(1);
        }

        /* read words from dictionary, then send them off */
        while (!interrupted && fgets(word, MAX_WORD_LEN, dict) != NULL) {
            len = strlen(word) + 1; /* plus one for null */

            if (len > MAX_WORD_LEN)
                printf("error: word %s too large to send\n", word);
            else {
                rtp_sendto(snd, word, len);
                printf("sending word: %s", word);
            }
            usleep(USEC_RATE);
        }

        rtp_sender_deinit_srtp(snd);
        rtp_sender_dealloc(snd);

        fclose(dict);
    } else { /* prog_type == receiver */
        rtp_receiver_t rcvr;

        if (bind(sock, (struct sockaddr *)&name, sizeof(name)) < 0) {
            close(sock);
            fprintf(stderr, "%s: socket bind error\n", argv[0]);
            perror(NULL);
            if (ADDR_IS_MULTICAST(rcvr_addr.s_addr)) {
                leave_group(sock, mreq, argv[0]);
            }
            exit(1);
        }

        rcvr = rtp_receiver_alloc();
        if (rcvr == NULL) {
            fprintf(stderr, "error: malloc() failed\n");
            exit(1);
        }
        rtp_receiver_init(rcvr, sock, name, ssrc);
        status = rtp_receiver_init_srtp(rcvr, &policy);
        if (status) {
            fprintf(stderr, "error: srtp_create() failed with code %d\n",
                    status);
            exit(1);
        }

        /* get next word and loop */
        while (!interrupted) {
            len = MAX_WORD_LEN;
            if (rtp_recvfrom(rcvr, word, &len) > -1)
                printf("\tword: %s\n", word);
        }

        rtp_receiver_deinit_srtp(rcvr);
        rtp_receiver_dealloc(rcvr);
    }

    if (ADDR_IS_MULTICAST(rcvr_addr.s_addr)) {
        leave_group(sock, mreq, argv[0]);
    }

#ifdef RTPW_USE_WINSOCK2
    ret = closesocket(sock);
#else
    ret = close(sock);
#endif
    if (ret < 0) {
        fprintf(stderr, "%s: Failed to close socket", argv[0]);
        perror("");
    }

    status = srtp_shutdown();
    if (status) {
        printf("error: srtp shutdown failed with error code %d\n", status);
        exit(1);
    }

#ifdef RTPW_USE_WINSOCK2
    WSACleanup();
#endif

    return 0;
}

void usage(char *string)
{
    printf("usage: %s [-d <debug>]* [-k <key> [-a][-e]] "
           "[-s | -r] dest_ip dest_port\n"
           "or     %s -l\n"
           "where  -a use message authentication\n"
           "       -e <key size> use encryption (use 128 or 256 for key size)\n"
           "       -g Use AES-GCM mode (must be used with -e)\n"
           "       -t <tag size> Tag size to use in GCM mode (use 8 or 16)\n"
           "       -k <key>  sets the srtp master key given in hexadecimal\n"
           "       -b <key>  sets the srtp master key given in base64\n"
           "       -s act as rtp sender\n"
           "       -r act as rtp receiver\n"
           "       -l list debug modules\n"
           "       -d <debug> turn on debugging for module <debug>\n"
           "       -w <wordsfile> use <wordsfile> for input, rather than %s\n",
           string, string, DICT_FILE);
    exit(1);
}

void leave_group(int sock, struct ip_mreq mreq, char *name)
{
    int ret;

    ret = setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, (void *)&mreq,
                     sizeof(mreq));
    if (ret < 0) {
        fprintf(stderr, "%s: Failed to leave multicast group", name);
        perror("");
    }
}

void handle_signal(int signum)
{
    interrupted = 1;
    /* Reset handler explicitly, in case we don't have sigaction() (and signal()
       has BSD semantics), or we don't have SA_RESETHAND */
    signal(signum, SIG_DFL);
}

int setup_signal_handler(char *name)
{
#if HAVE_SIGACTION
    struct sigaction act;
    memset(&act, 0, sizeof(act));

    act.sa_handler = handle_signal;
    sigemptyset(&act.sa_mask);
#if defined(SA_RESETHAND)
    act.sa_flags = SA_RESETHAND;
#else
    act.sa_flags = 0;
#endif
    /* Note that we're not setting SA_RESTART; we want recvfrom to return
     * EINTR when we signal the receiver. */

    if (sigaction(SIGTERM, &act, NULL) != 0) {
        fprintf(stderr, "%s: error setting up signal handler", name);
        perror("");
        return -1;
    }
#else
    if (signal(SIGTERM, handle_signal) == SIG_ERR) {
        fprintf(stderr, "%s: error setting up signal handler", name);
        perror("");
        return -1;
    }
#endif
    return 0;
}
