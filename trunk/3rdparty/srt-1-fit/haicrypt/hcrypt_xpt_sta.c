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

#include <string.h>			/* memset, memcpy */
#include <time.h>           /* time() */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>		/* htonl, ntohl */
#endif
#include "hcrypt.h"

/*
 *  HaiCrypt Standalone Transport Media Stream (MS) Data Msg Prefix:
 *  Cache maintained in network order
 *
 *      0                   1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 * 0x00 |0|Vers |   PT  |             Sign              |    resv   |KF |
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 * 0x04 |                              pki                              |
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 *      |                            payload...                         |
 */

/*
 *  HaiCrypt Standalone Transport Keying Material (KM) Msg (no prefix, use KM Msg directly):
 *  Cache maintained in network order
 *
 *      0                   1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 * 0x00 |0|Vers |   PT  |             Sign              |     resv      |
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 *      ...                                                             .
 */

#define HCRYPT_MSG_STA_HDR_SZ	    4
#define HCRYPT_MSG_STA_PKI_SZ	    4
#define HCRYPT_MSG_STA_PFX_SZ	    (HCRYPT_MSG_STA_HDR_SZ + HCRYPT_MSG_STA_PKI_SZ)

#define HCRYPT_MSG_STA_OFS_VERSION	HCRYPT_MSG_KM_OFS_VERSION
#define HCRYPT_MSG_STA_OFS_PT		HCRYPT_MSG_KM_OFS_PT
#define HCRYPT_MSG_STA_OFS_SIGN		HCRYPT_MSG_KM_OFS_SIGN
#define HCRYPT_MSG_STA_OFS_KFLGS	HCRYPT_MSG_KM_OFS_KFLGS

#define HCRYPT_MSG_STA_OFS_PKI      HCRYPT_MSG_STA_HDR_SZ

#define hcryptMsg_STA_GetVersion(msg)	(((msg)[HCRYPT_MSG_STA_OFS_VERSION]>>4)& 0xF)
#define hcryptMsg_STA_GetPktType(msg)	(((msg)[HCRYPT_MSG_STA_OFS_PT]) & 0xF)
#define hcryptMsg_STA_GetSign(msg)		(((msg)[HCRYPT_MSG_STA_OFS_SIGN]<<8) | (msg)[HCRYPT_MSG_STA_OFS_SIGN+1])

static hcrypt_MsgInfo _hcMsg_STA_MsgInfo;

static unsigned hcryptMsg_STA_GetKeyFlags(unsigned char *msg)
{
	return((unsigned)(msg[HCRYPT_MSG_STA_OFS_KFLGS] & HCRYPT_MSG_F_xSEK));
}

static hcrypt_Pki hcryptMsg_STA_GetPki(unsigned char *msg, int nwkorder)
{
	hcrypt_Pki pki;
	memcpy(&pki, &msg[HCRYPT_MSG_STA_OFS_PKI], sizeof(pki)); //header is in host order
	return (nwkorder ? pki : ntohl(pki));
}

static void hcryptMsg_STA_SetPki(unsigned char *msg, hcrypt_Pki pki)
{
	hcrypt_Pki nwk_pki = htonl(pki);
	memcpy(&msg[HCRYPT_MSG_STA_OFS_PKI], &nwk_pki, sizeof(nwk_pki)); //header is in host order
}

static void hcryptMsg_STA_ResetCache(unsigned char *pfx_cache, unsigned pkt_type, unsigned kflgs)
{
	pfx_cache[HCRYPT_MSG_STA_OFS_VERSION] = (unsigned char)((HCRYPT_MSG_VERSION << 4) | pkt_type); // version || PT
	pfx_cache[HCRYPT_MSG_STA_OFS_SIGN]    = (unsigned char)((HCRYPT_MSG_SIGN >> 8) & 0xFF); // Haivision PnP Mfr ID
	pfx_cache[HCRYPT_MSG_STA_OFS_SIGN+1]  = (unsigned char)(HCRYPT_MSG_SIGN & 0xFF);

	switch(pkt_type) {
	case HCRYPT_MSG_PT_MS:
		pfx_cache[HCRYPT_MSG_STA_OFS_KFLGS] = (unsigned char)kflgs; //HCRYPT_MSG_F_xxx
		hcryptMsg_STA_SetPki(pfx_cache, 0);
		break;
	case HCRYPT_MSG_PT_KM:
		pfx_cache[HCRYPT_MSG_KM_OFS_KFLGS] = (unsigned char)kflgs; //HCRYPT_MSG_F_xxx
		break;
	default:
		break;
	}
}

static void hcryptMsg_STA_IndexMsg(unsigned char *msg, unsigned char *pfx_cache)
{
	hcrypt_Pki pki = hcryptMsg_STA_GetPki(pfx_cache, 0); //Get in host order
	memcpy(msg, pfx_cache, HCRYPT_MSG_STA_PFX_SZ);
	hcryptMsg_SetPki(&_hcMsg_STA_MsgInfo, pfx_cache, ++pki);
}

static time_t _tLastLogTime = 0;

static int hcryptMsg_STA_ParseMsg(unsigned char *msg)
{
	int rc;

	if ((HCRYPT_MSG_VERSION != hcryptMsg_STA_GetVersion(msg))	/* Version 1 */
	||  (HCRYPT_MSG_SIGN != hcryptMsg_STA_GetSign(msg))) {		/* 'HAI' PnP Mfr ID */
		time_t tCurrentTime = time(NULL);
		// invalid data
		if ((tCurrentTime - _tLastLogTime) >= 2 || (0 == _tLastLogTime))
		{
			_tLastLogTime = tCurrentTime;
			HCRYPT_LOG(LOG_ERR, "invalid msg hdr: 0x%02x %02x%02x %02x\n",
				msg[0], msg[1], msg[2], msg[3]);
		}
		return(-1);	/* Invalid packet */
	}
	rc = hcryptMsg_STA_GetPktType(msg);
	switch(rc) {
	case HCRYPT_MSG_PT_MS:
		if (hcryptMsg_HasNoSek(&_hcMsg_STA_MsgInfo, msg)
		||  hcryptMsg_HasBothSek(&_hcMsg_STA_MsgInfo, msg)) {
			HCRYPT_LOG(LOG_ERR, "invalid MS msg flgs: %02x\n", 
				hcryptMsg_GetKeyIndex(&_hcMsg_STA_MsgInfo, msg));
			return(-1);
		}
		break;
	case HCRYPT_MSG_PT_KM:
	if (HCRYPT_SE_TSUDP != hcryptMsg_KM_GetSE(msg)) {
			HCRYPT_LOG(LOG_ERR, "invalid KM msg SE: %d\n",
				hcryptMsg_KM_GetSE(msg));
	} else if (hcryptMsg_KM_HasNoSek(msg)) {
			HCRYPT_LOG(LOG_ERR, "invalid KM msg flgs: %02x\n",
				hcryptMsg_KM_GetKeyIndex(msg));
			return(-1);
		}
		break;
	default:
		HCRYPT_LOG(LOG_ERR, "invalid pkt type: %d\n",	rc);
		rc = 0; /* unknown packet type */
		break;
	}
	return(rc);	/* -1: error, 0: unknown: >0: PT */
}	

static hcrypt_MsgInfo _hcMsg_STA_MsgInfo;

hcrypt_MsgInfo *hcryptMsg_STA_MsgInfo(void)
{
	_hcMsg_STA_MsgInfo.hdr_len      = HCRYPT_MSG_STA_HDR_SZ;
	_hcMsg_STA_MsgInfo.pfx_len      = HCRYPT_MSG_STA_PFX_SZ;
	_hcMsg_STA_MsgInfo.getKeyFlags  = hcryptMsg_STA_GetKeyFlags;
	_hcMsg_STA_MsgInfo.getPki       = hcryptMsg_STA_GetPki;
	_hcMsg_STA_MsgInfo.setPki       = hcryptMsg_STA_SetPki;
	_hcMsg_STA_MsgInfo.resetCache   = hcryptMsg_STA_ResetCache;
	_hcMsg_STA_MsgInfo.indexMsg     = hcryptMsg_STA_IndexMsg;
	_hcMsg_STA_MsgInfo.parseMsg     = hcryptMsg_STA_ParseMsg;

	return(&_hcMsg_STA_MsgInfo);
}

