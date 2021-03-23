/*
 * sha1.c
 *
 * an implementation of the Secure Hash Algorithm v.1 (SHA-1),
 * specified in FIPS 180-1
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "sha1.h"

srtp_debug_module_t srtp_mod_sha1 = {
    0,      /* debugging is off by default */
    "sha-1" /* printable module name       */
};

/* SN == Rotate left N bits */
#define S1(X) ((X << 1) | (X >> 31))
#define S5(X) ((X << 5) | (X >> 27))
#define S30(X) ((X << 30) | (X >> 2))

#define f0(B, C, D) ((B & C) | (~B & D))
#define f1(B, C, D) (B ^ C ^ D)
#define f2(B, C, D) ((B & C) | (B & D) | (C & D))
#define f3(B, C, D) (B ^ C ^ D)

/*
 * nota bene: the variable K0 appears in the curses library, so we
 * give longer names to these variables to avoid spurious warnings
 * on systems that uses curses
 */

uint32_t SHA_K0 = 0x5A827999; /* Kt for 0  <= t <= 19 */
uint32_t SHA_K1 = 0x6ED9EBA1; /* Kt for 20 <= t <= 39 */
uint32_t SHA_K2 = 0x8F1BBCDC; /* Kt for 40 <= t <= 59 */
uint32_t SHA_K3 = 0xCA62C1D6; /* Kt for 60 <= t <= 79 */

void srtp_sha1(const uint8_t *msg, int octets_in_msg, uint32_t hash_value[5])
{
    srtp_sha1_ctx_t ctx;

    srtp_sha1_init(&ctx);
    srtp_sha1_update(&ctx, msg, octets_in_msg);
    srtp_sha1_final(&ctx, hash_value);
}

/*
 *  srtp_sha1_core(M, H) computes the core compression function, where M is
 *  the next part of the message (in network byte order) and H is the
 *  intermediate state { H0, H1, ...} (in host byte order)
 *
 *  this function does not do any of the padding required in the
 *  complete SHA1 function
 *
 *  this function is used in the SEAL 3.0 key setup routines
 *  (crypto/cipher/seal.c)
 */

void srtp_sha1_core(const uint32_t M[16], uint32_t hash_value[5])
{
    uint32_t H0;
    uint32_t H1;
    uint32_t H2;
    uint32_t H3;
    uint32_t H4;
    uint32_t W[80];
    uint32_t A, B, C, D, E, TEMP;
    int t;

    /* copy hash_value into H0, H1, H2, H3, H4 */
    H0 = hash_value[0];
    H1 = hash_value[1];
    H2 = hash_value[2];
    H3 = hash_value[3];
    H4 = hash_value[4];

    /* copy/xor message into array */

    W[0] = be32_to_cpu(M[0]);
    W[1] = be32_to_cpu(M[1]);
    W[2] = be32_to_cpu(M[2]);
    W[3] = be32_to_cpu(M[3]);
    W[4] = be32_to_cpu(M[4]);
    W[5] = be32_to_cpu(M[5]);
    W[6] = be32_to_cpu(M[6]);
    W[7] = be32_to_cpu(M[7]);
    W[8] = be32_to_cpu(M[8]);
    W[9] = be32_to_cpu(M[9]);
    W[10] = be32_to_cpu(M[10]);
    W[11] = be32_to_cpu(M[11]);
    W[12] = be32_to_cpu(M[12]);
    W[13] = be32_to_cpu(M[13]);
    W[14] = be32_to_cpu(M[14]);
    W[15] = be32_to_cpu(M[15]);
    TEMP = W[13] ^ W[8] ^ W[2] ^ W[0];
    W[16] = S1(TEMP);
    TEMP = W[14] ^ W[9] ^ W[3] ^ W[1];
    W[17] = S1(TEMP);
    TEMP = W[15] ^ W[10] ^ W[4] ^ W[2];
    W[18] = S1(TEMP);
    TEMP = W[16] ^ W[11] ^ W[5] ^ W[3];
    W[19] = S1(TEMP);
    TEMP = W[17] ^ W[12] ^ W[6] ^ W[4];
    W[20] = S1(TEMP);
    TEMP = W[18] ^ W[13] ^ W[7] ^ W[5];
    W[21] = S1(TEMP);
    TEMP = W[19] ^ W[14] ^ W[8] ^ W[6];
    W[22] = S1(TEMP);
    TEMP = W[20] ^ W[15] ^ W[9] ^ W[7];
    W[23] = S1(TEMP);
    TEMP = W[21] ^ W[16] ^ W[10] ^ W[8];
    W[24] = S1(TEMP);
    TEMP = W[22] ^ W[17] ^ W[11] ^ W[9];
    W[25] = S1(TEMP);
    TEMP = W[23] ^ W[18] ^ W[12] ^ W[10];
    W[26] = S1(TEMP);
    TEMP = W[24] ^ W[19] ^ W[13] ^ W[11];
    W[27] = S1(TEMP);
    TEMP = W[25] ^ W[20] ^ W[14] ^ W[12];
    W[28] = S1(TEMP);
    TEMP = W[26] ^ W[21] ^ W[15] ^ W[13];
    W[29] = S1(TEMP);
    TEMP = W[27] ^ W[22] ^ W[16] ^ W[14];
    W[30] = S1(TEMP);
    TEMP = W[28] ^ W[23] ^ W[17] ^ W[15];
    W[31] = S1(TEMP);

    /* process the remainder of the array */
    for (t = 32; t < 80; t++) {
        TEMP = W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16];
        W[t] = S1(TEMP);
    }

    A = H0;
    B = H1;
    C = H2;
    D = H3;
    E = H4;

    for (t = 0; t < 20; t++) {
        TEMP = S5(A) + f0(B, C, D) + E + W[t] + SHA_K0;
        E = D;
        D = C;
        C = S30(B);
        B = A;
        A = TEMP;
    }
    for (; t < 40; t++) {
        TEMP = S5(A) + f1(B, C, D) + E + W[t] + SHA_K1;
        E = D;
        D = C;
        C = S30(B);
        B = A;
        A = TEMP;
    }
    for (; t < 60; t++) {
        TEMP = S5(A) + f2(B, C, D) + E + W[t] + SHA_K2;
        E = D;
        D = C;
        C = S30(B);
        B = A;
        A = TEMP;
    }
    for (; t < 80; t++) {
        TEMP = S5(A) + f3(B, C, D) + E + W[t] + SHA_K3;
        E = D;
        D = C;
        C = S30(B);
        B = A;
        A = TEMP;
    }

    hash_value[0] = H0 + A;
    hash_value[1] = H1 + B;
    hash_value[2] = H2 + C;
    hash_value[3] = H3 + D;
    hash_value[4] = H4 + E;

    return;
}

void srtp_sha1_init(srtp_sha1_ctx_t *ctx)
{
    /* initialize state vector */
    ctx->H[0] = 0x67452301;
    ctx->H[1] = 0xefcdab89;
    ctx->H[2] = 0x98badcfe;
    ctx->H[3] = 0x10325476;
    ctx->H[4] = 0xc3d2e1f0;

    /* indicate that message buffer is empty */
    ctx->octets_in_buffer = 0;

    /* reset message bit-count to zero */
    ctx->num_bits_in_msg = 0;
}

void srtp_sha1_update(srtp_sha1_ctx_t *ctx,
                      const uint8_t *msg,
                      int octets_in_msg)
{
    int i;
    uint8_t *buf = (uint8_t *)ctx->M;

    /* update message bit-count */
    ctx->num_bits_in_msg += octets_in_msg * 8;

    /* loop over 16-word blocks of M */
    while (octets_in_msg > 0) {
        if (octets_in_msg + ctx->octets_in_buffer >= 64) {
            /*
             * copy words of M into msg buffer until that buffer is full,
             * converting them into host byte order as needed
             */
            octets_in_msg -= (64 - ctx->octets_in_buffer);
            for (i = ctx->octets_in_buffer; i < 64; i++) {
                buf[i] = *msg++;
            }
            ctx->octets_in_buffer = 0;

            /* process a whole block */

            debug_print0(srtp_mod_sha1, "(update) running srtp_sha1_core()");

            srtp_sha1_core(ctx->M, ctx->H);

        } else {
            debug_print0(srtp_mod_sha1,
                         "(update) not running srtp_sha1_core()");

            for (i = ctx->octets_in_buffer;
                 i < (ctx->octets_in_buffer + octets_in_msg); i++) {
                buf[i] = *msg++;
            }
            ctx->octets_in_buffer += octets_in_msg;
            octets_in_msg = 0;
        }
    }
}

/*
 * srtp_sha1_final(ctx, output) computes the result for ctx and copies it
 * into the twenty octets located at *output
 */

void srtp_sha1_final(srtp_sha1_ctx_t *ctx, uint32_t *output)
{
    uint32_t A, B, C, D, E, TEMP;
    uint32_t W[80];
    int i, t;

    /*
     * process the remaining octets_in_buffer, padding and terminating as
     * necessary
     */
    {
        int tail = ctx->octets_in_buffer % 4;

        /* copy/xor message into array */
        for (i = 0; i < (ctx->octets_in_buffer + 3) / 4; i++) {
            W[i] = be32_to_cpu(ctx->M[i]);
        }

        /* set the high bit of the octet immediately following the message */
        switch (tail) {
        case (3):
            W[i - 1] = (be32_to_cpu(ctx->M[i - 1]) & 0xffffff00) | 0x80;
            W[i] = 0x0;
            break;
        case (2):
            W[i - 1] = (be32_to_cpu(ctx->M[i - 1]) & 0xffff0000) | 0x8000;
            W[i] = 0x0;
            break;
        case (1):
            W[i - 1] = (be32_to_cpu(ctx->M[i - 1]) & 0xff000000) | 0x800000;
            W[i] = 0x0;
            break;
        case (0):
            W[i] = 0x80000000;
            break;
        }

        /* zeroize remaining words */
        for (i++; i < 15; i++) {
            W[i] = 0x0;
        }

        /*
         * if there is room at the end of the word array, then set the
         * last word to the bit-length of the message; otherwise, set that
         * word to zero and then we need to do one more run of the
         * compression algo.
         */
        if (ctx->octets_in_buffer < 56) {
            W[15] = ctx->num_bits_in_msg;
        } else if (ctx->octets_in_buffer < 60) {
            W[15] = 0x0;
        }

        /* process the word array */
        for (t = 16; t < 80; t++) {
            TEMP = W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16];
            W[t] = S1(TEMP);
        }

        A = ctx->H[0];
        B = ctx->H[1];
        C = ctx->H[2];
        D = ctx->H[3];
        E = ctx->H[4];

        for (t = 0; t < 20; t++) {
            TEMP = S5(A) + f0(B, C, D) + E + W[t] + SHA_K0;
            E = D;
            D = C;
            C = S30(B);
            B = A;
            A = TEMP;
        }
        for (; t < 40; t++) {
            TEMP = S5(A) + f1(B, C, D) + E + W[t] + SHA_K1;
            E = D;
            D = C;
            C = S30(B);
            B = A;
            A = TEMP;
        }
        for (; t < 60; t++) {
            TEMP = S5(A) + f2(B, C, D) + E + W[t] + SHA_K2;
            E = D;
            D = C;
            C = S30(B);
            B = A;
            A = TEMP;
        }
        for (; t < 80; t++) {
            TEMP = S5(A) + f3(B, C, D) + E + W[t] + SHA_K3;
            E = D;
            D = C;
            C = S30(B);
            B = A;
            A = TEMP;
        }

        ctx->H[0] += A;
        ctx->H[1] += B;
        ctx->H[2] += C;
        ctx->H[3] += D;
        ctx->H[4] += E;
    }

    debug_print0(srtp_mod_sha1, "(final) running srtp_sha1_core()");

    if (ctx->octets_in_buffer >= 56) {
        debug_print0(srtp_mod_sha1, "(final) running srtp_sha1_core() again");

        /* we need to do one final run of the compression algo */

        /*
         * set initial part of word array to zeros, and set the
         * final part to the number of bits in the message
         */
        for (i = 0; i < 15; i++) {
            W[i] = 0x0;
        }
        W[15] = ctx->num_bits_in_msg;

        /* process the word array */
        for (t = 16; t < 80; t++) {
            TEMP = W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16];
            W[t] = S1(TEMP);
        }

        A = ctx->H[0];
        B = ctx->H[1];
        C = ctx->H[2];
        D = ctx->H[3];
        E = ctx->H[4];

        for (t = 0; t < 20; t++) {
            TEMP = S5(A) + f0(B, C, D) + E + W[t] + SHA_K0;
            E = D;
            D = C;
            C = S30(B);
            B = A;
            A = TEMP;
        }
        for (; t < 40; t++) {
            TEMP = S5(A) + f1(B, C, D) + E + W[t] + SHA_K1;
            E = D;
            D = C;
            C = S30(B);
            B = A;
            A = TEMP;
        }
        for (; t < 60; t++) {
            TEMP = S5(A) + f2(B, C, D) + E + W[t] + SHA_K2;
            E = D;
            D = C;
            C = S30(B);
            B = A;
            A = TEMP;
        }
        for (; t < 80; t++) {
            TEMP = S5(A) + f3(B, C, D) + E + W[t] + SHA_K3;
            E = D;
            D = C;
            C = S30(B);
            B = A;
            A = TEMP;
        }

        ctx->H[0] += A;
        ctx->H[1] += B;
        ctx->H[2] += C;
        ctx->H[3] += D;
        ctx->H[4] += E;
    }

    /* copy result into output buffer */
    output[0] = be32_to_cpu(ctx->H[0]);
    output[1] = be32_to_cpu(ctx->H[1]);
    output[2] = be32_to_cpu(ctx->H[2]);
    output[3] = be32_to_cpu(ctx->H[3]);
    output[4] = be32_to_cpu(ctx->H[4]);

    /* indicate that message buffer in context is empty */
    ctx->octets_in_buffer = 0;

    return;
}
