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

#ifndef HAICRYPT_H
#define HAICRYPT_H

#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// setup exports
#if defined _WIN32 && !defined __MINGW__
#ifdef HAICRYPT_DYNAMIC
#ifdef HAICRYPT_EXPORTS
#define HAICRYPT_API __declspec(dllexport)
#else
#define HAICRYPT_API __declspec(dllimport)
#endif
#else
#define HAICRYPT_API
#endif
#else
#define HAICRYPT_API
#endif

typedef void *HaiCrypt_Cryspr;

HAICRYPT_API HaiCrypt_Cryspr HaiCryptCryspr_Get_Instance (void);     /* Return a default cryspr instance */

#define HAICRYPT_CIPHER_BLK_SZ      16  /* AES Block Size */

#define HAICRYPT_PWD_MAX_SZ         80  /* MAX password (for Password-based Key Derivation) */
#define HAICRYPT_KEY_MAX_SZ         32  /* MAX key */
#define HAICRYPT_SECRET_MAX_SZ      (HAICRYPT_PWD_MAX_SZ > HAICRYPT_KEY_MAX_SZ ? HAICRYPT_PWD_MAX_SZ : HAICRYPT_KEY_MAX_SZ)


#define HAICRYPT_SALT_SZ            16

#define HAICRYPT_WRAPKEY_SIGN_SZ    8       /* RFC3394 AES KeyWrap signature size */

#define HAICRYPT_PBKDF2_SALT_LEN    8       /* PKCS#5 PBKDF2 Password based key derivation salt length */
#define HAICRYPT_PBKDF2_ITER_CNT    2048    /* PKCS#5 PBKDF2 Password based key derivation iteration count */

#define HAICRYPT_TS_PKT_SZ          188     /* Transport Stream packet size */

typedef struct {
#define HAICRYPT_SECTYP_UNDEF       0
#define HAICRYPT_SECTYP_PRESHARED   1       /* Preshared KEK */
#define HAICRYPT_SECTYP_PASSPHRASE  2       /* Password */
        unsigned        typ;
        size_t          len;
        unsigned char   str[HAICRYPT_SECRET_MAX_SZ];
}HaiCrypt_Secret;

typedef struct {
#define HAICRYPT_CFG_F_TX       0x01        /* !TX -> RX */
#define HAICRYPT_CFG_F_CRYPTO   0x02        /* Perform crypto Tx:Encrypt Rx:Decrypt */
#define HAICRYPT_CFG_F_FEC      0x04        /* Do Forward Error Correction */
        unsigned        flags;

        HaiCrypt_Secret secret;             /* Security Association */

        HaiCrypt_Cryspr cryspr;             /* CRYSPR implementation */
#define HAICRYPT_DEF_KEY_LENGTH 16          /* default key length (bytes) */
        size_t  key_len;                    /* SEK length (bytes) */
#define HAICRYPT_DEF_DATA_MAX_LENGTH 1500   /* default packet data length (bytes) */
        size_t  data_max_len;               /* Maximum data_len passed to HaiCrypt (bytes) */

#define HAICRYPT_XPT_STANDALONE 0
#define HAICRYPT_XPT_SRT        1
        int             xport;

#define HAICRYPT_DEF_KM_TX_PERIOD 1000          /* Keying Material Default Tx Period (msec) */
        unsigned int    km_tx_period_ms;        /* Keying Material Tx period (msec) */
#define HAICRYPT_DEF_KM_REFRESH_RATE 0x1000000  /* Keying Material Default Refresh Rate (pkts) */
        unsigned int    km_refresh_rate_pkt;    /* Keying Material Refresh Rate (pkts) */
#define HAICRYPT_DEF_KM_PRE_ANNOUNCE 0x1000     /* Keying Material Default Pre/Post Announce (pkts) */
        unsigned int    km_pre_announce_pkt;    /* Keying Material Pre/Post Announce (pkts) */
}HaiCrypt_Cfg;

typedef enum HaiCrypt_CryptoDir { HAICRYPT_CRYPTO_DIR_RX, HAICRYPT_CRYPTO_DIR_TX } HaiCrypt_CryptoDir;

//typedef void *HaiCrypt_Handle;
// internally it will be correctly interpreted,
// for the outsider it's just some kinda incomplete type
// but still if you use any kinda pointer instead, you'll get complaints
typedef struct hcrypt_Session_str* HaiCrypt_Handle;



HAICRYPT_API int  HaiCrypt_SetLogLevel(int level, int logfa);

HAICRYPT_API int  HaiCrypt_Create(const HaiCrypt_Cfg *cfg, HaiCrypt_Handle *phhc);
HAICRYPT_API int  HaiCrypt_Clone(HaiCrypt_Handle hhcSrc, HaiCrypt_CryptoDir tx, HaiCrypt_Handle *phhc);
HAICRYPT_API int  HaiCrypt_Close(HaiCrypt_Handle hhc);
HAICRYPT_API int  HaiCrypt_Tx_GetBuf(HaiCrypt_Handle hhc, size_t data_len, unsigned char **in_p);
HAICRYPT_API int  HaiCrypt_Tx_Process(HaiCrypt_Handle hhc, unsigned char *in, size_t in_len,
        void *out_p[], size_t out_len_p[], int maxout);
HAICRYPT_API int  HaiCrypt_Rx_Process(HaiCrypt_Handle hhc, unsigned char *in, size_t in_len,
        void *out_p[], size_t out_len_p[], int maxout);

HAICRYPT_API int  HaiCrypt_Tx_GetKeyFlags(HaiCrypt_Handle hhc);
HAICRYPT_API int  HaiCrypt_Tx_ManageKeys(HaiCrypt_Handle hhc, void *out_p[], size_t out_len_p[], int maxout);
HAICRYPT_API int  HaiCrypt_Tx_Data(HaiCrypt_Handle hhc, unsigned char *pfx, unsigned char *data, size_t data_len);
HAICRYPT_API int  HaiCrypt_Rx_Data(HaiCrypt_Handle hhc, unsigned char *pfx, unsigned char *data, size_t data_len);

/* Status values */

#define HAICRYPT_ERROR -1
#define HAICRYPT_ERROR_WRONG_SECRET -2
#define HAICRYPT_OK 0


#ifdef __cplusplus
}
#endif

#endif /* HAICRYPT_H */
