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
        GnuTLS/Nettle CRYSPR/4SRT (CRYypto Service PRovider for SRT)

*****************************************************************************/

#ifndef CRYSPR_GNUTLS_H
#define CRYSPR_GNUTLS_H

#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>  //gnutls_rnd()

#include <nettle/aes.h>     //has AES cipher
#include <nettle/ctr.h>     //has CTR cipher mode
#include <nettle/pbkdf2.h>  //has Password-based Key Derivation Function 2
//#include <nettle/sha1.h>  //No need for sha1 since we have pbkdf2


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

/*
#define CRYSPR_AESCTX to the CRYSPR specifix AES key context object.
This type reserves room in the CRYPSPR control block for Haicrypt KEK and SEK
It is set from hte keystring through CRYSPR_methods.aes_set_key and passed
to CRYSPR_methods.aes_XXX.
*/
typedef struct aes_ctx CRYSPR_AESCTX;   /* CRYpto Service PRovider AES key context */

struct tag_CRYSPR_methods *crysprGnuTLS(void);

#endif /* CRYSPR_GNUTLS_H */

