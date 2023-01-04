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

   2022-05-19 (jdube)
        CRYSPR2 adaptation
   2019-06-27 (jdube)
        GnuTLS/Nettle CRYSPR/4SRT (CRYypto Service PRovider for SRT)
*****************************************************************************/

#include "hcrypt.h"

#include <string.h>

typedef struct tag_crysprGnuTLS_AES_cb {
        CRYSPR_cb       ccb;        /* CRYSPR control block */
        /* Add other cryptolib specific data here */
#ifdef CRYSPR2
        CRYSPR_AESCTX   aes_kek_buf;		/* Key Encrypting Key (KEK) */
        CRYSPR_AESCTX   aes_sek_buf[2];		/* even/odd Stream Encrypting Key (SEK) */
#endif
} crysprGnuTLS_cb;


int crysprGnuTLS_Prng(unsigned char *rn, int len)
{
    return(gnutls_rnd(GNUTLS_RND_KEY,(rn),(len)) < 0 ? -1 : 0);
}

int crysprGnuTLS_AES_SetKey(
    int cipher_type,            /* One of HCRYPT_CTX_MODE_[CLRTXT|AESECB|AESCTR] */
    bool bEncrypt,              /* true:encrypt key, false:decrypt key*/
    const unsigned char *kstr,  /* key string */
    size_t kstr_len,            /* kstr length in  bytes (16, 24, or 32 bytes (for AES128,AES192, or AES256) */
    CRYSPR_AESCTX *aes_key)     /* Cryptolib Specific AES key context */
{
    (void)cipher_type;

    if (bEncrypt) {        /* Encrypt key */
        if (!(kstr_len == 16 || kstr_len == 24 || kstr_len == 32)) {
            HCRYPT_LOG(LOG_ERR, "%s", "AES_set_encrypt_key(kek) bad length\n");
          return -1;
        }
        aes_set_encrypt_key (aes_key, kstr_len, kstr);
    } else {               /* Decrypt key */
        if (!(kstr_len == 16 || kstr_len == 24 || kstr_len == 32)) {
            HCRYPT_LOG(LOG_ERR, "%s", "AES_set_decrypt_key(kek) bad length\n");
          return -1;
        }
        aes_set_decrypt_key (aes_key, kstr_len, kstr);
    }
    return(0);
}

int crysprGnuTLS_AES_EcbCipher( /* AES Electronic Codebook cipher*/
    bool bEncrypt,              /* true:encrypt, false:decrypt */
    CRYSPR_AESCTX *aes_key,     /* CryptoLib AES context */
    const unsigned char *indata,/* src (clear text)*/
    size_t inlen,               /* length */
    unsigned char *out_txt,     /* dst (cipher text) */
    size_t *outlen)             /* dst len */
{
    int nblk = inlen/CRYSPR_AESBLKSZ;
    int nmore = inlen%CRYSPR_AESBLKSZ;
    int i;

    if (bEncrypt) {
        /* Encrypt packet payload, block by block, in output buffer */
        for (i=0; i<nblk; i++){
            aes_encrypt(aes_key, CRYSPR_AESBLKSZ, &out_txt[(i*CRYSPR_AESBLKSZ)], &indata[(i*CRYSPR_AESBLKSZ)]);
        }
        /* Encrypt last incomplete block */
        if (0 < nmore) {
            unsigned char intxt[CRYSPR_AESBLKSZ];

            memcpy(intxt, &indata[(nblk*CRYSPR_AESBLKSZ)], nmore);
            memset(intxt+nmore, 0, CRYSPR_AESBLKSZ-nmore);
            aes_encrypt(aes_key, CRYSPR_AESBLKSZ, &out_txt[(nblk*CRYSPR_AESBLKSZ)], intxt);
            nblk++;
        }
        if (outlen != NULL) *outlen = nblk*CRYSPR_AESBLKSZ;
    } else { /* Decrypt */
        for (i=0; i<nblk; i++){
            aes_decrypt(aes_key, CRYSPR_AESBLKSZ, &out_txt[(i*CRYSPR_AESBLKSZ)], &indata[(i*CRYSPR_AESBLKSZ)]);
        }
        /* Encrypt last incomplete block */
        if (0 < nmore) {
            //shall not happens in decrypt
        }
        if (outlen != NULL) *outlen = nblk*CRYSPR_AESBLKSZ;
    }
    return 0;
}

int crysprGnuTLS_AES_CtrCipher( /* AES-CTR128 Encryption */
    bool bEncrypt,              /* true:encrypt, false:decrypt */
    CRYSPR_AESCTX *aes_key,     /* CryptoLib AES context */
    unsigned char *iv,          /* iv */
    const unsigned char *indata,/* src */
    size_t inlen,               /* src length */
    unsigned char *out_txt)     /* dest buffer[inlen] */
{
    (void)bEncrypt;             /* CTR mode encrypt for both encryption and decryption */

    ctr_crypt (aes_key,         /* ctx */
               (nettle_cipher_func*)aes_encrypt, /* nettle_cipher_func */
               CRYSPR_AESBLKSZ,  /* cipher blocksize */
               iv,              /* iv */
               inlen,           /* length */
               out_txt,         /* dest */
               indata);         /* src */
    return 0;
}

#ifdef CRYSPR2
static CRYSPR_cb *crysprGnuTLS_Open(CRYSPR_methods *cryspr, size_t max_len)
{
    crysprGnuTLS_cb *aes_data;
    CRYSPR_cb *cryspr_cb;

    aes_data = (crysprGnuTLS_cb *)crysprHelper_Open(cryspr, sizeof(crysprGnuTLS_cb), max_len);
    if (NULL == aes_data) {
        HCRYPT_LOG(LOG_ERR, "crysprHelper_Open(%p, %zd, %zd) failed\n", cryspr, sizeof(crysprGnuTLS_cb), max_len);
        return(NULL);
    }

    aes_data->ccb.aes_kek = &aes_data->aes_kek_buf; //key encrypting key
    aes_data->ccb.aes_sek[0] = &aes_data->aes_sek_buf[0]; //stream encrypting key
    aes_data->ccb.aes_sek[1] = &aes_data->aes_sek_buf[1]; //stream encrypting key

    return(&aes_data->ccb);
}

static int crysprGnuTLS_Close(CRYSPR_cb *cryspr_cb)
{
    return(crysprHelper_Close(cryspr_cb));
}
#endif /* CRYSPR2 */

#ifdef CRYSPR_HAS_PBKDF2
/*
* Password-based Key Derivation Function
*/
int crysprGnuTLS_KmPbkdf2(
    CRYSPR_cb *cryspr_cb,
    char *passwd,           /* passphrase */
    size_t passwd_len,      /* passphrase len */
    unsigned char *salt,    /* salt */
    size_t salt_len,        /* salt_len */
    int itr,                /* iterations */
    size_t key_len,         /* key_len */
    unsigned char *out)     /* derived key buffer[key_len]*/
{
    (void)cryspr_cb;

    pbkdf2_hmac_sha1(passwd_len,(const uint8_t *)passwd,itr,salt_len,salt,key_len,out);
    return(0);
}
#endif /* CRYSPR_HAS_PBKDF2 */

static CRYSPR_methods crysprGnuTLS_methods;

CRYSPR_methods *crysprGnuTLS(void)
{
    if(NULL == crysprGnuTLS_methods.open) {
        crysprInit(&crysprGnuTLS_methods); /* Set default methods */

        /* CryptoLib Primitive API */
        crysprGnuTLS_methods.prng           = crysprGnuTLS_Prng;
        crysprGnuTLS_methods.aes_set_key    = crysprGnuTLS_AES_SetKey;
    #if CRYSPR_HAS_AESCTR
        crysprGnuTLS_methods.aes_ctr_cipher = crysprGnuTLS_AES_CtrCipher;
    #endif
    #if !(CRYSPR_HAS_AESCTR && CRYSPR_HAS_AESKWRAP)
        /* AES-ECB only required if cryspr has no AES-CTR or no AES KeyWrap */
        crysprGnuTLS_methods.aes_ecb_cipher = crysprGnuTLS_AES_EcbCipher;
    #endif
    #if !CRYSPR_HAS_PBKDF2
        crysprGnuTLS_methods.sha1_msg_digest= crysprGnuTLS_SHA1_MsgDigest; //Onl required if using generic KmPbkdf2
    #endif

    //--Crypto Session (Top API)
    #ifdef CRYSPR2
        crysprGnuTLS_methods.open     = crysprGnuTLS_Open;
        crysprGnuTLS_methods.close    = crysprGnuTLS_Close;
    #else /* CRYSPR2 */
    //  crysprGnuTLS_methods.open     =
    //  crysprGnuTLS_methods.close    =
    #endif /* CRYSPR2 */
    //--Keying material (km) encryption
#if CRYSPR_HAS_PBKDF2
    	crysprGnuTLS_methods.km_pbkdf2  = crysprGnuTLS_KmPbkdf2;
#endif
    //	crysprGnuTLS_methods.km_setkey  =
    //  crysprGnuTLS_methods.km_wrap    =
    //  crysprGnuTLS_methods.km_unwrap  =
    //--Media stream (ms) encryption
    //  crysprGnuTLS_methods.ms_setkey  =
    //	crysprGnuTLS_methods.ms_encrypt =
    //	crysprGnuTLS_methods.ms_decrypt =
    }
    return(&crysprGnuTLS_methods);
}



