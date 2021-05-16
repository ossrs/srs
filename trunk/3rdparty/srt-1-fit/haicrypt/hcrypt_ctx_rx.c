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

#include <string.h>				/* memcpy */
#include "hcrypt.h"

int hcryptCtx_Rx_Init(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const HaiCrypt_Cfg *cfg)
{
    ctx->mode = HCRYPT_CTX_MODE_AESCTR;
    ctx->status = HCRYPT_CTX_S_INIT;

    ctx->msg_info = crypto->msg_info;

    if (cfg && hcryptCtx_SetSecret(crypto, ctx, &cfg->secret)) {
        return(-1);
    }
    ctx->status = HCRYPT_CTX_S_SARDY;
    return(0);
}

int hcryptCtx_Rx_Rekey(hcrypt_Session *crypto, hcrypt_Ctx *ctx, unsigned char *sek, size_t sek_len)
{
	if (crypto->cryspr->ms_setkey(crypto->cryspr_cb, ctx, sek, sek_len)) {
		HCRYPT_LOG(LOG_ERR, "cryspr setkey[%d](sek) failed\n", hcryptCtx_GetKeyIndex(ctx));
		return(-1);
	}
	memcpy(ctx->sek, sek, sek_len);
	ctx->sek_len = sek_len;

	HCRYPT_LOG(LOG_INFO, "updated context[%d]\n", hcryptCtx_GetKeyIndex(ctx));
	HCRYPT_PRINTKEY(ctx->sek, ctx->sek_len, "sek");
	ctx->status = HCRYPT_CTX_S_KEYED;
	return(0);
}

/* Parse Keying Material message */
int hcryptCtx_Rx_ParseKM(hcrypt_Session *crypto, unsigned char *km_msg, size_t msg_len)
{
	size_t sek_len, salt_len;
	unsigned char seks[HAICRYPT_KEY_MAX_SZ * 2];
	int sek_cnt;
	size_t kek_len = 0;
	hcrypt_Ctx *ctx;
	int do_pbkdf = 0;

	if (NULL == crypto) {
		HCRYPT_LOG(LOG_ERR, "Rx_ParseKM: invalid params: crypto=%p\n", crypto);
		return(-1);
	}

	/* Validate message content */
	{
		if (msg_len <= HCRYPT_MSG_KM_OFS_SALT) {
			HCRYPT_LOG(LOG_WARNING, "KMmsg length too small (%zd)\n", msg_len);
			return(-1);
		}
		salt_len = hcryptMsg_KM_GetSaltLen(km_msg);
		sek_len = hcryptMsg_KM_GetSekLen(km_msg);

		if ((salt_len > HAICRYPT_SALT_SZ)
		||	(sek_len > HAICRYPT_KEY_MAX_SZ)) {
			HCRYPT_LOG(LOG_WARNING, "%s", "KMmsg unsupported salt/key length\n");
			return(-1);
		}
		if ((16 != sek_len)
		&&  (24 != sek_len)
		&&  (32 != sek_len)) {
			HCRYPT_LOG(LOG_WARNING, "%s", "KMmsg unsupported key length\n");
			return(-1);
		}
		if (hcryptMsg_KM_HasBothSek(km_msg)) {
			sek_cnt = 2;
		} else {
			sek_cnt = 1;
		}
		if (msg_len != (HCRYPT_MSG_KM_OFS_SALT + salt_len + (sek_cnt * sek_len) + HAICRYPT_WRAPKEY_SIGN_SZ)) {
			HCRYPT_LOG(LOG_WARNING, "KMmsg length inconsistent (%zd,%zd,%zd)\n",
				salt_len, sek_len, msg_len);
			return(-1);
		}

		/* Check options support  */
		if ((HCRYPT_CIPHER_AES_CTR != km_msg[HCRYPT_MSG_KM_OFS_CIPHER])
		||  (HCRYPT_AUTH_NONE != km_msg[HCRYPT_MSG_KM_OFS_AUTH])) {
			HCRYPT_LOG(LOG_WARNING, "%s", "KMmsg unsupported option\n");
			return(-1);
		}

		if (crypto->se != km_msg[HCRYPT_MSG_KM_OFS_SE]) {
			HCRYPT_LOG(LOG_WARNING, "%s", "KMmsg invalid SE\n");
			return(-1);
		}

		/* Check KEKI here and pick right key */
		//>>todo
		/*
		 * We support no key exchange,
		 * KEK is preshared or derived from a passphrase
		 */
	}

	/* Pick the context updated by this KMmsg */
	if (hcryptMsg_KM_HasBothSek(km_msg) && (NULL != crypto->ctx)) {
		ctx = crypto->ctx->alt; /* 2 SEK KM, start with inactive ctx */
	} else {
		ctx = &crypto->ctx_pair[hcryptMsg_KM_GetKeyIndex(km_msg)];
	}
	if (NULL == ctx) {
		HCRYPT_LOG(LOG_WARNING, "%s", "KMmsg invalid flags (no SEK)\n");
		return(-1);
	}

	/* Check Salt and get if new */
	if ((salt_len != ctx->salt_len)
	||  (0 != memcmp(ctx->salt, &km_msg[HCRYPT_MSG_KM_OFS_SALT], salt_len))) {
		/* Salt changed (or 1st KMmsg received) */
		memcpy(ctx->salt, &km_msg[HCRYPT_MSG_KM_OFS_SALT], salt_len);
		ctx->salt_len = salt_len;
		do_pbkdf = 1; /* Impact on password derived kek */
	}

	/* Check SEK length and get if new */
	if (sek_len != ctx->sek_len) {
		/* Key length changed or 1st KMmsg received */
		ctx->sek_len = sek_len;
		do_pbkdf = 1; /* Impact on password derived kek */
	}

	/* 
	 * Regenerate KEK if it is password derived
	 * and Salt or SEK length changed
	 */
	if (ctx->cfg.pwd_len && do_pbkdf) {
		if (hcryptCtx_GenSecret(crypto, ctx)) {
			return(-1);
		}
		ctx->status = HCRYPT_CTX_S_SARDY;
		kek_len = sek_len;	/* KEK changed */
	}

	/* Unwrap SEK(s) and set in context */
	if (0 > crypto->cryspr->km_unwrap(crypto->cryspr_cb, seks,
		&km_msg[HCRYPT_MSG_KM_OFS_SALT + salt_len], 
		(sek_cnt * sek_len) + HAICRYPT_WRAPKEY_SIGN_SZ)) {
		HCRYPT_LOG(LOG_WARNING, "%s", "unwrap key failed\n");
		return(-2); //Report unmatched shared secret
	}
	/*
	 * First SEK in KMmsg is eSEK if both SEK present
	 */
	hcryptCtx_Rx_Rekey(crypto, ctx,
		((2 == sek_cnt) && (ctx->flags & HCRYPT_MSG_F_oSEK)) ? &seks[sek_len] : &seks[0],
		sek_len);

	/*
	 * Refresh KMmsg cache to detect Keying Material changes
	 */
	ctx->KMmsg_len = msg_len;
	memcpy(ctx->KMmsg_cache, km_msg, msg_len);

	/* update other (alternate) context if both SEK provided */
	if (2 == sek_cnt) {
		hcrypt_Ctx *alt = ctx->alt;

		memcpy(alt->salt, &km_msg[HCRYPT_MSG_KM_OFS_SALT], salt_len);
		alt->salt_len = salt_len;

		if (kek_len) { /* New or changed KEK */
//			memcpy(&alt->aes_kek, &ctx->aes_kek, sizeof(alt->aes_kek));
			alt->status = HCRYPT_CTX_S_SARDY;
		}

		hcryptCtx_Rx_Rekey(crypto, alt,
			((2 == sek_cnt) && (alt->flags & HCRYPT_MSG_F_oSEK)) ? &seks[sek_len] : &seks[0],
			sek_len);

		alt->KMmsg_len = msg_len;
		memcpy(alt->KMmsg_cache, km_msg, msg_len);
	}
	return(0);
}
