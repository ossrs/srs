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

   2023-03-28 (oviano)
        Botan CRYSPR/4SRT (CRYypto Service PRovider for SRT)
*****************************************************************************/

#include "hcrypt.h"

#include <string.h>

typedef struct tag_crysprBotanTLS_AES_cb {
    CRYSPR_cb       ccb;            /* CRYSPR control block */
#ifdef CRYSPR2
    CRYSPR_AESCTX   aes_kek_buf;	/* Key Encrypting Key (KEK) */
    CRYSPR_AESCTX   aes_sek_buf[2]; /* even/odd Stream Encrypting Key (SEK) */
#endif
} crysprBotan_cb;


static const char* get_key_wrap_algo(size_t kek_len)
{
    switch (kek_len)
    {
    case 16:
        return "AES-128";
    case 24:
        return "AES-192";
    case 32:
        return "AES-256";
    }
    
    return NULL;
}

int crysprBotan_Prng(unsigned char *rn, int len)
{
    if (botan_system_rng_get(rn, len) != BOTAN_FFI_SUCCESS) {
        return -1;
    }

    return 0;
}

int crysprBotan_AES_SetKey(
        int cipher_type,            /* One of HCRYPT_CTX_MODE_[CLRTXT|AESECB|AESCTR] */
        bool bEncrypt,              /* true:encrypt key, false:decrypt key*/
        const unsigned char *kstr,  /* key string */
        size_t kstr_len,            /* kstr length in  bytes (16, 24, or 32 bytes, for AES128, AES192, or AES256) */
        CRYSPR_AESCTX *aes_key)     /* Cryptolib Specific AES key context */
{
    if (!(cipher_type == HCRYPT_CTX_MODE_AESCTR || cipher_type == HCRYPT_CTX_MODE_AESGCM)) {
        return -1;
    }
    if (!(kstr_len == 16 || kstr_len == 24 || kstr_len == 32)) {
        return -1;
    }

    const char* algo = NULL;
    if (cipher_type == HCRYPT_CTX_MODE_AESCTR) {
        if (kstr_len == 16) {
            algo = "CTR(AES-128)";
        }
        else if (kstr_len == 24) {
            algo = "CTR(AES-192)";
        }
        else/* if (kstr_len == 32)*/ {
            algo = "CTR(AES-256)";
        }
    } else if (cipher_type == HCRYPT_CTX_MODE_AESGCM) {
        if (kstr_len == 16) {
            algo = "AES-128/GCM";
        }
        else if (kstr_len == 24) {
            algo = "AES-192/GCM";
        }
        else/* if (kstr_len == 32)*/ {
            algo = "AES-256/GCM";
        }
    }

    if (botan_cipher_init(
        &aes_key->cipher,
        algo,
        bEncrypt ? BOTAN_CIPHER_INIT_FLAG_ENCRYPT : BOTAN_CIPHER_INIT_FLAG_DECRYPT) != BOTAN_FFI_SUCCESS) {
        return -1;
    }

    if (botan_cipher_set_key(
        aes_key->cipher,
        kstr,
        kstr_len) != BOTAN_FFI_SUCCESS) {
        return -1;
    }

    memcpy(aes_key->kstr, kstr, kstr_len);
    aes_key->kstr_len = kstr_len;

    return 0;
}

int crysprBotan_AES_CtrCipher(
    bool bEncrypt,                  /* true:encrypt, false:decrypt */
    CRYSPR_AESCTX *aes_key,         /* CryptoLib AES context */
    unsigned char *iv,              /* iv */
    const unsigned char *indata,    /* src */
    size_t inlen,                   /* src length */
    unsigned char *out_txt)         /* dest buffer[inlen] */
{
    (void)bEncrypt;

    if (botan_cipher_start(
        aes_key->cipher,
        iv,
        16) != BOTAN_FFI_SUCCESS) {
        return -1;
    }

    size_t out_written = 0;
    size_t in_consumed = 0;
    if (botan_cipher_update(
        aes_key->cipher,
        BOTAN_CIPHER_UPDATE_FLAG_FINAL,
        out_txt,
        inlen,
        &out_written,
        indata,
        inlen,
        &in_consumed) != BOTAN_FFI_SUCCESS) {
        return -1;
    }
    if (in_consumed != inlen || out_written != inlen) {
        return -1;
    }

    return 0;
}

int crysprBotan_AES_GCMCipher(
    bool bEncrypt,                  /* true:encrypt, false:decrypt */
    CRYSPR_AESCTX* aes_key,         /* CryptoLib AES context */
    unsigned char* iv,              /* iv */
    const unsigned char* aad,       /* associated data */
    size_t aadlen,                  /* associated data length */
    const unsigned char* indata,    /* src */
    size_t inlen,                   /* src length */
    unsigned char* out_txt,         /* dest buffer[inlen] */
    unsigned char* out_tag)         /* dest auth tag */
{
    if (botan_cipher_set_associated_data(
        aes_key->cipher,
        aad,
        aadlen) != BOTAN_FFI_SUCCESS) {
        return -1;
    }

    if (botan_cipher_start(
        aes_key->cipher,
        iv,
        16) != BOTAN_FFI_SUCCESS) {
        return -1;
    }

    size_t out_written = 0;
    size_t in_consumed = 0;
    if (!bEncrypt) {
        if (botan_cipher_update(
            aes_key->cipher,
            BOTAN_CIPHER_UPDATE_FLAG_FINAL,
            out_txt,
            inlen,
            &out_written,
            indata,
            inlen + HAICRYPT_AUTHTAG_MAX,
            &in_consumed) != BOTAN_FFI_SUCCESS) {
            return -1;
        }
        if (out_written != inlen) {
            return -1;
        }
    } else {
        if (botan_cipher_update(
            aes_key->cipher,
            BOTAN_CIPHER_UPDATE_FLAG_FINAL,
            out_txt,
            inlen + HAICRYPT_AUTHTAG_MAX,
            &out_written,
            indata,
            inlen,
            &in_consumed) != BOTAN_FFI_SUCCESS) {
            return -1;
        }
        if (in_consumed != inlen) {
            return -1;
        }
        memcpy(out_tag, out_txt + inlen, HAICRYPT_AUTHTAG_MAX);
    }

    return 0;
}

static CRYSPR_cb *crysprBotan_Open(CRYSPR_methods *cryspr, size_t max_len)
{
    crysprBotan_cb *aes_data;

    aes_data = (crysprBotan_cb *)crysprHelper_Open(cryspr, sizeof(crysprBotan_cb), max_len);
    if (!aes_data) {
        return NULL;
    }

    CRYSPR_cb *cryspr_cb = &aes_data->ccb;

#ifdef CRYSPR2
    cryspr_cb->aes_kek = &aes_data->aes_kek_buf;        // key encrypting key
    cryspr_cb->aes_sek[0] = &aes_data->aes_sek_buf[0];  // stream encrypting key
    cryspr_cb->aes_sek[1] = &aes_data->aes_sek_buf[1];  // stream encrypting key
#endif
    cryspr_cb->aes_kek->cipher = NULL;
    cryspr_cb->aes_sek[0]->cipher = NULL;
    cryspr_cb->aes_sek[1]->cipher = NULL;

    return cryspr_cb;
}

static int crysprBotan_Close(CRYSPR_cb *cryspr_cb)
{
    if (cryspr_cb->aes_kek->cipher) {
        botan_cipher_destroy(cryspr_cb->aes_kek->cipher);
        cryspr_cb->aes_kek->cipher = NULL;
    }
    if (cryspr_cb->aes_sek[0]->cipher) {
        botan_cipher_destroy(cryspr_cb->aes_sek[0]->cipher);
        cryspr_cb->aes_sek[0]->cipher = NULL;
    }
    if (cryspr_cb->aes_sek[1]->cipher) {
        botan_cipher_destroy(cryspr_cb->aes_sek[1]->cipher);
        cryspr_cb->aes_sek[1]->cipher = NULL;
    }

    return crysprHelper_Close(cryspr_cb);
}

int crysprBotan_KmPbkdf2(
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

    if (botan_pwdhash(
        "PBKDF2(SHA-1)",
        itr,
        0,
        0,
        out,
        key_len,
        passwd,
        passwd_len,
        salt,
        salt_len) != BOTAN_FFI_SUCCESS) {
        return -1;
    }

    return 0;
}

int crysprBotan_KmSetKey(CRYSPR_cb *cryspr_cb, bool bWrap, const unsigned char *kek, size_t kek_len)
{
    return crysprBotan_AES_SetKey(
        HCRYPT_CTX_MODE_AESCTR,
        bWrap,
        kek,
        kek_len,
        CRYSPR_GETKEK(cryspr_cb));
}

int crysprBotan_KmWrap(CRYSPR_cb *cryspr_cb,
		unsigned char *wrap,
		const unsigned char *sek,
        unsigned int seklen)
{
    CRYSPR_AESCTX *aes_key = CRYSPR_GETKEK(cryspr_cb); // key encrypting key

    size_t wraplen = seklen + HAICRYPT_WRAPKEY_SIGN_SZ;
    int ret;
    if ((ret = botan_nist_kw_enc(
        get_key_wrap_algo(aes_key->kstr_len),
        0,
        sek,
        seklen,
        aes_key->kstr,
        aes_key->kstr_len,
        wrap,
        &wraplen)) != BOTAN_FFI_SUCCESS) {
        return -1;
    }

    if (seklen + HAICRYPT_WRAPKEY_SIGN_SZ != wraplen) {
        return -1;
    }

    return 0;
}

int crysprBotan_KmUnwrap(
        CRYSPR_cb *cryspr_cb,
		unsigned char *sek,
		const unsigned char *wrap,
        unsigned int wraplen)
{
    CRYSPR_AESCTX *aes_key = CRYSPR_GETKEK(cryspr_cb); // key encrypting key

    size_t seklen = wraplen - HAICRYPT_WRAPKEY_SIGN_SZ;
    if (botan_nist_kw_dec(
        get_key_wrap_algo(aes_key->kstr_len),
        0,
        wrap,
        wraplen,
        aes_key->kstr,
        aes_key->kstr_len,
        sek,
        &seklen) != BOTAN_FFI_SUCCESS) {
        return -1;
    }

    if (wraplen - HAICRYPT_WRAPKEY_SIGN_SZ != seklen) {
        return -1;
    }

    return 0;
}

static CRYSPR_methods crysprBotan_methods;

CRYSPR_methods *crysprBotan(void)
{
    if (crysprBotan_methods.open) {
        return &crysprBotan_methods;
    }

    crysprInit(&crysprBotan_methods);

    /* CryptoLib Primitive API */
    crysprBotan_methods.prng            = crysprBotan_Prng;
    crysprBotan_methods.aes_set_key     = crysprBotan_AES_SetKey;
    crysprBotan_methods.aes_ctr_cipher  = crysprBotan_AES_CtrCipher;
    crysprBotan_methods.aes_gcm_cipher  = crysprBotan_AES_GCMCipher;

    //--Crypto Session (Top API)
    crysprBotan_methods.open            = crysprBotan_Open;
    crysprBotan_methods.close           = crysprBotan_Close;

    //--Keying material (km) encryption
    crysprBotan_methods.km_pbkdf2       = crysprBotan_KmPbkdf2;
    crysprBotan_methods.km_setkey       = crysprBotan_KmSetKey;
    crysprBotan_methods.km_wrap         = crysprBotan_KmWrap;
    crysprBotan_methods.km_unwrap       = crysprBotan_KmUnwrap;

    return &crysprBotan_methods;
}



