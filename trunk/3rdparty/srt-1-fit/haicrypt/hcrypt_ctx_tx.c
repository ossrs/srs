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

#include <string.h>		/* memcpy */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <win/wintime.h>
#else
    #include <sys/time.h>
#endif
#include "hcrypt.h"

int hcryptCtx_Tx_Init(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const HaiCrypt_Cfg *cfg)
{
	ctx->cfg.key_len = cfg->key_len;

	ctx->mode = HCRYPT_CTX_MODE_AESCTR;
	ctx->status = HCRYPT_CTX_S_INIT;

	ctx->msg_info = crypto->msg_info;

	if (hcryptCtx_SetSecret(crypto, ctx, &cfg->secret)) {
		return(-1);
	}
	return(0);
}

int hcryptCtx_Tx_Rekey(hcrypt_Session *crypto, hcrypt_Ctx *ctx)
{
	int iret;

	ASSERT(HCRYPT_CTX_S_SARDY <= ctx->status);

	/* Generate Salt */
	ctx->salt_len = HAICRYPT_SALT_SZ;
	if (0 > (iret = crypto->cryspr->prng(ctx->salt, ctx->salt_len))) {
		HCRYPT_LOG(LOG_ERR, "PRNG(salt[%zd]) failed\n", ctx->salt_len);
		return(iret);
	}

	/* Generate SEK */
	ctx->sek_len = ctx->cfg.key_len;
	if (0 > (iret = crypto->cryspr->prng(ctx->sek, ctx->sek_len))) {
		HCRYPT_LOG(LOG_ERR, "PRNG(sek[%zd] failed\n", ctx->sek_len);
		return(iret);
	}

	/* Set SEK in cryspr */
	if (crypto->cryspr->ms_setkey(crypto->cryspr_cb, ctx, ctx->sek, ctx->sek_len)) {
		HCRYPT_LOG(LOG_ERR, "cryspr setkey(sek[%zd]) failed\n", ctx->sek_len);
		return(-1);
	}

	HCRYPT_LOG(LOG_NOTICE, "rekeyed crypto context[%d]\n", (ctx->flags & HCRYPT_CTX_F_xSEK)/2);
	HCRYPT_PRINTKEY(ctx->sek, ctx->sek_len, "sek");

	/* Regenerate KEK if Password-based (uses newly generated salt and sek_len) */
	if ((0 < ctx->cfg.pwd_len)
	&&	(0 > (iret = hcryptCtx_GenSecret(crypto, ctx)))) {
		return(iret);
	}

	/* Assemble the new Keying Material message */
	if (0 != (iret = hcryptCtx_Tx_AsmKM(crypto, ctx, NULL))) {
		return(iret);
	}
	if ((HCRYPT_CTX_S_KEYED <= ctx->alt->status)
	&&  hcryptMsg_KM_HasBothSek(ctx->alt->KMmsg_cache)) {
		/* 
		 * previous context KM announced in alternate (odd/even) KM, 
		 * reassemble it without our KM
		*/
		hcryptCtx_Tx_AsmKM(crypto, ctx->alt, NULL);
	}

	/* Initialize the Media Stream message prefix cache */
	ctx->msg_info->resetCache(ctx->MSpfx_cache, HCRYPT_MSG_PT_MS, ctx->flags & HCRYPT_CTX_F_xSEK);
	ctx->pkt_cnt = 1;

	ctx->status = HCRYPT_CTX_S_KEYED;
	return(0);
}

int hcryptCtx_Tx_CloneKey(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const hcrypt_Session* cryptoSrc)
{
	int iret;

	ASSERT(HCRYPT_CTX_S_SARDY <= ctx->status);

    const hcrypt_Ctx* ctxSrc = cryptoSrc->ctx;
    if (!ctxSrc)
    {
        /* Probbly the context is not yet completely initialized, so
         * use blindly the first context from the pair
         */
        ctxSrc = &cryptoSrc->ctx_pair[0];
    }

    /* Copy SALT (instead of generating) */
    ctx->salt_len = ctxSrc->salt_len;
    memcpy(ctx->salt, ctxSrc->salt, ctx->salt_len);

    /* Copy SEK */
    ctx->sek_len = ctxSrc->sek_len;
    memcpy(ctx->sek, ctxSrc->sek, ctx->sek_len);

	/* Set SEK in cryspr */
	if (crypto->cryspr->ms_setkey(crypto->cryspr_cb, ctx, ctx->sek, ctx->sek_len)) {
		HCRYPT_LOG(LOG_ERR, "cryspr setkey(sek[%zd]) failed\n", ctx->sek_len);
		return(-1);
	}

	HCRYPT_LOG(LOG_NOTICE, "clone-keyed crypto context[%d]\n", (ctx->flags & HCRYPT_CTX_F_xSEK)/2);
	HCRYPT_PRINTKEY(ctx->sek, ctx->sek_len, "sek");

	/* Regenerate KEK if Password-based (uses newly generated salt and sek_len) */
    /* (note for CloneKey imp: it's expected that the same passphrase-salt pair
       shall generate the same KEK. GenSecret also prints the KEK */
	if ((0 < ctx->cfg.pwd_len)
    &&	(0 > (iret = hcryptCtx_GenSecret(crypto, ctx)))) {
		return(iret);
	}

	/* Assemble the new Keying Material message */
	if (0 != (iret = hcryptCtx_Tx_AsmKM(crypto, ctx, NULL))) {
		return(iret);
	}
	if ((HCRYPT_CTX_S_KEYED <= ctx->alt->status)
	&&  hcryptMsg_KM_HasBothSek(ctx->alt->KMmsg_cache)) {
		/* 
		 * previous context KM announced in alternate (odd/even) KM, 
		 * reassemble it without our KM
		*/
		hcryptCtx_Tx_AsmKM(crypto, ctx->alt, NULL);
	}

	/* Initialize the Media Stream message prefix cache */
	ctx->msg_info->resetCache(ctx->MSpfx_cache, HCRYPT_MSG_PT_MS, ctx->flags & HCRYPT_CTX_F_xSEK);
	ctx->pkt_cnt = 1;

	ctx->status = HCRYPT_CTX_S_KEYED;
	return(0);
}

/* 
 * Refresh the alternate context from the current.
 * Regenerates the SEK but keep the salt, doing so also
 * preserve the KEK generated from secret password and salt.
 */

int hcryptCtx_Tx_Refresh(hcrypt_Session *crypto)
{
	hcrypt_Ctx *ctx = crypto->ctx;
	hcrypt_Ctx *new_ctx;
	int iret;

	ASSERT(NULL != ctx);
	ASSERT(HCRYPT_CTX_S_ACTIVE == ctx->status);

	/* Pick the alternative (inactive) context */
	new_ctx = ctx->alt;

	ASSERT(HCRYPT_CTX_S_SARDY <= new_ctx->status);

	/* Keep same KEK, configuration, and salt */
//	memcpy(&new_ctx->aes_kek, &ctx->aes_kek, sizeof(new_ctx->aes_kek));
	memcpy(&new_ctx->cfg, &ctx->cfg, sizeof(new_ctx->cfg));

	new_ctx->salt_len = ctx->salt_len;
	memcpy(new_ctx->salt, ctx->salt, HAICRYPT_SALT_SZ);

	/* Generate new SEK */
	new_ctx->sek_len = new_ctx->cfg.key_len;

	HCRYPT_LOG(LOG_DEBUG, "refresh/generate SEK. salt_len=%d sek_len=%d\n", (int)new_ctx->salt_len, (int)new_ctx->sek_len);

	if (0 > crypto->cryspr->prng(new_ctx->sek, new_ctx->sek_len)) {
		HCRYPT_LOG(LOG_ERR, "PRNG(sek[%zd] failed\n", new_ctx->sek_len);
		return(-1);
	}
	/* Cryspr's dependent key */
	if (crypto->cryspr->ms_setkey(crypto->cryspr_cb, new_ctx, new_ctx->sek, new_ctx->sek_len)) {
		HCRYPT_LOG(LOG_ERR, "refresh cryspr setkey(sek[%d]) failed\n", new_ctx->sek_len);
		return(-1);
	}

	HCRYPT_PRINTKEY(new_ctx->sek, new_ctx->sek_len, "sek");

	/* Assemble the new KMmsg with new and current SEK */
	if (0 != (iret = hcryptCtx_Tx_AsmKM(crypto, new_ctx, ctx->sek))) {
		return(iret);
	}

	/* Initialize the message prefix cache */
	new_ctx->msg_info->resetCache(new_ctx->MSpfx_cache, HCRYPT_MSG_PT_MS, new_ctx->flags & HCRYPT_MSG_F_xSEK);
	new_ctx->pkt_cnt = 0;

	new_ctx->status = HCRYPT_CTX_S_KEYED;
	return(0);
}

/* 
 * Prepare context switch
 * both odd & even keys announced
 */
int hcryptCtx_Tx_PreSwitch(hcrypt_Session *crypto)
{
	hcrypt_Ctx *ctx = crypto->ctx;

	ASSERT(NULL != ctx);
	ASSERT(HCRYPT_CTX_S_ACTIVE == ctx->status);
	ASSERT(HCRYPT_CTX_S_KEYED == ctx->alt->status);

	ctx->alt->flags |= HCRYPT_CTX_F_ANNOUNCE;
	ctx->alt->flags |= HCRYPT_CTX_F_TTSEND; //Send now

	/* Stop announcing current context if next one contains its key */
	if (hcryptMsg_KM_HasBothSek(ctx->alt->KMmsg_cache)) {
		ctx->flags &= ~HCRYPT_CTX_F_ANNOUNCE;
	}
	return(0);
}

int hcryptCtx_Tx_Switch(hcrypt_Session *crypto)
{
	hcrypt_Ctx *ctx = crypto->ctx;

	ASSERT(HCRYPT_CTX_S_KEYED <= ctx->alt->status);

	ctx->status = HCRYPT_CTX_S_DEPRECATED;
	ctx->alt->status = HCRYPT_CTX_S_ACTIVE;

	ctx->alt->flags |= HCRYPT_CTX_F_ANNOUNCE;	// Already cleared if new KM has both SEK
	crypto->ctx = ctx->alt;
	return(0);
}

int hcryptCtx_Tx_PostSwitch(hcrypt_Session *crypto)
{
	hcrypt_Ctx *ctx = crypto->ctx;
	hcrypt_Ctx *old_ctx = ctx->alt;

	/* Stop announcing old context (if announced) */
	old_ctx->flags &= ~HCRYPT_CTX_F_ANNOUNCE;
	old_ctx->status = HCRYPT_CTX_S_SARDY;

	/* If current context KM announce both, reassemble it */
	if (hcryptMsg_KM_HasBothSek(ctx->KMmsg_cache)) {
		hcryptCtx_Tx_AsmKM(crypto, ctx, NULL);
	}
	return(0);
}

/* Assemble Keying Material message */
int hcryptCtx_Tx_AsmKM(hcrypt_Session *crypto, hcrypt_Ctx *ctx, unsigned char *alt_sek)
{
	unsigned char *km_msg;
	size_t msg_len;
	int sek_cnt = (NULL == alt_sek ? 1 : 2);
	unsigned char sek_buf[HAICRYPT_KEY_MAX_SZ * 2];
	unsigned char *seks;

	if (NULL == ctx) {
		HCRYPT_LOG(LOG_ERR, "%s", "crypto context undefined\n");
		return(-1);
	}

	msg_len = HCRYPT_MSG_KM_OFS_SALT
		+ ctx->salt_len
		+ (ctx->sek_len * sek_cnt)
		+ HAICRYPT_WRAPKEY_SIGN_SZ;

	km_msg = &ctx->KMmsg_cache[0];
	ctx->KMmsg_len = 0;

	memset(km_msg, 0, msg_len);
	ctx->msg_info->resetCache(km_msg, HCRYPT_MSG_PT_KM, 
		2 == sek_cnt ? HCRYPT_MSG_F_xSEK : (ctx->flags & HCRYPT_MSG_F_xSEK));

	/* crypto->KMmsg_cache[4..7]: KEKI=0 */
	km_msg[HCRYPT_MSG_KM_OFS_CIPHER] = HCRYPT_CIPHER_AES_CTR;
	km_msg[HCRYPT_MSG_KM_OFS_AUTH] = HCRYPT_AUTH_NONE;
	km_msg[HCRYPT_MSG_KM_OFS_SE] = crypto->se;
	hcryptMsg_KM_SetSaltLen(km_msg, ctx->salt_len);
	hcryptMsg_KM_SetSekLen(km_msg, ctx->sek_len);

	memcpy(&km_msg[HCRYPT_MSG_KM_OFS_SALT], ctx->salt, ctx->salt_len);

	if (2 == sek_cnt) {
		/* Even SEK first in dual SEK KMmsg */
		if (HCRYPT_MSG_F_eSEK & ctx->flags) {
			memcpy(&sek_buf[0], ctx->sek, ctx->sek_len);
			memcpy(&sek_buf[ctx->sek_len], alt_sek, ctx->sek_len);
		} else {
			memcpy(&sek_buf[0], alt_sek, ctx->sek_len);
			memcpy(&sek_buf[ctx->sek_len], ctx->sek, ctx->sek_len);
		}
		seks = sek_buf;
	} else {
		seks = ctx->sek;
	}
	if (0 > crypto->cryspr->km_wrap(crypto->cryspr_cb,
		&km_msg[HCRYPT_MSG_KM_OFS_SALT + ctx->salt_len],
		seks, sek_cnt * ctx->sek_len)) {

		HCRYPT_LOG(LOG_ERR, "%s", "wrap key failed\n");
		return(-1);
	}
	ctx->KMmsg_len = msg_len;
	return(0);
}

int hcryptCtx_Tx_ManageKM(hcrypt_Session *crypto)
{
	hcrypt_Ctx *ctx = crypto->ctx;

	ASSERT(NULL != ctx);

	HCRYPT_LOG(LOG_DEBUG, "KM[%d] KEY STATUS: pkt_cnt=%u against ref.rate=%u and pre.announce=%u\n",
                          (ctx->alt->flags & HCRYPT_CTX_F_xSEK)/2,
                          ctx->pkt_cnt, crypto->km.refresh_rate, crypto->km.pre_announce);

	if ((ctx->pkt_cnt > crypto->km.refresh_rate)
	||  (ctx->pkt_cnt == 0)) {	//rolled over
		/* 
		 * End of crypto period for current SEK, 
		 * switch to other (even/odd) SEK
		 */
		HCRYPT_LOG(LOG_INFO, "KM[%d] Activated\n",
			(ctx->alt->flags & HCRYPT_CTX_F_xSEK)/2);

		hcryptCtx_Tx_Switch(crypto);

	} else 
	if ((ctx->pkt_cnt > (crypto->km.refresh_rate - crypto->km.pre_announce))
	&&  !(ctx->alt->flags & HCRYPT_CTX_F_ANNOUNCE)) {
		/* 
		 * End of crypto period approach for this SEK,
		 * prepare next SEK for announcement
		 */
		hcryptCtx_Tx_Refresh(crypto);

		HCRYPT_LOG(LOG_INFO, "KM[%d] Pre-announced\n",
			(ctx->alt->flags & HCRYPT_CTX_F_xSEK)/2);

		hcryptCtx_Tx_PreSwitch(crypto);

	} else
	if ((ctx->alt->status == HCRYPT_CTX_S_DEPRECATED)
	&&  (ctx->pkt_cnt > crypto->km.pre_announce)) {
		/* 
		 * Deprecated SEK is no longer needed (for late packets),
		 * decommission it
		 */
		HCRYPT_LOG(LOG_INFO, "KM[%d] Deprecated\n",
			(ctx->alt->flags & HCRYPT_CTX_F_xSEK)/2);

		hcryptCtx_Tx_PostSwitch(crypto);
	}

	/* Check if it is time to send Keying Material */
	if (timerisset(&crypto->km.tx_period)) { /* tx_period=0.0 -> out-of-stream Keying Material distribution */
		struct timeval now, nxt_tx;

		gettimeofday(&now, NULL);
		timeradd(&crypto->km.tx_last, &crypto->km.tx_period, &nxt_tx);
		if (timercmp(&now, &nxt_tx, >)) {
			if (crypto->ctx_pair[0].flags & HCRYPT_CTX_F_ANNOUNCE) crypto->ctx_pair[0].flags |= HCRYPT_CTX_F_TTSEND;
			if (crypto->ctx_pair[1].flags & HCRYPT_CTX_F_ANNOUNCE) crypto->ctx_pair[1].flags |= HCRYPT_CTX_F_TTSEND;
		}
	}

	return(0);
}

int hcryptCtx_Tx_InjectKM(hcrypt_Session *crypto,
	void *out_p[], size_t out_len_p[], int maxout ATR_UNUSED)
{
	int i, nbout = 0;

	ASSERT(maxout >= 2);
	for (i=0; i<2; i++) {
		if (crypto->ctx_pair[i].flags & HCRYPT_CTX_F_TTSEND) { /* Time To Send */
			HCRYPT_LOG(LOG_DEBUG, "Send KMmsg[%d] len=%zd\n", i, 
				crypto->ctx_pair[i].KMmsg_len); 
			/* Send Keying Material */
			out_p[nbout] = crypto->ctx_pair[i].KMmsg_cache;
			out_len_p[nbout] = crypto->ctx_pair[i].KMmsg_len;
			nbout++;
			crypto->ctx_pair[i].flags &= ~HCRYPT_CTX_F_TTSEND;
		}
	}
	if (nbout) {
		struct timeval now;
		gettimeofday(&now, NULL);
		crypto->km.tx_last = now;
	}
	return(nbout);
}


