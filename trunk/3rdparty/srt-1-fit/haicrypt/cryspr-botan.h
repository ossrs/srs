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
        Botan CRYSPR/4SRT (CRYypto Service PRovider for SRT)

*****************************************************************************/

#ifndef CRYSPR_BOTAN_H
#define CRYSPR_BOTAN_H

#include <botan/ffi.h>

typedef struct botan_aes_context
{
    botan_cipher_t cipher;
    unsigned char kstr[32];
    size_t kstr_len;
} botan_aes_context;

/* Define CRYSPR_HAS_AESCTR to 1 if this CRYSPR has AES CTR cipher mode.
*/
#define CRYSPR_HAS_AESCTR 1

/* Define CRYSPR_HAS_AESGCM to 1 if this CRYSPR has AES GCM cipher mode.
*/
#define CRYSPR_HAS_AESGCM 1

/* Define CRYSPR_HAS_AESKWRAP to 1 if this CRYSPR has AES Key Wrap.
*/
#define CRYSPR_HAS_AESKWRAP 1

/* Define CRYSPR_HAS_PBKDF2 to 1 if this CRYSPR has SHA1-HMAC Password-based Key Derivaion Function 2.
*/
#define CRYSPR_HAS_PBKDF2 1

/*
#define CRYSPR_AESCTX to the CRYSPR specifix AES key context object.
This type reserves room in the CRYPSPR control block for Haicrypt KEK and SEK
It is set from the keystring through CRYSPR_methods.aes_set_key and passed
to CRYSPR_methods.aes_XXX.
*/
typedef botan_aes_context CRYSPR_AESCTX;   /* CRYpto Service PRovider AES key context */

struct tag_CRYSPR_methods *crysprBotan(void);

#endif /* CRYSPR_BOTAN_H */

