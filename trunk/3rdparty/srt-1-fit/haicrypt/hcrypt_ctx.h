/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */


/*****************************************************************************
written by
   Haivision Systems Inc.

   2011-06-23 (jdube)
        HaiCrypt initial implementation.
   2014-03-11 (jdube)
        Adaptation for SRT.
*****************************************************************************/

#ifndef HCRYPT_CTX_H
#define HCRYPT_CTX_H

#include <stdbool.h>
#include <sys/types.h>
#include "hcrypt.h"

#if !defined(HAISRT_VERSION_INT)
#include "haicrypt.h"
#include "hcrypt_msg.h"
#else
// Included by haisrt.h or similar
#include "haisrt/haicrypt.h"
#include "haisrt/hcrypt_msg.h"
#endif

typedef struct {
        unsigned char *pfx; //Prefix described by transport msg info (in ctx)
        unsigned char *payload;
        size_t len; //Payload size
}hcrypt_DataDesc;


typedef struct tag_hcrypt_Ctx {
        struct tag_hcrypt_Ctx * alt;    /* Alternative ctx (even/odd) */

#define HCRYPT_CTX_F_MSG        0x00FF  /* Aligned wiht message header flags */		
#define HCRYPT_CTX_F_eSEK       HCRYPT_MSG_F_eSEK
#define HCRYPT_CTX_F_oSEK       HCRYPT_MSG_F_oSEK
#define HCRYPT_CTX_F_xSEK       HCRYPT_MSG_F_xSEK

#define HCRYPT_CTX_F_ENCRYPT    0x0100  /* 0:decrypt 1:encrypt */
#define HCRYPT_CTX_F_ANNOUNCE   0x0200  /* Announce KM */
#define HCRYPT_CTX_F_TTSEND     0x0400  /* time to send */
        unsigned         flags;
#define hcryptCtx_GetKeyFlags(ctx)      ((ctx)->flags & HCRYPT_CTX_F_xSEK)
#define hcryptCtx_GetKeyIndex(ctx)      (((ctx)->flags & HCRYPT_CTX_F_xSEK)>>1)

#define HCRYPT_CTX_S_INIT       1
#define HCRYPT_CTX_S_SARDY      2   /* Security Association (KEK) ready */
#define HCRYPT_CTX_S_KEYED      3   /* Media Stream Encrypting Key (SEK) ready */
#define HCRYPT_CTX_S_ACTIVE     4   /* Announced and in use */
#define HCRYPT_CTX_S_DEPRECATED 5   /* Still announced but no longer used */
        unsigned         status;

#define HCRYPT_CTX_MODE_CLRTXT  0   /* NULL cipher (for tests) */
#define HCRYPT_CTX_MODE_AESECB  1   /* Electronic Code Book mode */
#define HCRYPT_CTX_MODE_AESCTR  2   /* Counter mode */
#define HCRYPT_CTX_MODE_AESCBC  3   /* Cipher-block chaining mode */
        unsigned         mode;

        struct {
            size_t       key_len;
            size_t       pwd_len;
            char         pwd[HAICRYPT_PWD_MAX_SZ];
        } cfg;

        size_t           salt_len;
        unsigned char    salt[HAICRYPT_SALT_SZ];

        size_t           sek_len;
        unsigned char    sek[HAICRYPT_KEY_MAX_SZ];

        hcrypt_MsgInfo * msg_info;  /* Transport message handler */
        unsigned         pkt_cnt;   /* Key usage counter */

#define HCRYPT_CTX_MAX_KM_PFX_SZ   16
        size_t           KMmsg_len;
        unsigned char    KMmsg_cache[HCRYPT_CTX_MAX_KM_PFX_SZ + HCRYPT_MSG_KM_MAX_SZ];

#define HCRYPT_CTX_MAX_MS_PFX_SZ   16
        unsigned char    MSpfx_cache[HCRYPT_CTX_MAX_MS_PFX_SZ];
} hcrypt_Ctx;


#endif /* HCRYPT_CTX_H */
