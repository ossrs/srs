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

   2019-06-28 (jdube)
        CRYSPR/4SRT Initial implementation.
*****************************************************************************/

#ifndef CRYSPR_H
#define CRYSPR_H

#include <stdbool.h>
#include <sys/types.h>

#if !defined(HAISRT_VERSION_INT)
#include "haicrypt.h"
#include "hcrypt_msg.h"
#else
// Included by haisrt.h or similar
#include "haisrt/haicrypt.h"
#include "haisrt/hcrypt_msg.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "cryspr-config.h"

typedef struct tag_CRYSPR_cb {
#ifdef CRYSPR2
	CRYSPR_AESCTX  *aes_kek;		/* Key Encrypting Key (KEK) */
	CRYSPR_AESCTX  *aes_sek[2];		/* even/odd Stream Encrypting Key (SEK) */
#define CRYSPR_GETKEK(cb)       ((cb)->aes_kek)
#define CRYSPR_GETSEK(cb,kk)    ((cb)->aes_sek[kk])
#else /*CRYSPR2*/
	CRYSPR_AESCTX   aes_kek;		/* Key Encrypting Key (KEK) */
	CRYSPR_AESCTX   aes_sek[2];		/* even/odd Stream Encrypting Key (SEK) */
#define CRYSPR_GETKEK(cb)       (&((cb)->aes_kek))
#define CRYSPR_GETSEK(cb,kk)    (&((cb)->aes_sek[kk]))
#endif /*CRYSPR2*/

	struct tag_CRYSPR_methods *cryspr;

#if !CRYSPR_HAS_AESCTR
                                        /* Reserve room to build the counter stream ourself */
#define HCRYPT_CTR_BLK_SZ       CRYSPR_AESBLKSZ
#define HCRYPT_CTR_STREAM_SZ	2048
	unsigned char * ctr_stream;
	size_t          ctr_stream_len; /* Content size */
	size_t          ctr_stream_siz; /* Allocated length */
#endif /* !CRYSPR_HAS_AESCTR */

#define	CRYSPR_OUTMSGMAX		6
	uint8_t *       outbuf; 		/* output circle buffer */
	size_t          outbuf_ofs;		/* write offset in circle buffer */
	size_t          outbuf_siz;		/* circle buffer size */
} CRYSPR_cb;

typedef struct tag_CRYSPR_methods {
        /*
        * prng:
        * Pseudo-Random Number Generator
        */
        int (*prng)(
            unsigned char *rn,      /* out: pseudo random number */
            int rn_len);

        int (*aes_set_key)(
            int cipher_type,        /* One of HCRYPT_CTX_MODE_[CLRTXT|AESECB|AESCTR|AESGDM] */
            bool bEncrypt,          /* true Enxcrypt key, false: decrypt */
            const unsigned char *kstr,/* key string*/
            size_t kstr_len,        /* kstr len in  bytes (16, 24, or 32 bytes (for AES128,AES192, or AES256) */
            CRYSPR_AESCTX *aeskey); /* Cryptolib Specific AES key context */

        int (*aes_ecb_cipher)(
            bool bEncrypt,          /* true:encrypt false:decrypt */
            CRYSPR_AESCTX *aes_key, /* ctx */
            const unsigned char *indata,  /* src (clear text)*/
            size_t inlen,           /* src length */
            unsigned char *out_txt, /* dst (cipher text) */
            size_t *outlen);        /* dst length */

        int (*aes_ctr_cipher)(
            bool bEncrypt,          /* true:encrypt false:decrypt (don't care with CTR) */
            CRYSPR_AESCTX *aes_key, /* ctx */
            unsigned char *iv,      /* iv */
            const unsigned char *indata,  /* src (clear text) */
            size_t inlen,           /* src length */
            unsigned char *out_txt);/* dest */

        unsigned char *(*sha1_msg_digest)(
            const unsigned char *m, /* in: message */
            size_t m_len,           /* message length */
            unsigned char *md);     /* out: message digest buffer *160 bytes */

        /*
        * open:
        * Create a cipher instance
        * Allocate output buffers
        */
        CRYSPR_cb *(*open)(
            struct tag_CRYSPR_methods *cryspr,
            size_t max_len);                                /* Maximum packet length that will be encrypted/decrypted */

        /*
        * close:
        * Release any cipher resources
        */
        int     (*close)(
            CRYSPR_cb *cryspr_data);                /* Cipher handle, internal data */

        /*
        * pbkdf2_hmac_sha1
        * Password-based Key Derivation Function 2
        */
        int (*km_pbkdf2)(
            CRYSPR_cb *cryspr_cb,   /* Cryspr Control Block */
            char *passwd,           /* passphrase */
            size_t passwd_len,      /* passphrase len */
            unsigned char *salt,    /* salt */
            size_t salt_len,        /* salt_len */
            int itr,                /* iterations */
            size_t out_len,         /* key_len */
            unsigned char *out);    /* derived key */

        /*
        * km_setkey:
        * Set the Key Encypting Key for Wrap (Encryption) or UnWrap (Decryption).
        * Context (ctx) tells if it's for Wrap or Unwrap
        * A Context flags (ctx->flags) also tells if this is for wrap(encryption) or unwrap(decryption) context (HCRYPT_CTX_F_ENCRYPT)
        */
        int (*km_setkey)(
            CRYSPR_cb *cryspr_cb,                       /* Cryspr Control Block */
            bool bWrap,                                 /* True: Wrap KEK, False: Unwrap KEK */
            const unsigned char *kek, size_t kek_len);  /* KEK: Key Encrypting Key */

        /*
        * km_wrap:
        * wrap media stream key
        */
        int (*km_wrap)(CRYSPR_cb *cryspr_cb,
            unsigned char *wrap,
            const unsigned char *sek,
            unsigned int seklen);

        /*
        * km_unwrap:
        * wrap media stream key
        */
        int (*km_unwrap)(CRYSPR_cb *cryspr_cb,
            unsigned char *sek,
            const unsigned char *wrap,
            unsigned int wraplen);
        /*
        * setkey:
        * Set the Odd or Even, Encryption or Decryption key.
        * Context (ctx) tells if it's for Odd or Even key (hcryptCtx_GetKeyIndex(ctx))
        * A Context flags (ctx->flags) also tells if this is an encryption or decryption context (HCRYPT_CTX_F_ENCRYPT)
        */
        int (*ms_setkey)(
            CRYSPR_cb *cryspr_cb,                           /* Cryspr Control Block */
            hcrypt_Ctx *ctx,                                /* HaiCrypt Context (cipher, keys, Odd/Even, etc..) */
            const unsigned char *key, size_t kwelen);       /* New Key */

        /*
        * encrypt:
        * Submit a list of nbin clear transport packets (hcrypt_DataDesc *in_data) to encryption
        * returns *nbout encrypted data packets of length out_len_p[] into out_p[]
        *
        * If cipher implements deferred encryption (co-processor, async encryption),
        * it may return no encrypted packets, or encrypted packets for clear text packets of a previous call.  
        */
        int (*ms_encrypt)(
            CRYSPR_cb *cryspr_cb,                           /* Cryspr Control Block */
            hcrypt_Ctx *ctx,                                /* HaiCrypt Context (cipher, keys, Odd/Even, etc..) */
            hcrypt_DataDesc *in_data, int nbin,             /* Clear text transport packets: header and payload */
            void *out_p[], size_t out_len_p[], int *nbout); /* Encrypted packets */

        /*
        * decrypt:
        * Submit a list of nbin encrypted transport packets (hcrypt_DataDesc *in_data) to decryption
        * returns *nbout clear text data packets of length out_len_p[] into out_p[]
        *
        * If cipher implements deferred decryption (co-processor, async encryption),
        * it may return no decrypted packets, or decrypted packets for encrypted packets of a previous call.
        */
        int (*ms_decrypt)(
            CRYSPR_cb *cryspr_cb,                           /* Cryspr Control Block */
            hcrypt_Ctx *ctx,                                /* HaiCrypt Context (cipher, keys, Odd/Even, etc..) */
            hcrypt_DataDesc *in_data, int nbin,             /* Clear text transport packets: header and payload */
            void *out_p[], size_t out_len_p[], int *nbout); /* Encrypted packets */

} CRYSPR_methods;

CRYSPR_cb  *crysprHelper_Open(CRYSPR_methods *cryspr, size_t cb_len, size_t max_len);
int         crysprHelper_Close(CRYSPR_cb *cryspr_cb);

CRYSPR_methods *crysprInit(CRYSPR_methods *cryspr);

#ifdef __cplusplus
}
#endif

#endif /* CRYSPR_H */
