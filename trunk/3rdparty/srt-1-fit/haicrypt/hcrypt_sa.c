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
*****************************************************************************/

/*
 * For now: 
 *  Pre-shared or password derived KEK (Key Encrypting Key)
 * Future:
 *  Certificate-based association
 */
#include <string.h>				/* memcpy */
#include "hcrypt.h"

int hcryptCtx_SetSecret(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const HaiCrypt_Secret *secret)
{
	int iret;
	(void)crypto;

	switch(secret->typ) {
	case HAICRYPT_SECTYP_PRESHARED:
		ASSERT(secret->len <= HAICRYPT_KEY_MAX_SZ);
		ctx->cfg.pwd_len = 0;
		/* KEK: Key Encrypting Key */
		if (0 > (iret = crypto->cryspr->km_setkey(crypto->cryspr_cb,
							  (HCRYPT_CTX_F_ENCRYPT & ctx->flags ? true : false),
							  secret->str, secret->len))) {
			HCRYPT_LOG(LOG_ERR, "km_setkey(pdkek[%zd]) failed (rc=%d)\n", secret->len, iret);
			return(-1);
		}
		ctx->status = HCRYPT_CTX_S_SARDY;
		break;

	case HAICRYPT_SECTYP_PASSPHRASE:
		ASSERT(secret->len <= sizeof(ctx->cfg.pwd));
		memcpy(ctx->cfg.pwd, secret->str, secret->len);
		ctx->cfg.pwd_len = secret->len;
		/* KEK will be derived from password with Salt */
		ctx->status = HCRYPT_CTX_S_SARDY;
		break;

	default:
	    HCRYPT_LOG(LOG_ERR, "Unknown secret type  %d\n", 
			secret->typ);
		return(-1);
	}
	return(0);
}

int hcryptCtx_GenSecret(hcrypt_Session *crypto, hcrypt_Ctx *ctx)
{
	/*
	 * KEK need same length as the key it protects (SEK)
	 * KEK = PBKDF2(Pwd, LSB(64, Salt), Iter, Klen)
	 */
	unsigned char kek[HAICRYPT_KEY_MAX_SZ];
	size_t kek_len = ctx->sek_len;
	size_t pbkdf_salt_len = (ctx->salt_len >= HAICRYPT_PBKDF2_SALT_LEN
		? HAICRYPT_PBKDF2_SALT_LEN 
		: ctx->salt_len);
	int iret = 0;
	(void)crypto;

	iret = crypto->cryspr->km_pbkdf2(crypto->cryspr_cb, ctx->cfg.pwd, ctx->cfg.pwd_len,
		&ctx->salt[ctx->salt_len - pbkdf_salt_len], pbkdf_salt_len, 
		HAICRYPT_PBKDF2_ITER_CNT, kek_len, kek);

	if(iret) {
		HCRYPT_LOG(LOG_ERR, "km_pbkdf2() failed (rc=%d)\n", iret);
		return(-1);
	}
	HCRYPT_PRINTKEY(ctx->cfg.pwd, ctx->cfg.pwd_len, "pwd");
	HCRYPT_PRINTKEY(kek, kek_len, "kek");
	
	/* KEK: Key Encrypting Key */
	if (0 > (iret = crypto->cryspr->km_setkey(crypto->cryspr_cb, (HCRYPT_CTX_F_ENCRYPT & ctx->flags ? true : false), kek, kek_len))) {
		HCRYPT_LOG(LOG_ERR, "km_setkey(pdkek[%zd]) failed (rc=%d)\n", kek_len, iret);
		return(-1);
	}
	return(0);
}

