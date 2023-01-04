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
        OpenSSL EVP CRYSPR/4SRT (CRYypto Service PRovider for SRT).
*****************************************************************************/

#include "hcrypt.h"

#include <string.h>

typedef struct tag_crysprOpenSSL_EVP_cb
{
    CRYSPR_cb ccb;
    /* Add cryptolib specific data here */
} crysprOpenSSL_EVP_cb;

int crysprOpenSSL_EVP_Prng(unsigned char* rn, int len)
{
    return (RAND_bytes(rn, len) <= 0 ? -1 : 0);
}

const EVP_CIPHER* (*Xcipher_fnptr)(void) = EVP_aes_128_ecb;

const EVP_CIPHER* (*_crysprOpenSSL_EVP_cipher_fnptr[][3])(void) = {
    {NULL, NULL, NULL},
    {EVP_aes_128_ecb, EVP_aes_192_ecb, EVP_aes_256_ecb},
    {EVP_aes_128_ctr, EVP_aes_192_ctr, EVP_aes_256_ctr},
};

int crysprOpenSSL_EVP_AES_SetKey(
    int                  cipher_type, /* One of HCRYPT_CTX_MODE_[CLRTXT|AESECB|AESCTR] */
    bool                 bEncrypt,    /* true Enxcrypt key, false: decrypt */
    const unsigned char* kstr,        /* key sttring*/
    size_t               kstr_len,    /* kstr len in  bytes (16, 24, or 32 bytes (for AES128, AES192, or AES256) */
    CRYSPR_AESCTX*       aes_key)           /* CRYpto Service PRovider AES Key context */
{
    const EVP_CIPHER* cipher  = NULL;
    int               idxKlen = (kstr_len / 8) - 2; /* key_len index in cipher_fnptr array in [0,1,2] range */

    switch (cipher_type)
    {
    case HCRYPT_CTX_MODE_CLRTXT:
        return 0;
    case HCRYPT_CTX_MODE_AESECB:
        break;
    case HCRYPT_CTX_MODE_AESCTR:
#if !CRYSPR_HAS_AESCTR
        /* internal implementation of AES-CTR using crypto lib's AES-ECB */
        cipher_type = HCRYPT_CTX_MODE_AESECB;
#endif
        break;
    default:
        HCRYPT_LOG(LOG_ERR,
                   "invalid cipher type (%d). Expected: [%d..%d]\n",
                   cipher_type,
                   HCRYPT_CTX_MODE_AESECB,
                   HCRYPT_CTX_MODE_AESCTR);
        return (-1);
    }
    switch (kstr_len)
    {
    case 128 / 8:
    case 192 / 8:
    case 256 / 8:
        break;
    default:
        HCRYPT_LOG(LOG_ERR, "invalid key length (%d). Expected: 16, 24, 32\n", (int)kstr_len);
        return -1;
    }
    cipher = _crysprOpenSSL_EVP_cipher_fnptr[cipher_type][idxKlen]();

    if (bEncrypt)
    { /* Encrypt key */
        if (!EVP_EncryptInit_ex(aes_key, cipher, NULL, kstr, NULL))
        {
            HCRYPT_LOG(LOG_ERR, "%s", "EVP_CipherInit_ex(kek) failed\n");
            return (-1);
        }
    }
    else
    { /* Decrypt key */
        if (!EVP_DecryptInit_ex(aes_key, cipher, NULL, kstr, NULL))
        {
            HCRYPT_LOG(LOG_ERR, "%s", "EVP_CipherInit_ex(kek) failed\n");
            return (-1);
        }
    }
    return (0);
}

static CRYSPR_cb* crysprOpenSSL_EVP_Open(CRYSPR_methods* cryspr, size_t max_len)
{
    CRYSPR_cb* cryspr_cb = crysprHelper_Open(cryspr, sizeof(*cryspr_cb), max_len);
    if (NULL == cryspr_cb)
    {
        HCRYPT_LOG(LOG_ERR, "crysprFallback_Open(%p, %zd) failed\n", cryspr, max_len);
        return (NULL);
    }

    cryspr_cb->aes_kek = EVP_CIPHER_CTX_new();

    cryspr_cb->aes_sek[0] = EVP_CIPHER_CTX_new();

    cryspr_cb->aes_sek[1] = EVP_CIPHER_CTX_new();

    return (cryspr_cb);
}

static int crysprOpenSSL_EVP_Close(CRYSPR_cb* cryspr_cb)
{
    if (NULL != cryspr_cb)
    {
        EVP_CIPHER_CTX_free(cryspr_cb->aes_sek[0]);
        EVP_CIPHER_CTX_free(cryspr_cb->aes_sek[1]);
        EVP_CIPHER_CTX_free(cryspr_cb->aes_kek);
    }
    return (crysprHelper_Close(cryspr_cb));
}

#if !(CRYSPR_HAS_AESCTR && CRYSPR_HAS_AESKWRAP)

int crysprOpenSSL_EVP_AES_EcbCipher(bool                 bEncrypt, /* true:encrypt, false:decrypt */
                                    CRYSPR_AESCTX*       aes_key,  /* CRYpto Service PRovider AES Key context */
                                    const unsigned char* indata, /* src (clear text if encrypt, cipher text otherwise)*/
                                    size_t               inlen,  /* indata length */
                                    unsigned char* out_txt, /* dst (cipher text if encrypt, clear text otherwise) */
                                    size_t*        outlen_p)       /* in/out dst len */
{
    int    nmore  = inlen % CRYSPR_AESBLKSZ; /* bytes in last incomplete block */
    int    nblk   = inlen / CRYSPR_AESBLKSZ + (nmore ? 1 : 0); /* blocks including incomplete */
    size_t outsiz = (outlen_p ? *outlen_p : 0);
    int    c_len = 0, f_len = 0;

    (void)bEncrypt; // not needed, alreadydefined in context

    if (outsiz % CRYSPR_AESBLKSZ)
    {
        HCRYPT_LOG(LOG_ERR, "%s\n", "EcbCipher() no room for PKCS7 padding");
        return (-1); /* output buf size must be a multiple of AES block size (16) */
    }
    if ((outsiz > 16) && ((int)outsiz < (nblk * CRYSPR_AESBLKSZ)))
    {
        HCRYPT_LOG(LOG_ERR, "%s\n", "EcbCipher() no room for PKCS7 padding");
        return (-1); /* output buf size must have room for PKCS7 padding */
    }
    /* allows reusing of 'e' for multiple encryption cycles */
    if (!EVP_CipherInit_ex(aes_key, NULL, NULL, NULL, NULL, -1))
    {
        HCRYPT_LOG(LOG_ERR, "EVP_CipherInit_ex(%p,NULL,...,-1) failed\n", aes_key);
        return -1;
    }
    if (!EVP_CIPHER_CTX_set_padding(aes_key, 0))
    {
        HCRYPT_LOG(LOG_ERR, "%s\n", "EVP_CIPHER_CTX_set_padding(%p) failed", aes_key);
        return -1;
    }

    /* update ciphertext, c_len is filled with the length of ciphertext generated,
     * cryptoPtr->cipher_in_len is the size of plain/cipher text in bytes
     */
    if (!EVP_CipherUpdate(aes_key, out_txt, &c_len, indata, inlen))
    {
        HCRYPT_LOG(LOG_ERR, "EVP_CipherUpdate(%p, out, %d, in, %d) failed\n", aes_key, c_len, inlen);
        return -1;
    }

    /* update ciphertext with the final remaining bytes */
    /* Useless with pre-padding */
    f_len = 0;
    if (0 == EVP_CipherFinal_ex(aes_key, &out_txt[c_len], &f_len))
    {
#if ENABLE_HAICRYPT_LOGGING
        char szErrBuf[256];
        HCRYPT_LOG(LOG_ERR,
                   "EVP_CipherFinal_ex(ctx,&out[%d],%d)) failed: %s\n",
                   c_len,
                   f_len,
                   ERR_error_string(ERR_get_error(), szErrBuf));
#endif /*ENABLE_HAICRYPT_LOGGING*/
        return -1;
    }
    if (outlen_p != NULL) *outlen_p = nblk * CRYSPR_AESBLKSZ;
    return 0;
}
#endif /* !(CRYSPR_HAS_AESCTR && CRYSPR_HAS_AESKWRAP) */

int crysprOpenSSL_EVP_AES_CtrCipher(bool                 bEncrypt, /* true:encrypt, false:decrypt */
                                    CRYSPR_AESCTX*       aes_key,  /* CRYpto Service PRovider AES Key context */
                                    unsigned char*       iv,       /* iv */
                                    const unsigned char* indata,   /* src */
                                    size_t               inlen,    /* length */
                                    unsigned char*       out_txt)        /* dest */

{
    int c_len, f_len;

    (void)bEncrypt;

    /* allows reusing of 'e' for multiple encryption cycles */
    if (!EVP_CipherInit_ex(aes_key, NULL, NULL, NULL, iv, -1))
    {
        HCRYPT_LOG(LOG_ERR, "%s\n", "EVP_CipherInit_ex() failed");
        return -1;
    }
    if (!EVP_CIPHER_CTX_set_padding(aes_key, 0))
    {
        HCRYPT_LOG(LOG_ERR, "%s\n", "EVP_CIPHER_CTX_set_padding() failed");
        return -1;
    }

    /* update ciphertext, c_len is filled with the length of ciphertext generated,
     * cryptoPtr->cipher_in_len is the size of plain/cipher text in bytes
     */
    if (!EVP_CipherUpdate(aes_key, out_txt, &c_len, indata, inlen))
    {
        HCRYPT_LOG(LOG_ERR, "%s\n", "EVP_CipherUpdate() failed");
        return -1;
    }

    /* update ciphertext with the final remaining bytes */
    /* Useless with pre-padding */
    f_len = 0;
    if (0 == EVP_CipherFinal_ex(aes_key, &out_txt[c_len], &f_len))
    {
#if ENABLE_HAICRYPT_LOGGING
        char szErrBuf[256];
        HCRYPT_LOG(LOG_ERR,
                   "EVP_CipherFinal_ex(ctx,&out[%d],%d)) failed: %s\n",
                   c_len,
                   f_len,
                   ERR_error_string(ERR_get_error(), szErrBuf));
#endif /*ENABLE_HAICRYPT_LOGGING*/
        return -1;
    }
    return 0;
}

/*
 * Password-based Key Derivation Function
 */
int crysprOpenSSL_EVP_KmPbkdf2(CRYSPR_cb*     cryspr_cb,
                               char*          passwd,     /* passphrase */
                               size_t         passwd_len, /* passphrase len */
                               unsigned char* salt,       /* salt */
                               size_t         salt_len,   /* salt_len */
                               int            itr,        /* iterations */
                               size_t         key_len,    /* key_len */
                               unsigned char* out)        /* derived key */
{
    (void)cryspr_cb;
    int rc = PKCS5_PBKDF2_HMAC_SHA1(passwd, passwd_len, salt, salt_len, itr, key_len, out);
    return (rc == 1 ? 0 : -1);
}

#if CRYSPR_HAS_AESKWRAP
int crysprOpenSSL_EVP_KmWrap(CRYSPR_cb* cryspr_cb, unsigned char* wrap, const unsigned char* sek, unsigned int seklen)
{
    crysprOpenSSL_EVP_cb* aes_data = (crysprOpenSSL_EVP_cb*)cryspr_cb;
    EVP_CIPHER_CTX*       kek      = CRYSPR_GETKEK(cryspr_cb); // key encrypting key

    return (((seklen + HAICRYPT_WRAPKEY_SIGN_SZ) == (unsigned int)AES_wrap_key(kek, NULL, wrap, sek, seklen)) ? 0 : -1);
}

int crysprOpenSSL_EVP_KmUnwrap(CRYSPR_cb*           cryspr_cb,
                               unsigned char*       sek, // Stream encrypting key
                               const unsigned char* wrap,
                               unsigned int         wraplen)
{
    crysprOpenSSL_EVP_cb* aes_data = (crysprOpenSSL_EVP_cb*)cryspr_cb;
    EVP_CIPHER_CTX*       kek      = CRYSPR_GETKEK(cryspr_cb); // key encrypting key

    return (((wraplen - HAICRYPT_WRAPKEY_SIGN_SZ) == (unsigned int)AES_unwrap_key(kek, NULL, sek, wrap, wraplen)) ? 0
                                                                                                                  : -1);
}
#endif /*CRYSPR_HAS_AESKWRAP*/

static CRYSPR_methods crysprOpenSSL_EVP_methods;

CRYSPR_methods* crysprOpenSSL_EVP(void)
{
    if (NULL == crysprOpenSSL_EVP_methods.open)
    {
        crysprInit(&crysprOpenSSL_EVP_methods); // Default/fallback methods

        crysprOpenSSL_EVP_methods.prng = crysprOpenSSL_EVP_Prng;
        //--CryptoLib Primitive API-----------------------------------------------
        crysprOpenSSL_EVP_methods.aes_set_key = crysprOpenSSL_EVP_AES_SetKey;
#if CRYSPR_HAS_AESCTR
        crysprOpenSSL_EVP_methods.aes_ctr_cipher = crysprOpenSSL_EVP_AES_CtrCipher;
#endif
#if !(CRYSPR_HAS_AESCTR && CRYSPR_HAS_AESKWRAP)
        /* AES-ECB only required if cryspr has no AES-CTR and no AES KeyWrap */
        /* OpenSSL has both AESCTR and AESKWRP and the AESECB wrapper is only used
           to test the falback methods */
        crysprOpenSSL_EVP_methods.aes_ecb_cipher = crysprOpenSSL_EVP_AES_EcbCipher;
#endif
#if !CRYSPR_HAS_PBKDF2
        crysprOpenSSL_EVP_methods.sha1_msg_digest = NULL; // Required to use eventual default/fallback KmPbkdf2
#endif

        //--Crypto Session API-----------------------------------------
        crysprOpenSSL_EVP_methods.open  = crysprOpenSSL_EVP_Open;
        crysprOpenSSL_EVP_methods.close = crysprOpenSSL_EVP_Close;
        //--Keying material (km) encryption

#if CRYSPR_HAS_PBKDF2
        crysprOpenSSL_EVP_methods.km_pbkdf2 = crysprOpenSSL_EVP_KmPbkdf2;
#else
#error There is no default/fallback method for PBKDF2
#endif
        //	crysprOpenSSL_EVP_methods.km_setkey  =
#if CRYSPR_HAS_AESKWRAP
        crysprOpenSSL_EVP_methods.km_wrap   = crysprOpenSSL_EVP_KmWrap;
        crysprOpenSSL_EVP_methods.km_unwrap = crysprOpenSSL_EVP_KmUnwrap;
#endif

        //--Media stream (ms) encryption
        //  crysprOpenSSL_EVP_methods.ms_setkey  =
        //	crysprOpenSSL_EVP_methods.ms_encrypt =
        //	crysprOpenSSL_EVP_methods.ms_decrypt =
    }
    return (&crysprOpenSSL_EVP_methods);
}
