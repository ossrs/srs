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
   2019-06-26 (jdube)
        OpenSSL Direct AES CRYSPR/4SRT (CRYypto Service PRovider for SRT).
*****************************************************************************/

#include "hcrypt.h"

#include <string.h>

typedef struct tag_crysprOpenSSL_AES_cb {
    CRYSPR_cb       ccb;
    /* Add cryptolib specific data here */
#ifdef CRYSPR2
    CRYSPR_AESCTX   aes_kek_buf;		/* Key Encrypting Key (KEK) */
    CRYSPR_AESCTX   aes_sek_buf[2];		/* even/odd Stream Encrypting Key (SEK) */
#endif
} crysprOpenSSL_cb;


int crysprOpenSSL_Prng(unsigned char *rn, int len)
{
    return(RAND_bytes(rn, len) <= 0 ? -1 : 0);
}

int crysprOpenSSL_AES_SetKey(
    int cipher_type,            /* One of HCRYPT_CTX_MODE_[CLRTXT|AESECB|AESCTR] */
    bool bEncrypt,              /* true Enxcrypt key, false: decrypt */
    const unsigned char *kstr,  /* key sttring*/
    size_t kstr_len,            /* kstr len in  bytes (16, 24, or 32 bytes (for AES128,AES192, or AES256) */
    CRYSPR_AESCTX *aes_key)     /* CRYpto Service PRovider AES Key context */
{
    (void)cipher_type;

    if (bEncrypt) {        /* Encrypt key */
        if (AES_set_encrypt_key(kstr, kstr_len * 8, aes_key)) {
            HCRYPT_LOG(LOG_ERR, "%s", "AES_set_encrypt_key(kek) failed\n");
            return(-1);
        }
    } else {               /* Decrypt key */
        if (AES_set_decrypt_key(kstr, kstr_len * 8, aes_key)) {
            HCRYPT_LOG(LOG_ERR, "%s", "AES_set_decrypt_key(kek) failed\n");
            return(-1);
        }
    }
    return(0);
}

#if !(CRYSPR_HAS_AESCTR && CRYSPR_HAS_AESKWRAP)

int crysprOpenSSL_AES_EcbCipher(
    bool bEncrypt,              /* true:encrypt, false:decrypt */
    CRYSPR_AESCTX *aes_key,     /* CRYpto Service PRovider AES Key context */
    const unsigned char *indata,/* src (clear text if encrypt, cipher text otherwise)*/
    size_t inlen,               /* indata length */
    unsigned char *out_txt,     /* dst (cipher text if encrypt, clear text otherwise) */
    size_t *outlen)             /* in/out dst len */
{
    int nblk = inlen/CRYSPR_AESBLKSZ;
    int nmore = inlen%CRYSPR_AESBLKSZ;
    size_t outsiz = (outlen ? *outlen : 0);
    int i;

    if (outsiz % CRYSPR_AESBLKSZ) return(-1); /* output buf size must be a multiple of AES block size (16) */
    if (bEncrypt) {
        if (outsiz > 16 && outsiz < (nblk+nmore)*CRYSPR_AESBLKSZ) return(-1); /* output buf size must have room for PKCS7 padding */
        /* Encrypt packet payload, block by block, in output buffer */
        for (i=0; i<nblk; i++){
            AES_ecb_encrypt(&indata[(i*CRYSPR_AESBLKSZ)],
                &out_txt[(i*CRYSPR_AESBLKSZ)], aes_key, AES_ENCRYPT);
        }
        /* Encrypt last incomplete block */
        if (0 < nmore) {
            unsigned char intxt[CRYSPR_AESBLKSZ];
            /* PKCS7 padding: padding value is number of bytes padded */
            memcpy(intxt, &indata[(nblk*CRYSPR_AESBLKSZ)], nmore);
            memset(intxt+nmore, CRYSPR_AESBLKSZ-nmore, CRYSPR_AESBLKSZ-nmore);
            AES_ecb_encrypt(intxt, &out_txt[(nblk*CRYSPR_AESBLKSZ)], aes_key, AES_ENCRYPT);
            nblk++;
        }
        if (outlen != NULL) *outlen = nblk*CRYSPR_AESBLKSZ;
    } else { /* Decrypt */
        for (i=0; i<nblk; i++){
            AES_ecb_encrypt(&indata[(i*CRYSPR_AESBLKSZ)],
                &out_txt[(i*CRYSPR_AESBLKSZ)], aes_key, AES_DECRYPT);
        }
        /* Encrypt last incomplete block */
        if (0 < nmore) {
            //shall not happens in decrypt
        }
        if (outlen != NULL) *outlen = nblk*CRYSPR_AESBLKSZ;
    }
    return 0;
}
#endif /* !(CRYSPR_HAS_AESCTR && CRYSPR_HAS_AESKWRAP) */

int crysprOpenSSL_AES_CtrCipher(
    bool bEncrypt,              /* true:encrypt, false:decrypt */
    CRYSPR_AESCTX *aes_key,     /* CRYpto Service PRovider AES Key context */
    unsigned char *iv,          /* iv */
    const unsigned char *indata,/* src */
    size_t inlen,               /* length */
    unsigned char *out_txt)     /* dest */
{
    unsigned char ctr[CRYSPR_AESBLKSZ];
    unsigned blk_ofs = 0;
    (void)bEncrypt;             /* CTR mode encrypt for both encryption and decryption */

    memset(&ctr[0], 0, sizeof(ctr));
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(OPENSSL_IS_BORINGSSL))
    CRYPTO_ctr128_encrypt(indata, out_txt,
                          inlen, aes_key, iv, ctr, &blk_ofs, (block128_f) AES_encrypt);
#else
    AES_ctr128_encrypt(indata, out_txt,
                       inlen, aes_key, iv, ctr, &blk_ofs);
#endif
    return 0;
}
#ifdef CRYSPR2
static CRYSPR_cb *crysprOpenSSL_Open(CRYSPR_methods *cryspr, size_t max_len)
{
    crysprOpenSSL_cb *aes_data;

    aes_data = (crysprOpenSSL_cb *)crysprHelper_Open(cryspr, sizeof(crysprOpenSSL_cb), max_len);
    if (NULL == aes_data) {
        HCRYPT_LOG(LOG_ERR, "crysprHelper_Open(%p, %zd, %zd) failed\n", cryspr, sizeof(crysprOpenSSL_cb), max_len);
        return(NULL);
    }

    aes_data->ccb.aes_kek = &aes_data->aes_kek_buf; //key encrypting key
    aes_data->ccb.aes_sek[0] = &aes_data->aes_sek_buf[0]; //stream encrypting key
    aes_data->ccb.aes_sek[1] = &aes_data->aes_sek_buf[1]; //stream encrypting key

    return(&aes_data->ccb);
}
#endif /* CRYSPR2 */
/*
* Password-based Key Derivation Function
*/
int crysprOpenSSL_KmPbkdf2(
    CRYSPR_cb *cryspr_cb,
    char *passwd,           /* passphrase */
    size_t passwd_len,      /* passphrase len */
    unsigned char *salt,    /* salt */
    size_t salt_len,        /* salt_len */
    int itr,                /* iterations */
    size_t key_len,         /* key_len */
    unsigned char *out)     /* derived key */
{
    (void)cryspr_cb;
    int rc = PKCS5_PBKDF2_HMAC_SHA1(passwd,passwd_len,salt,salt_len,itr,key_len,out);
    return(rc == 1? 0 : -1);
}

#if CRYSPR_HAS_AESKWRAP
int crysprOpenSSL_KmWrap(CRYSPR_cb *cryspr_cb,
		unsigned char *wrap,
		const unsigned char *sek,
        unsigned int seklen)
{
    AES_KEY *kek = CRYSPR_GETKEK(cryspr_cb); //key encrypting key

    return(((seklen + HAICRYPT_WRAPKEY_SIGN_SZ) == (unsigned int)AES_wrap_key(kek, NULL, wrap, sek, seklen)) ? 0 : -1);
}

int crysprOpenSSL_KmUnwrap(
        CRYSPR_cb *cryspr_cb,
		unsigned char *sek,             //Stream encrypting key
		const unsigned char *wrap,
        unsigned int wraplen)
{
    AES_KEY *kek = CRYSPR_GETKEK(cryspr_cb); //key encrypting key

    return(((wraplen - HAICRYPT_WRAPKEY_SIGN_SZ) == (unsigned int)AES_unwrap_key(kek, NULL, sek, wrap, wraplen)) ? 0 : -1);
}
#endif /*CRYSPR_HAS_AESKWRAP*/


static CRYSPR_methods crysprOpenSSL_methods;

CRYSPR_methods *crysprOpenSSL(void)
{
    if(NULL == crysprOpenSSL_methods.open) {
        crysprInit(&crysprOpenSSL_methods);    //Default/fallback methods

        crysprOpenSSL_methods.prng           = crysprOpenSSL_Prng;
    //--CryptoLib Primitive API-----------------------------------------------
        crysprOpenSSL_methods.aes_set_key    = crysprOpenSSL_AES_SetKey;
    #if CRYSPR_HAS_AESCTR
        crysprOpenSSL_methods.aes_ctr_cipher = crysprOpenSSL_AES_CtrCipher;
    #endif
    #if !(CRYSPR_HAS_AESCTR && CRYSPR_HAS_AESKWRAP)
        /* AES-ECB only required if cryspr has no AES-CTR and no AES KeyWrap */
        /* OpenSSL has both AESCTR and AESKWRP and the AESECB wrapper is only used
           to test the falback methods */
        crysprOpenSSL_methods.aes_ecb_cipher = crysprOpenSSL_AES_EcbCipher;
    #endif
    #if !CRYSPR_HAS_PBKDF2
        crysprOpenSSL_methods.sha1_msg_digest= NULL; //Required to use eventual default/fallback KmPbkdf2
    #endif

    //--Crypto Session API-----------------------------------------
#ifdef CRYSPR2
        crysprOpenSSL_methods.open     = crysprOpenSSL_Open;
#else
    //  crysprOpenSSL_methods.open     =
#endif
    //  crysprOpenSSL_methods.close    =
    //--Keying material (km) encryption

#if CRYSPR_HAS_PBKDF2
    	crysprOpenSSL_methods.km_pbkdf2  = crysprOpenSSL_KmPbkdf2;
#else
#error  There is no default/fallback method for PBKDF2
#endif
    //	crysprOpenSSL_methods.km_setkey  =
#if CRYSPR_HAS_AESKWRAP
        crysprOpenSSL_methods.km_wrap    = crysprOpenSSL_KmWrap;
        crysprOpenSSL_methods.km_unwrap  = crysprOpenSSL_KmUnwrap;
#endif

    //--Media stream (ms) encryption
    //  crysprOpenSSL_methods.ms_setkey  =
    //	crysprOpenSSL_methods.ms_encrypt =
    //	crysprOpenSSL_methods.ms_decrypt =
    }
    return(&crysprOpenSSL_methods);
}


