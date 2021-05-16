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

#include <stdlib.h>				/* NULL */
#include <string.h>				/* memcmp */
#include "hcrypt.h"

int HaiCrypt_Rx_Data(HaiCrypt_Handle hhc, 
	unsigned char *in_pfx, unsigned char *data, size_t data_len)
{
	hcrypt_Session *crypto = (hcrypt_Session *)hhc;
	hcrypt_Ctx *ctx;
	int nb = -1;

	if ((NULL == crypto)
	||  (NULL == data)) {
		HCRYPT_LOG(LOG_ERR, "%s", "invalid parameters\n");
		return(nb);
	}

	ctx = &crypto->ctx_pair[hcryptMsg_GetKeyIndex(crypto->msg_info, in_pfx)];

	ASSERT(NULL != ctx); /* Header check should prevent this error */
	ASSERT(NULL != crypto->cryspr); /* Header check should prevent this error */

	crypto->ctx = ctx; /* Context of last received msg */
	if (NULL == crypto->cryspr->ms_decrypt) {
		HCRYPT_LOG(LOG_ERR, "%s", "cryspr had no decryptor\n");
	} else if (ctx->status >= HCRYPT_CTX_S_KEYED) {
		hcrypt_DataDesc indata;
		indata.pfx      = in_pfx;
		indata.payload  = data;
		indata.len      = data_len;

		if (0 > (nb = crypto->cryspr->ms_decrypt(crypto->cryspr_cb, ctx, &indata, 1, NULL, NULL, NULL))) {
			HCRYPT_LOG(LOG_ERR, "%s", "ms_decrypt failed\n");
		} else {
			nb = indata.len;
		}
	} else { /* No key received yet */
		nb = 0;
	}
	return(nb);
}

int HaiCrypt_Rx_Process(HaiCrypt_Handle hhc, 
	unsigned char *in_msg, size_t in_len, 
	void *out_p[], size_t out_len_p[], int maxout)
{
	hcrypt_Session *crypto = (hcrypt_Session *)hhc;
	hcrypt_Ctx *ctx;
	int nbout = maxout;
	int msg_type;

	if ((NULL == crypto)
	||  (NULL == in_msg)) {

		HCRYPT_LOG(LOG_ERR, "%s", "invalid parameters\n");
		return(-1);
	}

	/* Validate HaiCrypt message */
	if (0 > (msg_type = crypto->msg_info->parseMsg(in_msg))) {
		return(-1);
	}

	switch(msg_type) {
	case HCRYPT_MSG_PT_MS: /* MSmsg */
		ctx = &crypto->ctx_pair[hcryptMsg_GetKeyIndex(crypto->msg_info, in_msg)];

		if ((NULL == out_p)
		||  (NULL == out_len_p)) {
			HCRYPT_LOG(LOG_ERR, "%s", "invalid parameters\n");
			return(-1);
		}
		ASSERT(NULL != ctx); /* Header check should prevent this error */
		ASSERT(NULL != crypto->cryspr); /* Header check should prevent this error */

		crypto->ctx = ctx; /* Context of last received msg */
		if (NULL == crypto->cryspr->ms_decrypt) {
			HCRYPT_LOG(LOG_ERR, "%s", "cryspr had no decryptor\n");
			nbout = -1;
		} else if (ctx->status >= HCRYPT_CTX_S_KEYED) {
			hcrypt_DataDesc indata;
			indata.pfx      = in_msg;
			indata.payload  = &in_msg[crypto->msg_info->pfx_len];
			indata.len      = in_len - crypto->msg_info->pfx_len;

			if (crypto->cryspr->ms_decrypt(crypto->cryspr_cb, ctx, &indata, 1, out_p, out_len_p, &nbout)) {
				HCRYPT_LOG(LOG_ERR, "%s", "ms_decrypt failed\n");
				nbout = -1;
			}
		} else { /* No key received yet */
			nbout = 0;
		}
		break;

	case HCRYPT_MSG_PT_KM: /* KMmsg */
		/* Even or Both SEKs check with even context */
		ctx = &crypto->ctx_pair[hcryptMsg_GetKeyIndex(crypto->msg_info, in_msg)];

		ASSERT(NULL != ctx); /* Header check should prevent this error */

		if ((ctx->status < HCRYPT_CTX_S_KEYED)	/* No key deciphered yet */
		||  (in_len != ctx->KMmsg_len) 				/* or not same size */
		||  (0 != memcmp(ctx->KMmsg_cache, in_msg, in_len))) { /* or different */

			nbout = hcryptCtx_Rx_ParseKM(crypto, in_msg, in_len);
			//-2: unmatched shared secret
			//-1: other failures
			//0: success
		} else {
			nbout = 0;
		}
		if (NULL != out_p) out_p[0] = NULL;
		if (NULL != out_len_p) out_len_p[0] = 0;
		break;

	default:
		HCRYPT_LOG(LOG_WARNING, "%s", "unknown packet type\n");
		nbout = 0;
		break;
	}

	return(nbout);
}


