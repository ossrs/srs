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

   2019-06-27 (jdube)
        MBedTLS CRYSPR/4SRT (CRYypto Service PRovider for SRT)

*****************************************************************************/

#ifndef CRYSPR_MBEDTLS_H
#define CRYSPR_MBEDTLS_H

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/aes.h>


/* Define CRYSPR_HAS_AESCTR to 1 if this CRYSPR has AESCTR cipher mode
   if not set it 0 to use enable CTR cipher mode implementation using ECB cipher mode
   and provide the aes_ecb_cipher method.
*/
#define CRYSPR_HAS_AESCTR 1

/* Define CRYSPR_HAS_AESKWRAP to 1 if this CRYSPR has AES Key Wrap
   if not set to 0 to enable default/fallback crysprFallback_AES_WrapKey/crysprFallback_AES_UnwrapKey methods
   and provide the aes_ecb_cipher method  .
*/
#define CRYSPR_HAS_AESKWRAP 0

/* Define CRYSPR_HAS_PBKDF2 to 1 if this CRYSPR has SHA1-HMAC Password-based Key Derivaion Function 2
   if not set to 0 to enable not-yet-implemented/fallback crysprFallback.km_pbkdf2 method
   and provide the sha1_msg_digest method.
*/
#define CRYSPR_HAS_PBKDF2 1

// mbedtls uses in the enc/dec functions 16-byte blocks
// for xcryption. This is not marked by any constant. See
// e.g. <mbedtls/aes.h>, mbedtls_aes_crypt_ecb signature.
#if CRYSPR_AESBLKSZ != 16
#error mbedtls requires AES single block size 16 bytes, implicitly.
#endif

/*
#define CRYSPR_AESCTX to the CRYSPR specifix AES key context object.
This type reserves room in the CRYPSPR control block for Haicrypt KEK and SEK
It is set from the keystring through CRYSPR_methods.aes_set_key and passed
to CRYSPR_methods.aes_XXX.
*/
typedef mbedtls_aes_context CRYSPR_AESCTX;   /* CRYpto Service PRovider AES key context */

struct tag_CRYSPR_methods *crysprMbedtls(void);

#endif /* CRYSPR_MBEDTLS_H */

