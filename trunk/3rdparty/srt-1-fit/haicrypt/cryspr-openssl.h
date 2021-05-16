/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */


/*****************************************************************************
written by
   Haivision Systems Inc.

   2019-06-26 (jdube)
        OpenSSL Direct AES CRYSPR/4SRT (CRYypto Service PRovider for SRT).
*****************************************************************************/

#ifndef CRYSPR_OPENSSL_H
#define CRYSPR_OPENSSL_H

#include <openssl/evp.h>	/* PKCS5_xxx() */
#include <openssl/aes.h>	/* AES_xxx() */
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(OPENSSL_IS_BORINGSSL))
# include <openssl/modes.h>	/* CRYPTO_xxx() */
#endif
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/opensslv.h> /* OPENSSL_VERSION_NUMBER */

/* Define CRYSPR_HAS_AESCTR to 1 if this CRYSPR has AESCTR cipher mode
   if not set it 0 to use enable CTR cipher mode implementation using ECB cipher mode
   and provide the aes_ecb_cipher method.
*/
#define CRYSPR_HAS_AESCTR 1

/* Define CRYSPR_HAS_AESKWRAP to 1 if this CRYSPR has AES Key Wrap
   if not set to 0 to enable default/fallback crysprFallback_AES_WrapKey/crysprFallback_AES_UnwrapKey methods
   and provide the aes_ecb_cipher method  .
*/
#if (OPENSSL_VERSION_NUMBER < 0x0090808fL) //0.9.8h
#define CRYSPR_HAS_AESKWRAP 0
#else
#define CRYSPR_HAS_AESKWRAP 1
#endif

/* Define CRYSPR_HAS_PBKDF2 to 1 if this CRYSPR has SHA1-HMAC Password-based Key Derivaion Function 2
   if not set to 0 to enable not-yet-implemented/fallback crysprFallback.km_pbkdf2 method
   and provide the sha1_msg_digest method.
*/
#define CRYSPR_HAS_PBKDF2 1             /* Define to 1 if CRYSPR has Password-based Key Derivaion Function 2 */

/*
#define CRYSPR_AESCTX to the CRYSPR specifix AES key context object.
This type reserves room in the CRYPSPR control block for Haicrypt KEK and SEK
It is set from hte keystring through CRYSPR_methods.aes_set_key and passed
to CRYSPR_methods.aes_*.
*/
typedef AES_KEY CRYSPR_AESCTX;          /* CRYpto Service PRovider AES key context */

struct tag_CRYSPR_methods *crysprOpenSSL(void);

#endif /* CRYSPR_OPENSSL_H */

