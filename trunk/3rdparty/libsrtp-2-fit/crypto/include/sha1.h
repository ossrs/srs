/*
 * sha1.h
 *
 * interface to the Secure Hash Algorithm v.1 (SHA-1), specified in
 * FIPS 180-1
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

#ifndef SHA1_H
#define SHA1_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "err.h"
#ifdef OPENSSL
#include <openssl/evp.h>
#include <stdint.h>
#else
#include "datatypes.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef OPENSSL

/*
 * srtp_sha1_init(&ctx) initializes the SHA1 context ctx
 *
 * srtp_sha1_update(&ctx, msg, len) hashes the len octets starting at msg
 * into the SHA1 context
 *
 * srtp_sha1_final(&ctx, output) performs the final processing of the SHA1
 * context and writes the result to the 20 octets at output
 *
 * Return values are ignored on the EVP functions since all three
 * of these functions return void.
 *
 */

/* OpenSSL 1.1.0 made EVP_MD_CTX an opaque structure, which must be allocated
   using EVP_MD_CTX_new. But this function doesn't exist in OpenSSL 1.0.x. */
#if OPENSSL_VERSION_NUMBER < 0x10100000L || LIBRESSL_VERSION_NUMBER

typedef EVP_MD_CTX srtp_sha1_ctx_t;

static inline void srtp_sha1_init(srtp_sha1_ctx_t *ctx)
{
    EVP_MD_CTX_init(ctx);
    EVP_DigestInit(ctx, EVP_sha1());
}

static inline void srtp_sha1_update(srtp_sha1_ctx_t *ctx,
                                    const uint8_t *M,
                                    int octets_in_msg)
{
    EVP_DigestUpdate(ctx, M, octets_in_msg);
}

static inline void srtp_sha1_final(srtp_sha1_ctx_t *ctx, uint32_t *output)
{
    unsigned int len = 0;

    EVP_DigestFinal(ctx, (unsigned char *)output, &len);
    EVP_MD_CTX_cleanup(ctx);
}

#else

typedef EVP_MD_CTX *srtp_sha1_ctx_t;

static inline void srtp_sha1_init(srtp_sha1_ctx_t *ctx)
{
    *ctx = EVP_MD_CTX_new();
    EVP_DigestInit(*ctx, EVP_sha1());
}

static inline void srtp_sha1_update(srtp_sha1_ctx_t *ctx,
                                    const uint8_t *M,
                                    int octets_in_msg)
{
    EVP_DigestUpdate(*ctx, M, octets_in_msg);
}

static inline void srtp_sha1_final(srtp_sha1_ctx_t *ctx, uint32_t *output)
{
    unsigned int len = 0;

    EVP_DigestFinal(*ctx, (unsigned char *)output, &len);
    EVP_MD_CTX_free(*ctx);
}
#endif

#else

typedef struct {
    uint32_t H[5];            /* state vector                    */
    uint32_t M[16];           /* message buffer                  */
    int octets_in_buffer;     /* octets of message in buffer     */
    uint32_t num_bits_in_msg; /* total number of bits in message */
} srtp_sha1_ctx_t;

/*
 * srtp_sha1_init(&ctx) initializes the SHA1 context ctx
 *
 * srtp_sha1_update(&ctx, msg, len) hashes the len octets starting at msg
 * into the SHA1 context
 *
 * srtp_sha1_final(&ctx, output) performs the final processing of the SHA1
 * context and writes the result to the 20 octets at output
 *
 */
void srtp_sha1_init(srtp_sha1_ctx_t *ctx);

void srtp_sha1_update(srtp_sha1_ctx_t *ctx,
                      const uint8_t *M,
                      int octets_in_msg);

void srtp_sha1_final(srtp_sha1_ctx_t *ctx, uint32_t output[5]);

/*
 * The srtp_sha1_core function is INTERNAL to SHA-1, but it is declared
 * here because it is also used by the cipher SEAL 3.0 in its key
 * setup algorithm.
 */

/*
 *  srtp_sha1_core(M, H) computes the core sha1 compression function, where M is
 *  the next part of the message and H is the intermediate state {H0,
 *  H1, ...}
 *
 *  this function does not do any of the padding required in the
 *  complete sha1 function
 */
void srtp_sha1_core(const uint32_t M[16], uint32_t hash_value[5]);

#endif /* else OPENSSL */

#ifdef __cplusplus
}
#endif

#endif /* SHA1_H */
