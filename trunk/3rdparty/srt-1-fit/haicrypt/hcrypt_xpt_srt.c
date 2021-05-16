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

   2014-03-11 (jdube)
        Adaptation for SRT.
*****************************************************************************/

#include <string.h>			/* memset, memcpy */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>		/* htonl, ntohl */
#endif
#include "hcrypt.h"

/*
 *  HaiCrypt SRT (Secure Reliable Transport) Media Stream (MS) Msg Prefix:
 *  This is UDT data header with Crypto Key Flags (KF) added.
 *  Header is in 32bit host order words in the context of the functions of this handler.
 *
 *      0                   1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 * 0x00 |0|               Packet Sequence Number (pki)                  |
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 * 0x04 |FF |o|KF |             Message Number                          |
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 * 0x08 |                         Time Stamp                            |
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 * 0x0C |                   Destination Socket ID)                      |
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 *      |                            Payload...                         |
 */


/*
 *  HaiCrypt Standalone Transport Keying Material (KM) Msg header kept in SRT
 *  Message and cache maintained in network order
 *
 *      0                   1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 * 0x00 |0|Vers |   PT  |             Sign              |     resv      |
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 *      ...                                                             .
 */

#define HCRYPT_MSG_SRT_HDR_SZ		16
#define HCRYPT_MSG_SRT_PFX_SZ		16

#define HCRYPT_MSG_SRT_OFS_PKI  	0
#define HCRYPT_MSG_SRT_OFS_MSGNO	4
#define HCRYPT_MSG_SRT_SHF_KFLGS	27   //shift

static hcrypt_MsgInfo _hcMsg_SRT_MsgInfo;

static unsigned hcryptMsg_SRT_GetKeyFlags(unsigned char *msg)
{
	uint32_t msgno;
	memcpy(&msgno, &msg[HCRYPT_MSG_SRT_OFS_MSGNO], sizeof(msgno)); //header is in host order
	return((unsigned)((msgno >> HCRYPT_MSG_SRT_SHF_KFLGS) & HCRYPT_MSG_F_xSEK));
}

static hcrypt_Pki hcryptMsg_SRT_GetPki(unsigned char *msg, int nwkorder)
{
	hcrypt_Pki pki;
	memcpy(&pki, &msg[HCRYPT_MSG_SRT_OFS_PKI], sizeof(pki)); //header is in host order
	return (nwkorder ? htonl(pki) : pki);
}

static void hcryptMsg_SRT_SetPki(unsigned char *msg, hcrypt_Pki pki)
{
	memcpy(&msg[HCRYPT_MSG_SRT_OFS_PKI], &pki, sizeof(pki)); //header is in host order
}

static void hcryptMsg_SRT_ResetCache(unsigned char *pfx_cache, unsigned pkt_type, unsigned kflgs)
{
	switch(pkt_type) {
	case HCRYPT_MSG_PT_MS: /* Media Stream */
	/* Nothing to do, header filled by protocol */
		break;
	case HCRYPT_MSG_PT_KM: /* Keying Material */
		pfx_cache[HCRYPT_MSG_KM_OFS_VERSION] = (unsigned char)((HCRYPT_MSG_VERSION << 4) | pkt_type); // version || PT
		pfx_cache[HCRYPT_MSG_KM_OFS_SIGN]    = (unsigned char)((HCRYPT_MSG_SIGN >> 8) & 0xFF); // Haivision PnP Mfr ID
		pfx_cache[HCRYPT_MSG_KM_OFS_SIGN+1]  = (unsigned char)(HCRYPT_MSG_SIGN & 0xFF);
		pfx_cache[HCRYPT_MSG_KM_OFS_KFLGS]    = (unsigned char)kflgs; //HCRYPT_MSG_F_xxx
		break;
	default:
		break;
	}
}

static void hcryptMsg_SRT_IndexMsg(unsigned char *msg, unsigned char *pfx_cache)
{
	(void)msg;
	(void)pfx_cache;
	return; //nothing to do, header and index maintained by SRT
}

static int hcryptMsg_SRT_ParseMsg(unsigned char *msg)
{
	int rc;

	if ((HCRYPT_MSG_VERSION == hcryptMsg_KM_GetVersion(msg))	/* Version 1 */
	&&  (HCRYPT_MSG_PT_KM   == hcryptMsg_KM_GetPktType(msg))   	/* Keying Material */
	&&  (HCRYPT_MSG_SIGN    == hcryptMsg_KM_GetSign(msg))) {	/* 'HAI' PnP Mfr ID */
		rc = HCRYPT_MSG_PT_KM;
	} else {
		//Assume it's data. 
		//SRT does not call this for MS msg
		rc = HCRYPT_MSG_PT_MS;
	}

	switch(rc) {
	case HCRYPT_MSG_PT_MS:
		if (hcryptMsg_HasNoSek(&_hcMsg_SRT_MsgInfo, msg)
		||  hcryptMsg_HasBothSek(&_hcMsg_SRT_MsgInfo, msg)) {
			HCRYPT_LOG(LOG_ERR, "invalid MS msg flgs: %02x\n", 
				hcryptMsg_GetKeyIndex(&_hcMsg_SRT_MsgInfo, msg));
			return(-1);
		}
		break;
	case HCRYPT_MSG_PT_KM:
		if (HCRYPT_SE_TSSRT != hcryptMsg_KM_GetSE(msg)) { //Check Stream Encapsulation (SE)
			HCRYPT_LOG(LOG_ERR, "invalid KM msg SE: %d\n",
				hcryptMsg_KM_GetSE(msg));
			return(-1);
		}
		if (hcryptMsg_KM_HasNoSek(msg)) {
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

static hcrypt_MsgInfo _hcMsg_SRT_MsgInfo;

hcrypt_MsgInfo *hcryptMsg_SRT_MsgInfo(void)
{
	_hcMsg_SRT_MsgInfo.hdr_len      = HCRYPT_MSG_SRT_HDR_SZ;
	_hcMsg_SRT_MsgInfo.pfx_len      = HCRYPT_MSG_SRT_PFX_SZ;
	_hcMsg_SRT_MsgInfo.getKeyFlags  = hcryptMsg_SRT_GetKeyFlags;
	_hcMsg_SRT_MsgInfo.getPki       = hcryptMsg_SRT_GetPki;
	_hcMsg_SRT_MsgInfo.setPki       = hcryptMsg_SRT_SetPki;
	_hcMsg_SRT_MsgInfo.resetCache   = hcryptMsg_SRT_ResetCache;
	_hcMsg_SRT_MsgInfo.indexMsg     = hcryptMsg_SRT_IndexMsg;
	_hcMsg_SRT_MsgInfo.parseMsg     = hcryptMsg_SRT_ParseMsg;

	return(&_hcMsg_SRT_MsgInfo);
}

