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
   2014-03-26 (jsantiago)
        OS-X Build.
   2014-03-27 (jdube)
        Remove dependency on internal Crypto API.
   2016-07-22 (jsantiago)
        MINGW-W64 Build.
*****************************************************************************/

#ifndef HCRYPT_H
#define HCRYPT_H

#include <sys/types.h>

#ifdef _WIN32
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #if defined(_MSC_VER)
      #pragma warning(disable:4267)
      #pragma warning(disable:4018)
   #endif
#else
   #include <sys/time.h>
#endif

#ifdef __GNUC__
#define ATR_UNUSED __attribute__((unused))
#else
#define ATR_UNUSED
#endif

#include "haicrypt.h"
#include "hcrypt_msg.h"
#include "hcrypt_ctx.h"
#include "cryspr.h"

//#define HCRYPT_DEV 1  /* Development: should not be defined in committed code */

#ifdef HAICRYPT_SUPPORT_CRYPTO_API
/* See CRYPTOFEC_OBJECT in session structure */
#define CRYPTO_API_SERVER 1 /* Enable handler's structures */
#include "crypto_api.h"
#endif /* HAICRYPT_SUPPORT_CRYPTO_API */

typedef struct hcrypt_Session_str {
#ifdef HAICRYPT_SUPPORT_CRYPTO_API
        /* 
         * Resv matches internal upper layer handle (crypto_api)
         * They are not used in HaiCrypt.
         * This make 3 layers using the same handle.
         * To get rid of this dependency for a portable HaiCrypt,
         * revise caller (crypto_hc.c) to allocate its own buffer.
         */
        CRYPTOFEC_OBJECT    resv;           /* See above comment */
#endif /* HAICRYPT_SUPPORT_CRYPTO_API */

        hcrypt_Ctx          ctx_pair[2];    /* Even(0)/Odd(1) crypto contexts */
        hcrypt_Ctx *        ctx;            /* Current context */

        CRYSPR_methods *    cryspr;
        CRYSPR_cb *         cryspr_cb;

        unsigned char *     inbuf;          /* allocated if cipher has no getinbuf() func */
        size_t              inbuf_siz;

        int                 se;             /* Stream Encapsulation (HCRYPT_SE_xxx) */
        hcrypt_MsgInfo *    msg_info;

        struct {
            size_t          data_max_len;
        }cfg;

        struct {
            struct timeval  tx_period;      /* Keying Material tx period (milliseconds) */  
            struct timeval  tx_last;        /* Keying Material last tx time */
            unsigned int    refresh_rate;   /* SEK use period */
            unsigned int    pre_announce;   /* Pre/Post next/old SEK announce */
        }km;
} hcrypt_Session;

#if ENABLE_HAICRYPT_LOGGING
#include "haicrypt_log.h"
#else

#define HCRYPT_LOG_INIT()
#define HCRYPT_LOG_EXIT()
#define HCRYPT_LOG(lvl, fmt, ...)

#endif

#ifdef  HCRYPT_DEV
#define HCRYPT_PRINTKEY(key, len, tag) HCRYPT_LOG(LOG_DEBUG, \
            "%s[%d]=0x%02x%02x..%02x%02x\n", tag, len, \
            (key)[0], (key)[1], (key)[(len)-2], (key)[(len)-1])
#else   /* HCRYPT_DEV */
#define HCRYPT_PRINTKEY(key,len,tag)
#endif  /* HCRYPT_DEV */

#ifndef ASSERT
#include <assert.h>
#define ASSERT(c)   assert(c)
#endif


/* HaiCrypt-TP CTR mode IV (128-bit):
 *    0   1   2   3   4   5  6   7   8   9   10  11  12  13  14  15
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * |                   0s                  |      pki      |  ctr  |
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *                            XOR                         
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * |                         nonce                         +
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *
 * pki    (32-bit): packet index
 * ctr    (16-bit): block counter
 * nonce (112-bit): number used once (salt)
 */
#define hcrypt_SetCtrIV(pki, nonce, iv) do { \
            memset(&(iv)[0], 0, 128/8); \
            memcpy(&(iv)[10], (pki), HCRYPT_PKI_SZ); \
            hcrypt_XorStream(&(iv)[0], (nonce), 112/8); \
        } while(0)

#define hcrypt_XorStream(dst, strm, len) do { \
            int __XORSTREAMi; \
            for (__XORSTREAMi = 0 \
                ;__XORSTREAMi < (int)(len) \
                ;__XORSTREAMi += 1) { \
                (dst)[__XORSTREAMi] ^= (strm)[__XORSTREAMi]; \
            } \
        } while(0)


int hcryptCtx_SetSecret(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const HaiCrypt_Secret *secret);
int hcryptCtx_GenSecret(hcrypt_Session *crypto, hcrypt_Ctx *ctx);

int hcryptCtx_Tx_Init(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const HaiCrypt_Cfg *cfg);
int hcryptCtx_Tx_Rekey(hcrypt_Session *crypto, hcrypt_Ctx *ctx);
int hcryptCtx_Tx_CloneKey(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const hcrypt_Session* cryptoSrc);
int hcryptCtx_Tx_Refresh(hcrypt_Session *crypto);
int hcryptCtx_Tx_PreSwitch(hcrypt_Session *crypto);
int hcryptCtx_Tx_Switch(hcrypt_Session *crypto);
int hcryptCtx_Tx_PostSwitch(hcrypt_Session *crypto);
int hcryptCtx_Tx_AsmKM(hcrypt_Session *crypto, hcrypt_Ctx *ctx, unsigned char *alt_sek);
int hcryptCtx_Tx_ManageKM(hcrypt_Session *crypto);
int hcryptCtx_Tx_InjectKM(hcrypt_Session *crypto, void *out_p[], size_t out_len_p[], int maxout);

int hcryptCtx_Rx_Init(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const HaiCrypt_Cfg *cfg);
int hcryptCtx_Rx_ParseKM(hcrypt_Session *crypto, unsigned char *msg, size_t msg_len);

#endif /* HCRYPT_H */
