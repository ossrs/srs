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

#include "hcrypt.h"
#include "cryspr.h"

#include <stdlib.h>
#include <string.h>

int crysprStub_Prng(unsigned char *rn, int len)
{
    (void)rn;
    (void)len;
    return(0);
}

int crysprStub_AES_SetKey(
    bool bEncrypt,              /* true Enxcrypt key, false: decrypt */
    const unsigned char *kstr,  /* key sttring*/
    size_t kstr_len,            /* kstr len in  bytes (16, 24, or 32 bytes (for AES128,AES192, or AES256) */
    CRYSPR_AESCTX *aes_key)     /* Cryptolib Specific AES key context */
{
    (void)bEncrypt;
    (void)kstr;
    (void)kstr_len;
    (void)aes_key;

    return(0);
}

int crysprStub_AES_EcbCipher(
    bool bEncrypt,              /* true:encrypt, false:decrypt */
    CRYSPR_AESCTX *aes_key,     /* AES context */
    const unsigned char *indata,/* src (clear text)*/
    size_t inlen,               /* length */
    unsigned char *out_txt,     /* dst (cipher text) */
    size_t *outlen)             /* dst len */
{
    (void)bEncrypt;
    (void)aes_key;
    (void)indata;
    (void)inlen;
    (void)out_txt;
    (void)outlen;

    return -1;
}

int crysprStub_AES_CtrCipher(
    bool bEncrypt,              /* true:encrypt, false:decrypt */
    CRYSPR_AESCTX *aes_key,     /* AES context */
    unsigned char *iv,          /* iv */
    const unsigned char *indata,/* src */
    size_t inlen,               /* length */
    unsigned char *out_txt)     /* dest */
{
    (void)bEncrypt;
    (void)aes_key;
    (void)iv;
    (void)indata;
    (void)inlen;
    (void)out_txt;

    return(-1);
}

unsigned char *crysprStub_SHA1_MsgDigest(
    const unsigned char *m, /* in: message */
    size_t m_len,           /* message length */
    unsigned char *md)      /* out: message digest buffer *160 bytes */
{
    (void)m;
    (void)m_len;
    (void)md;

    return(NULL);//return md;
}

/*
* Password-based Key Derivation Function
*/
int crysprStub_KmPbkdf2(
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
    (void)passwd;
    (void)passwd_len;
    (void)salt;
    (void)salt_len;
    (void)itr;
    (void)key_len;
    (void)out;

    /* >>Todo:
     * develop PBKDF2 using SHA1 primitive cryspr_cb->cryspr->sha1_msg_digest() for cryptolibs not providing it
     */
    return(-1);
}

static int crysprFallback_KmSetKey(CRYSPR_cb *cryspr_cb, bool bWrap, const unsigned char *kek, size_t kek_len)
{
	CRYSPR_AESCTX *aes_kek = &cryspr_cb->aes_kek;

    if (cryspr_cb->cryspr->aes_set_key(bWrap, kek, kek_len, aes_kek)) {
        HCRYPT_LOG(LOG_ERR, "AES_set_%s_key(kek) failed\n", bWrap? "encrypt": "decrypt");
        return(-1);
    }
	return(0);
}

/*
* AES_wrap_key()/AES_unwrap_key() introduced in openssl 0.9.8h
* Here is an implementation using AES native API for cryspr not providing it.
*/

static const unsigned char default_iv[] = {
  0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6,
};
int crysprFallback_AES_WrapKey(CRYSPR_cb *cryspr_cb,
		unsigned char *out,
		const unsigned char *in,
        unsigned int inlen)
{
	unsigned char *A, B[16], *R;
	const unsigned char *iv = default_iv;
	unsigned int i, j, t;
	if ((inlen & 0x7) || (inlen < 8))
		return -1;
	A = B;
	t = 1;
	memcpy(out + 8, in, inlen);

	memcpy(A, iv, 8);

	for (j = 0; j < 6; j++)
	{
		R = out + 8;
		for (i = 0; i < inlen; i += 8, t++, R += 8)
		{
			memcpy(B + 8, R, 8);
			{
				size_t outlen = 16;
				cryspr_cb->cryspr->aes_ecb_cipher(true, &cryspr_cb->aes_kek, B, 16, B, &outlen);
			}
			A[7] ^= (unsigned char)(t & 0xff);
			if (t > 0xff)	
			{
				A[6] ^= (unsigned char)((t >> 8) & 0xff);
				A[5] ^= (unsigned char)((t >> 16) & 0xff);
				A[4] ^= (unsigned char)((t >> 24) & 0xff);
			}
			memcpy(R, B + 8, 8);
		}
	}
	memcpy(out, A, 8);
    return 0;
}

int crysprFallback_AES_UnwrapKey(CRYSPR_cb *cryspr_cb,
		unsigned char *out,
		const unsigned char *in,
        unsigned int inlen)
{
	unsigned char *A, B[16], *R;
	const unsigned char *iv = default_iv;
	unsigned int i, j, t;
	inlen -= 8;
	if (inlen & 0x7)
		return -1;
	if (inlen < 8)
		return -1;
	A = B;
	t =  6 * (inlen >> 3);
	memcpy(A, in, 8);
	memcpy(out, in + 8, inlen);
	for (j = 0; j < 6; j++)
	{
		R = out + inlen - 8;
		for (i = 0; i < inlen; i += 8, t--, R -= 8)
		{
			A[7] ^= (unsigned char)(t & 0xff);
			if (t > 0xff)	
			{
				A[6] ^= (unsigned char)((t >> 8) & 0xff);
				A[5] ^= (unsigned char)((t >> 16) & 0xff);
				A[4] ^= (unsigned char)((t >> 24) & 0xff);
			}
			memcpy(B + 8, R, 8);
			{
				size_t outlen = 16;
				cryspr_cb->cryspr->aes_ecb_cipher(false, &cryspr_cb->aes_kek, B, 16, B, &outlen);
			}
			memcpy(R, B + 8, 8);
		}
	}
	if (memcmp(A, iv, 8))
	{
		memset(out, 0, inlen);
        return -1;
	}
    return 0;
}

static unsigned char *_crysprFallback_GetOutbuf(CRYSPR_cb *cryspr_cb, size_t pfx_len, size_t out_len)
{
	unsigned char *out_buf;

	if ((pfx_len + out_len) > (cryspr_cb->outbuf_siz - cryspr_cb->outbuf_ofs)) {
		/* Not enough room left, circle buffers */
		cryspr_cb->outbuf_ofs = 0;
	}
	out_buf = &cryspr_cb->outbuf[cryspr_cb->outbuf_ofs];
	cryspr_cb->outbuf_ofs += (pfx_len + out_len);
	return(out_buf);
}

static CRYSPR_cb *crysprFallback_Open(CRYSPR_methods *cryspr, size_t max_len)
{
	CRYSPR_cb *cryspr_cb;
	unsigned char *membuf;
	size_t memsiz, padded_len = hcryptMsg_PaddedLen(max_len, 128/8);

	HCRYPT_LOG(LOG_DEBUG, "%s", "Using OpenSSL AES\n");

	memsiz = sizeof(*cryspr_cb) + (CRYSPR_OUTMSGMAX * padded_len);
#if !CRYSPR_HAS_AESCTR
	memsiz += HCRYPT_CTR_STREAM_SZ;
#endif /* !CRYSPR_HAS_AESCTR */

	cryspr_cb = malloc(memsiz);
	if (NULL == cryspr_cb) {
		HCRYPT_LOG(LOG_ERR, "malloc(%zd) failed\n", memsiz);
		return(NULL);
	}
	membuf = (unsigned char *)cryspr_cb;
	membuf += sizeof(*cryspr_cb);

#if !CRYSPR_HAS_AESCTR
	cryspr_cb->ctr_stream = membuf;
	membuf += HCRYPT_CTR_STREAM_SZ;
	cryspr_cb->ctr_stream_siz = HCRYPT_CTR_STREAM_SZ;
	cryspr_cb->ctr_stream_len = 0;
#endif /* !CRYSPR_HAS_AESCTR */

	cryspr_cb->outbuf = membuf;
	cryspr_cb->outbuf_siz = CRYSPR_OUTMSGMAX * padded_len;
	cryspr_cb->outbuf_ofs = 0;
//	membuf += cryspr_cb->outbuf_siz;

	cryspr_cb->cryspr=(CRYSPR_methods *)cryspr;

	return(cryspr_cb);
}

static int crysprFallback_Close(CRYSPR_cb *cryspr_cb)
{
	if (NULL != cryspr_cb) {
		free(cryspr_cb);
	}
	return(0);
}

static int crysprFallback_MsSetKey(CRYSPR_cb *cryspr_cb, hcrypt_Ctx *ctx, const unsigned char *key, size_t key_len)
{
	CRYSPR_AESCTX *aes_sek = &cryspr_cb->aes_sek[hcryptCtx_GetKeyIndex(ctx)]; /* Ctx tells if it's for odd or even key */

	if ((ctx->flags & HCRYPT_CTX_F_ENCRYPT)        /* Encrypt key */
	||  (ctx->mode == HCRYPT_CTX_MODE_AESCTR)) {   /* CTR mode decrypts using encryption methods */
        	if (cryspr_cb->cryspr->aes_set_key(true, key, key_len, aes_sek)) {
			HCRYPT_LOG(LOG_ERR, "%s", "CRYSPR->set_encrypt_key(sek) failed\n");
			return(-1);
		}
	} else {                                       /* Decrypt key */
		if (cryspr_cb->cryspr->aes_set_key(false, key, key_len, aes_sek)) {
			HCRYPT_LOG(LOG_ERR, "%s", "CRYSPR->set_decrypt_key(sek) failed\n");
			return(-1);
		}
	}
	return(0);
}

#if !CRYSPR_HAS_AESCTR
static int _crysprFallback_AES_SetCtrStream(CRYSPR_cb *cryspr_cb, hcrypt_Ctx *ctx, size_t len, unsigned char *iv)
{
	/* Counter stream:
	 *   0   1   2   3   4   5     nblk
	 * +---+---+---+---+---+---+---+---+
	 * |blk|blk|blk|blk|blk|blk|...|blk|
	 * +---+---+---+---+---+---+---+---+
	 */

	/* IV (128-bit):
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
	unsigned char ctr[HCRYPT_CTR_BLK_SZ];
	unsigned nblk;

	ASSERT(NULL != cryspr_cb);
	ASSERT(NULL != ctx);

	memcpy(ctr, iv, HCRYPT_CTR_BLK_SZ);

	nblk = (len + (HCRYPT_CTR_BLK_SZ-1))/HCRYPT_CTR_BLK_SZ;
	if ((nblk * HCRYPT_CTR_BLK_SZ) <= cryspr_cb->ctr_stream_siz) {
		unsigned blk;
		unsigned char *csp = &cryspr_cb->ctr_stream[0];

		for(blk = 0; blk < nblk; blk++) {
			memcpy(csp, ctr, HCRYPT_CTR_BLK_SZ);
			csp += HCRYPT_CTR_BLK_SZ;
			if (0 == ++(ctr[HCRYPT_CTR_BLK_SZ-1])) ++(ctr[HCRYPT_CTR_BLK_SZ-2]);
		}
		cryspr_cb->ctr_stream_len = nblk * HCRYPT_CTR_BLK_SZ;
	} else {
		HCRYPT_LOG(LOG_ERR, "packet too long(%zd)\n", len);
		return(-1);
	}
	return(0);
}
#endif

static int crysprFallback_MsEncrypt(
	CRYSPR_cb *cryspr_cb,
	hcrypt_Ctx *ctx,
	hcrypt_DataDesc *in_data, int nbin ATR_UNUSED,
	void *out_p[], size_t out_len_p[], int *nbout_p)
{
	unsigned char *out_msg;
	size_t out_len = 0;	//payload size
	int pfx_len;

	ASSERT(NULL != ctx);
	ASSERT(NULL != cryspr_cb);
	ASSERT((NULL != in_data) || (1 == nbin)); //Only one in_data[] supported

	/* 
	 * Get message prefix length
	 * to reserve room for unencrypted message header in output buffer
	 */
	pfx_len = ctx->msg_info->pfx_len;

	/* Get buffer room from the internal circular output buffer */
	out_msg = _crysprFallback_GetOutbuf(cryspr_cb, pfx_len, in_data[0].len);

	if (NULL != out_msg) {
		switch(ctx->mode) {
			case HCRYPT_CTX_MODE_AESCTR: /* Counter mode */
			{
#if CRYSPR_HAS_AESCTR
				/* Get current key (odd|even) from context */
				CRYSPR_AESCTX *aes_key = &cryspr_cb->aes_sek[hcryptCtx_GetKeyIndex(ctx)];
				unsigned char iv[CRYSPR_AESBLKSZ];

				/* Get input packet index (in network order) */
				hcrypt_Pki pki = hcryptMsg_GetPki(ctx->msg_info, in_data[0].pfx, 1);

				/*
				 * Compute the Initial Vector
				 * IV (128-bit):
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
				hcrypt_SetCtrIV((unsigned char *)&pki, ctx->salt, iv);

				cryspr_cb->cryspr->aes_ctr_cipher(true, aes_key, iv, in_data[0].payload, in_data[0].len,
						&out_msg[pfx_len]);
#else /*CRYSPR_HAS_AESCTR*/
				/* Get current key (odd|even) from context */
				CRYSPR_AESCTX *aes_key = &cryspr_cb->aes_sek[hcryptCtx_GetKeyIndex(ctx)];
				unsigned char iv[CRYSPR_AESBLKSZ];
				int iret = 0;

				/* Get input packet index (in network order) */
				hcrypt_Pki pki = hcryptMsg_GetPki(ctx->msg_info, in_data[0].pfx, 1);

				/*
				 * Compute the Initial Vector
				 * IV (128-bit):
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
				hcrypt_SetCtrIV((unsigned char *)&pki, ctx->salt, iv);

				/* Create CtrStream. May be longer than in_len (next cryspr block size boundary) */
				iret = _crysprFallback_AES_SetCtrStream(cryspr_cb, ctx, in_data[0].len, iv);
				if (iret) {
					return(iret);
				}
				/* Reserve output buffer for cryspr */
				out_msg = _crysprFallback_GetOutbuf(cryspr_cb, pfx_len, cryspr_cb->ctr_stream_len);

				/* Create KeyStream (encrypt CtrStream) */
				iret = cryspr_cb->cryspr->aes_ecb_cipher(true, aes_key,
						cryspr_cb->ctr_stream, cryspr_cb->ctr_stream_len,
						&out_msg[pfx_len], &out_len);
				if (iret) {
					HCRYPT_LOG(LOG_ERR, "%s", "hcOpenSSL_AES_ecb_cipher(encrypt, failed\n");
					return(iret);
				}
#endif/*CRYSPR_HAS_AESCTR*/
				/* Prepend packet prefix (clear text) in output buffer */
				memcpy(out_msg, in_data[0].pfx, pfx_len);
				/* CTR mode output length is same as input, no padding */
				out_len = in_data[0].len;
				break;
			}
			case HCRYPT_CTX_MODE_CLRTXT:    /* Clear text mode (transparent mode for tests) */
				memcpy(&out_msg[pfx_len], in_data[0].payload, in_data[0].len);
				memcpy(out_msg, in_data[0].pfx, pfx_len);
				out_len = in_data[0].len;
				break;
			default:
				/* Unsupported cipher mode */
				return(-1);
		}
	} else {
		/* input data too big */
		return(-1);
	}

	if (out_len > 0) {
		/* Encrypted messages have been produced */
		if (NULL == out_p) {
			/*
			 * Application did not provided output buffer,
			 * so copy encrypted message back in input buffer
			 */
			memcpy(in_data[0].pfx, out_msg, pfx_len);
#if !CRYSPR_HAS_AESCTR
			if (ctx->mode == HCRYPT_CTX_MODE_AESCTR) {
				/* XOR KeyStream with input text directly in input buffer */
				hcrypt_XorStream(in_data[0].payload, &out_msg[pfx_len], out_len);
			}else{
				/* Copy output data back in input buffer */
				memcpy(in_data[0].payload, &out_msg[pfx_len], out_len);
			}
#else /* CRYSPR_HAS_AESCTR */
			/* Copy output data back in input buffer */
			memcpy(in_data[0].payload, &out_msg[pfx_len], out_len);
#endif /* CRYSPR_HAS_AESCTR */
		} else {
			/* Copy header in output buffer if needed */
			if (pfx_len > 0) memcpy(out_msg, in_data[0].pfx, pfx_len);
#if !CRYSPR_HAS_AESCTR
			if (ctx->mode == HCRYPT_CTX_MODE_AESCTR) {
				hcrypt_XorStream(&out_msg[pfx_len], in_data[0].payload, out_len);
			}
#endif /* CRYSPR_HAS_AESCTR */
			out_p[0] = out_msg;
			out_len_p[0] = pfx_len + out_len;
			*nbout_p = 1;
		}
	} else {
		/*
		 * Nothing out
		 * This is not an error for implementations using deferred/async processing
		 * with co-processor, DSP, crypto hardware, etc.
		 * Submitted input data could be returned encrypted in a next call.
		 */
		if (nbout_p != NULL) *nbout_p = 0;
		return(-1);
	}
	return(0);
}

static int crysprFallback_MsDecrypt(CRYSPR_cb *cryspr_cb, hcrypt_Ctx *ctx,
	hcrypt_DataDesc *in_data, int nbin ATR_UNUSED, void *out_p[], size_t out_len_p[], int *nbout_p)
{
	unsigned char *out_txt;
	size_t out_len;
	int iret = 0;

	ASSERT(NULL != cryspr_cb);
	ASSERT(NULL != ctx);
	ASSERT((NULL != in_data) || (1 == nbin)); //Only one in_data[] supported

	/* Reserve output buffer (w/no header) */
	out_txt = _crysprFallback_GetOutbuf(cryspr_cb, 0, in_data[0].len);

	if (NULL != out_txt) {
		switch(ctx->mode) {
			case HCRYPT_CTX_MODE_AESCTR:
			{
#if CRYSPR_HAS_AESCTR
				/* Get current key (odd|even) from context */
				CRYSPR_AESCTX *aes_key = &cryspr_cb->aes_sek[hcryptCtx_GetKeyIndex(ctx)];
				unsigned char iv[CRYSPR_AESBLKSZ];

				/* Get input packet index (in network order) */
				hcrypt_Pki pki = hcryptMsg_GetPki(ctx->msg_info, in_data[0].pfx, 1);

				/*
				 * Compute the Initial Vector
				 * IV (128-bit):
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
				hcrypt_SetCtrIV((unsigned char *)&pki, ctx->salt, iv);

				cryspr_cb->cryspr->aes_ctr_cipher(false, aes_key, iv, in_data[0].payload, in_data[0].len,
						out_txt);
				out_len = in_data[0].len;
#else  /*CRYSPR_HAS_AESCTR*/
				/* Get current key (odd|even) from context */
				CRYSPR_AESCTX *aes_key = &cryspr_cb->aes_sek[hcryptCtx_GetKeyIndex(ctx)];
				unsigned char iv[CRYSPR_AESBLKSZ];
				int iret = 0;

				/* Get input packet index (in network order) */
				hcrypt_Pki pki = hcryptMsg_GetPki(ctx->msg_info, in_data[0].pfx, 1);

				/*
				 * Compute the Initial Vector
				 * IV (128-bit):
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
				hcrypt_SetCtrIV((unsigned char *)&pki, ctx->salt, iv);

				/* Create CtrStream. May be longer than in_len (next cipher block size boundary) */
				iret = _crysprFallback_AES_SetCtrStream(cryspr_cb, ctx, in_data[0].len, iv);
				if (iret) {
					return(iret);
				}
				/* Reserve output buffer for cryspr */
				out_txt = _crysprFallback_GetOutbuf(cryspr_cb, 0, cryspr_cb->ctr_stream_len);

				/* Create KeyStream (encrypt CtrStream) */
				iret = cryspr_cb->cryspr->aes_ecb_cipher(true, aes_key,
						cryspr_cb->ctr_stream, cryspr_cb->ctr_stream_len,
						out_txt, &out_len);
				if (iret) {
					HCRYPT_LOG(LOG_ERR, "%s", "crysprNatural_AES_ecb_cipher(encrypt failed\n");
					return(iret);
				}

#endif /*CRYSPR_HAS_AESCTR*/
				break;
			}
			case HCRYPT_CTX_MODE_CLRTXT:
				memcpy(out_txt, in_data[0].payload, in_data[0].len);
				out_len = in_data[0].len;
				break;
			default:
				return(-1);
		}
	} else {
		return(-1);
	}

	if (out_len > 0) {
		if (NULL == out_p) {
			/*
			 * Application did not provided output buffer,
			 * so copy encrypted message back in input buffer
			 */
#if !CRYSPR_HAS_AESCTR
			if (ctx->mode == HCRYPT_CTX_MODE_AESCTR) {
				/* XOR KeyStream with input text directly in input buffer */
				hcrypt_XorStream(in_data[0].payload, out_txt, out_len);
			}else{
				/* Copy output data back in input buffer */
				memcpy(in_data[0].payload, out_txt, out_len);
			}
#else /* CRYSPR_HAS_AESCTR */
			/* Copy output data back in input buffer */
			memcpy(in_data[0].payload, out_txt, out_len);
#endif /* CRYSPR_HAS_AESCTR */
		} else {
			/* Copy header in output buffer if needed */
#if !CRYSPR_HAS_AESCTR
			if (ctx->mode == HCRYPT_CTX_MODE_AESCTR) {
				hcrypt_XorStream(out_txt, in_data[0].payload, out_len);
			}
#endif /* CRYSPR_HAS_AESCTR */
			out_p[0] = out_txt;
			out_len_p[0] = out_len;
			*nbout_p = 1;
		}
		iret = 0;
	} else {
		if (NULL != nbout_p) *nbout_p = 0;
		iret = -1;
	}

#if 0
	{	/* Debug decryption errors */
		static int nberr = 0;

		if (out_txt[0] != 0x47){
			if ((++nberr == 1)
					||  ((nberr > 500) && (0 == ((((unsigned char *)&MSmsg->pki)[2] & 0x0F)|((unsigned char *)&MSmsg->pki)[3])))) {
				HCRYPT_LOG(LOG_DEBUG, "keyindex=%d\n", hcryptCtx_GetKeyIndex(ctx));
				HCRYPT_PRINTKEY(ctx->sek, ctx->sek_len, "sek"); 
				HCRYPT_PRINTKEY(ctx->salt, ctx->salt_len, "salt"); 
			}
		} else {
			nberr = 0;
		}
	}
#endif
	return(iret);
}


CRYSPR_methods *crysprInit(CRYSPR_methods *cryspr)
{
	/* CryptoLib Primitive API */
	cryspr->prng            = crysprStub_Prng;
	cryspr->aes_set_key     = crysprStub_AES_SetKey;
	cryspr->aes_ecb_cipher  = crysprStub_AES_EcbCipher;
	cryspr->aes_ctr_cipher  = crysprStub_AES_CtrCipher;
	cryspr->sha1_msg_digest = crysprStub_SHA1_MsgDigest;


	/* Crypto Session API */
	cryspr->open       = crysprFallback_Open;
	cryspr->close      = crysprFallback_Close;
	//Keying material (km) encryption
	cryspr->km_pbkdf2  = crysprStub_KmPbkdf2;
	cryspr->km_setkey  = crysprFallback_KmSetKey;
	cryspr->km_wrap    = crysprFallback_AES_WrapKey;
	cryspr->km_unwrap  = crysprFallback_AES_UnwrapKey;
	//Media stream (ms) encryption
	cryspr->ms_setkey  = crysprFallback_MsSetKey;
	cryspr->ms_encrypt = crysprFallback_MsEncrypt;
	cryspr->ms_decrypt = crysprFallback_MsDecrypt;

	return(cryspr);
}

HaiCrypt_Cryspr HaiCryptCryspr_Get_Instance(void)
{
    return((HaiCrypt_Cryspr)cryspr4SRT());
}
