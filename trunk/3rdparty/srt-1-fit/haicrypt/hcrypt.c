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

#include <stdio.h>	/* snprintf */
#include <stdlib.h>	/* NULL, malloc, free */
#include <string.h>	/* memcpy, memset */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/time.h>	/* timerclear */
#endif

#include "hcrypt.h"

#if ENABLE_HAICRYPT_LOGGING
void HaiCrypt_DumpConfig(const HaiCrypt_Cfg* cfg);
#else
#define HaiCrypt_DumpConfig(x) (void)0
#endif


static hcrypt_Session* sHaiCrypt_PrepareHandle(const HaiCrypt_Cfg* cfg, HaiCrypt_CryptoDir tx)
{
    hcrypt_Session *crypto;
    unsigned char *mem_buf;
    size_t mem_siz, inbuf_siz;

    HaiCrypt_DumpConfig(cfg);

    HCRYPT_PRINTKEY(cfg->secret.str, cfg->secret.len, "cfgkey");

    inbuf_siz = 0;
    inbuf_siz = hcryptMsg_PaddedLen(cfg->data_max_len, 128/8);

    /* Allocate crypto session control struct */
    mem_siz = sizeof(hcrypt_Session)	// structure
        + inbuf_siz;

    crypto = malloc(mem_siz);
    if (NULL == crypto) {
        HCRYPT_LOG(LOG_ERR, "%s\n", "malloc failed");
        return NULL;
    }
    mem_buf = (unsigned char *)crypto;
    mem_buf += sizeof(*crypto);
    memset(crypto, 0, sizeof(*crypto));

    if (inbuf_siz) {
        crypto->inbuf = mem_buf;
        crypto->inbuf_siz = inbuf_siz;
    }

    crypto->cryspr = cfg->cryspr;
    crypto->cfg.data_max_len = cfg->data_max_len;

    /* Setup transport packet info */
    switch (cfg->xport) {
    case HAICRYPT_XPT_STANDALONE:
        crypto->se = HCRYPT_SE_TSUDP;
        crypto->msg_info = hcryptMsg_STA_MsgInfo();
        break;
    case HAICRYPT_XPT_SRT:
        crypto->se = HCRYPT_SE_TSSRT;
        crypto->msg_info = hcryptMsg_SRT_MsgInfo();
        break;
    default:
        HCRYPT_LOG(LOG_ERR, "invalid xport: %d\n", cfg->xport);
        free(crypto);
        return NULL;
    }

    timerclear(&crypto->km.tx_last);
    crypto->km.tx_period.tv_sec  = cfg->km_tx_period_ms / 1000;
    crypto->km.tx_period.tv_usec = (cfg->km_tx_period_ms % 1000) * 1000;

    crypto->km.refresh_rate = cfg->km_refresh_rate_pkt;
    crypto->km.pre_announce = cfg->km_pre_announce_pkt;

    /* Indentify each context */
    crypto->ctx_pair[0].flags = HCRYPT_MSG_F_eSEK | (tx ? HCRYPT_CTX_F_ENCRYPT : 0);
    crypto->ctx_pair[1].flags = HCRYPT_MSG_F_oSEK | (tx ? HCRYPT_CTX_F_ENCRYPT : 0);
    /* Point to each other */
    crypto->ctx_pair[0].alt = &crypto->ctx_pair[1];
    crypto->ctx_pair[1].alt = &crypto->ctx_pair[0];

    crypto->cryspr_cb = crypto->cryspr->open(crypto->cryspr, cfg->data_max_len);
    if (NULL == crypto->cryspr_cb) {
        free(crypto);
        return NULL;
    }

    return crypto;
}

int HaiCrypt_Create(const HaiCrypt_Cfg *cfg, HaiCrypt_Handle *phhc)
{
    ASSERT(cfg != NULL);
    ASSERT(phhc != NULL);

    hcrypt_Session *crypto;
    HaiCrypt_CryptoDir tx = (HaiCrypt_CryptoDir)(HAICRYPT_CFG_F_TX & cfg->flags);

    *phhc = NULL;

    HCRYPT_LOG_INIT();
    //Test log
    HCRYPT_LOG(LOG_INFO, "creating crypto context(flags=0x%x)\n", cfg->flags);

    if (!(HAICRYPT_CFG_F_CRYPTO & cfg->flags)) {
        HCRYPT_LOG(LOG_INFO, "no supported flags set (0x%x)\n", cfg->flags);
        return(-1);
    } else if ((16 != cfg->key_len)	/* SEK length */
            &&  (24 != cfg->key_len)
            &&  (32 != cfg->key_len)) {
        HCRYPT_LOG(LOG_ERR, "invalid key length (%d). Expected: 16, 24, 32\n", (int)cfg->key_len);
        return(-1);
    } else if ((HAICRYPT_SECTYP_PASSPHRASE == cfg->secret.typ)
            &&  ((0 == cfg->secret.len) || (sizeof(cfg->secret.str) < cfg->secret.len))) { /* KEK length */
        HCRYPT_LOG(LOG_ERR, "invalid secret passphrase length (%d)\n", (int)cfg->secret.len);
        return(-1);
    } else if ((HAICRYPT_SECTYP_PRESHARED == cfg->secret.typ)
            &&  (cfg->key_len > cfg->secret.len)) {
        HCRYPT_LOG(LOG_ERR, "preshared secret length (%d) smaller than key length (%d)\n", 
                (int)cfg->secret.len, (int)cfg->key_len);
        return(-1);
    } else if (NULL == cfg->cryspr) {
        HCRYPT_LOG(LOG_ERR, "%s\n", "no cryspr specified");
        return(-1);
    } else if (0 == cfg->data_max_len) {
        HCRYPT_LOG(LOG_ERR, "%s\n", "no data_max_len specified");
        return(-1);
    }

    crypto = sHaiCrypt_PrepareHandle(cfg, tx);
    if (!crypto)
        return -1;

    if (tx) { /* Encoder */
        /* Configure initial context */
        if (hcryptCtx_Tx_Init(crypto, &crypto->ctx_pair[0], cfg)
                ||  hcryptCtx_Tx_Init(crypto, &crypto->ctx_pair[1], cfg)) {
            free(crypto);
            return(-1);
        }			
        /* Generate keys for first (default) context */
        if (hcryptCtx_Tx_Rekey(crypto, &crypto->ctx_pair[0])) {
            free(crypto);
            return(-1);
        }
        crypto->ctx = &crypto->ctx_pair[0];
        crypto->ctx->flags |= (HCRYPT_CTX_F_ANNOUNCE | HCRYPT_CTX_F_TTSEND);
        crypto->ctx->status = HCRYPT_CTX_S_ACTIVE;
    } else { /* Decoder */
        /* Configure contexts */
        if (hcryptCtx_Rx_Init(crypto, &crypto->ctx_pair[0], cfg)
                ||  hcryptCtx_Rx_Init(crypto, &crypto->ctx_pair[1], cfg)) {
            free(crypto);
            return(-1);
        }
    }

    *phhc = (void *)crypto;
    return(0);
}

int HaiCrypt_ExtractConfig(HaiCrypt_Handle hhcSrc, HaiCrypt_Cfg* pcfg)
{
    hcrypt_Session *crypto = (hcrypt_Session *)hhcSrc;
    hcrypt_Ctx* ctx = crypto->ctx;
    if (!ctx)
    {
        // Fall  back to the first of the pair;
        // Should this be not initialized, ignore it.
        ctx = &crypto->ctx_pair[0];

        // We assume that when ctx != NULL, it is active or keyed anyway.
        if (ctx->status != HCRYPT_CTX_S_KEYED && ctx->status != HCRYPT_CTX_S_ACTIVE)
            return -1;
    }

    pcfg->flags = HAICRYPT_CFG_F_CRYPTO;
    if ((ctx->flags & HCRYPT_CTX_F_ENCRYPT) == HCRYPT_CTX_F_ENCRYPT)
        pcfg->flags |= HAICRYPT_CFG_F_TX;

    /* Set this explicitly - this use of this library is SRT only. */
    pcfg->xport = HAICRYPT_XPT_SRT;
    pcfg->cryspr = crypto->cryspr;
    pcfg->key_len = ctx->cfg.key_len;
    if (pcfg->key_len == 0) // not initialized - usual in RX
    {
        pcfg->key_len = ctx->sek_len;
    }
    pcfg->data_max_len = crypto->cfg.data_max_len;
    pcfg->km_tx_period_ms = 0;//No HaiCrypt KM inject period, handled in SRT;

    pcfg->km_refresh_rate_pkt = crypto->km.refresh_rate;
    pcfg->km_pre_announce_pkt = crypto->km.pre_announce;

    /* As SRT is using only the PASSPHRASE type, never PRESHARED,
     * this is so assumed here, although there are completely no
     * premises as to which is currently used by the hhcSrc.
     */
    pcfg->secret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    pcfg->secret.len = ctx->cfg.pwd_len;
    memcpy(pcfg->secret.str, ctx->cfg.pwd, pcfg->secret.len);

    return 0;
}

int HaiCrypt_Clone(HaiCrypt_Handle hhcSrc, HaiCrypt_CryptoDir tx, HaiCrypt_Handle *phhc)
{
    hcrypt_Session *cryptoSrc = (hcrypt_Session *)hhcSrc;
    hcrypt_Session *cryptoClone;
    unsigned char *mem_buf;
    size_t mem_siz, inbuf_siz;

    *phhc = NULL;

    ASSERT(NULL != hhcSrc);

    HCRYPT_LOG(LOG_INFO, "%s\n", "creating CLONED crypto context");

    if (tx) {
        HaiCrypt_Cfg crypto_config;
        HaiCrypt_ExtractConfig(hhcSrc, &crypto_config);

        /*
         * Just invert the direction written in flags and use the
         * standard way of creating the context, as you already have a config.
         */
        crypto_config.flags |= HAICRYPT_CFG_F_TX;
        cryptoClone = sHaiCrypt_PrepareHandle(&crypto_config, tx);
        if (!cryptoClone)
            return -1;

        /* Configure initial context */
        if (hcryptCtx_Tx_Init(cryptoClone, &cryptoClone->ctx_pair[0], &crypto_config)
                ||  hcryptCtx_Tx_Init(cryptoClone, &cryptoClone->ctx_pair[1], &crypto_config)) {
            free(cryptoClone);
            return(-1);
        }			
        /* Clone keys for first (default) context from the source RX crypto */
        if (hcryptCtx_Tx_CloneKey(cryptoClone, &cryptoClone->ctx_pair[0], cryptoSrc)) {
            free(cryptoClone);
            return(-1);
        }
        cryptoClone->ctx = &cryptoClone->ctx_pair[0];
        cryptoClone->ctx->flags |= (HCRYPT_CTX_F_ANNOUNCE | HCRYPT_CTX_F_TTSEND);
        cryptoClone->ctx->status = HCRYPT_CTX_S_ACTIVE;

    } else { /* Receiver */

        /* 
         * If cryspr has no special input buffer alignment requirement,
         * handle it in the crypto session.
         */
        inbuf_siz = cryptoSrc->inbuf_siz ;

        /* Allocate crypto session control struct */
        mem_siz = sizeof(hcrypt_Session)	// structure
            + inbuf_siz;

        cryptoClone = malloc(mem_siz);
        if (NULL == cryptoClone) {
            HCRYPT_LOG(LOG_ERR, "%s\n", "malloc failed");
            return(-1);
        }
        mem_buf = (unsigned char *)cryptoClone;
        mem_buf += sizeof(*cryptoClone);
        memcpy(cryptoClone, cryptoSrc, sizeof(*cryptoClone));

        if (inbuf_siz) {
            cryptoClone->inbuf = mem_buf;
            mem_buf += inbuf_siz;
        }
        timerclear(&cryptoClone->km.tx_last);

        /* Adjust pointers  pointing into cryproSrc after copy
           msg_info and crysprs are extern statics so this is ok*/
        cryptoClone->ctx_pair[0].alt = &cryptoClone->ctx_pair[1];
        cryptoClone->ctx_pair[1].alt = &cryptoClone->ctx_pair[0];

        /* create a new cryspr (OpenSSL) context */
        cryptoClone->cryspr_cb = cryptoClone->cryspr->open(cryptoClone->cryspr, cryptoClone->cfg.data_max_len);
        if (NULL == cryptoClone->cryspr_cb) {
            //shred
            free(cryptoClone);
            return(-1);
        }


        /* Configure contexts */
        if (hcryptCtx_Rx_Init(cryptoClone, &cryptoClone->ctx_pair[0], NULL)
                ||  hcryptCtx_Rx_Init(cryptoClone, &cryptoClone->ctx_pair[1], NULL)) {
            free(cryptoClone);
            return(-1);
        }

        /* Clear salt to force later regeneration of KEK as AES decrypting key,
           copyed one is encrypting key */
        cryptoClone->ctx_pair[0].flags &= ~HCRYPT_CTX_F_ENCRYPT;
        cryptoClone->ctx_pair[1].flags &= ~HCRYPT_CTX_F_ENCRYPT;
        memset(cryptoClone->ctx_pair[0].salt, 0, sizeof(cryptoClone->ctx_pair[0].salt));
        cryptoClone->ctx_pair[0].salt_len = 0;
    }

    *phhc = (void *)cryptoClone;
    return(0);
}

int HaiCrypt_Close(HaiCrypt_Handle hhc)
{
    hcrypt_Session *crypto = (hcrypt_Session *)hhc;
    int rc = -1;

    if (crypto) {
        if (crypto->cryspr && crypto->cryspr->close) crypto->cryspr->close(crypto->cryspr_cb);
        free(crypto);
        rc = 0;
    }
    HCRYPT_LOG_EXIT();
    return rc;
}
