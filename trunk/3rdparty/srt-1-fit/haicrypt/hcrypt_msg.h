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

#ifndef HCRYPT_MSG_H
#define HCRYPT_MSG_H

/*
 * HaiCrypt Transport Message Header info
 */


#ifndef HCRYPT_DSP
#include <sys/types.h>

typedef uint32_t hcrypt_Pki;
#endif /* HCRYPT_DSP */


#define HCRYPT_MSG_VERSION  1       /* Current HaiCrypt version */

#define HCRYPT_MSG_SIGN     (('H'-'@')<<10 | ('A'-'@')<<5 | ('I'-'@')) /* Haivision PnP Mfr ID 'HAI' */

#define HCRYPT_PKI_SZ       4       /* Packet Index size (CTR mode cipher) */

#define HCRYPT_MSG_PT_MS    1       /* Media stream */
#define HCRYPT_MSG_PT_KM    2       /* Keying Material */
#define HCRYPT_MSG_PT_RESV7 7       /* Reserved to dicriminate MPEG-TS packet (SyncByte=0x47) */


#define HCRYPT_MSG_F_eSEK   0x01    /* Even Stream Encrypting Key */
#define HCRYPT_MSG_F_oSEK   0x02    /* Odd Stream Encrypting Key */
#define HCRYPT_MSG_F_xSEK   0x03    /* Both Stream Encrypting Keys */

typedef struct {
        int hdr_len;     // data and control common prefix portion
        int pfx_len;     // Message Prefix len. Also payload offset
        unsigned    (*getKeyFlags)(unsigned char *msg);
        hcrypt_Pki  (*getPki)(unsigned char *msg, int nwko);
        void        (*setPki)(unsigned char *msg, hcrypt_Pki);
        void        (*resetCache)(unsigned char *pfx_cache, unsigned pkt_type, unsigned flags);
        void        (*indexMsg)(unsigned char *msg, unsigned char *pfx_cache);
        int         (*parseMsg)(unsigned char *msg);
}hcrypt_MsgInfo;


#define hcryptMsg_GetKeyIndex(mi,msg)   ((mi)->getKeyFlags(msg)>>1)
#define hcryptMsg_GetPki(mi,msg,nwko)   ((mi)->getPki(msg,nwko))
#define hcryptMsg_SetPki(mi,msg,pki)    (mi)->setPki(msg, pki)

#define hcryptMsg_HasEvenSek(mi,msg)    ((mi)->getKeyFlags(msg) & HCRYPT_MSG_F_eSEK)
#define hcryptMsg_HasOddSek(mi,msg)     ((mi)->getKeyFlags(msg) & HCRYPT_MSG_F_oSEK)
#define hcryptMsg_HasBothSek(mi,msg)    (HCRYPT_MSG_F_xSEK == ((mi)->getKeyFlags(msg) & HCRYPT_MSG_F_xSEK))
#define hcryptMsg_HasNoSek(mi,msg)      (0 == ((mi)->getKeyFlags(msg) & HCRYPT_MSG_F_xSEK))

#define hcryptMsg_PaddedLen(len, fact)  ((((len)+(fact)-1)/(fact))*(fact))


/*
 *  HaiCrypt KMmsg (Keying Material):
 *
 *      0                   1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 *+0x00 |0|Vers |   PT  |             Sign              |    resv   |KF |
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 *+0x04 |                              KEKI                             |
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 *+0x08 |    Cipher     |      Auth     |      SE       |     Resv1     |
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 *+0x0C |             Resv2             |     Slen/4    |     Klen/4    |
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 *+0x10 |                              Salt                             |
 *      |                              ...                              |
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 *      |                              Wrap                             |
 *      |                              ...                              |
 *      +-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+
 */



#define HCRYPT_MSG_KM_OFS_VERSION   0
#define HCRYPT_MSG_KM_OFS_PT        0
#define HCRYPT_MSG_KM_OFS_SIGN      1
#define HCRYPT_MSG_KM_OFS_KFLGS     3
#define HCRYPT_MSG_KM_RSH_KFLGS     0  /* Right shift (in byte) */

#define HCRYPT_MSG_KM_OFS_KEKI      4
#define HCRYPT_MSG_KM_OFS_CIPHER    8
#define HCRYPT_MSG_KM_OFS_AUTH      9
#define HCRYPT_MSG_KM_OFS_SE        10
#define HCRYPT_MSG_KM_OFS_RESV2     12
#define HCRYPT_MSG_KM_OFS_SLEN      14
#define HCRYPT_MSG_KM_OFS_KLEN      15
#define HCRYPT_MSG_KM_OFS_SALT      16

#define HCRYPT_MSG_KM_MAX_SZ    (0 \
        + HCRYPT_MSG_KM_OFS_SALT \
        + HAICRYPT_SALT_SZ \
        + (HAICRYPT_KEY_MAX_SZ * 2) \
        + HAICRYPT_WRAPKEY_SIGN_SZ)

#define HCRYPT_CIPHER_NONE      0
#define HCRYPT_CIPHER_AES_ECB   1
#define HCRYPT_CIPHER_AES_CTR   2
#define HCRYPT_CIPHER_AES_CBC   3

#define HCRYPT_AUTH_NONE        0

#define HCRYPT_SE_TSUDP         1
        hcrypt_MsgInfo *        hcryptMsg_STA_MsgInfo(void);
#define HCRYPT_SE_TSSRT         2
        hcrypt_MsgInfo *        hcryptMsg_SRT_MsgInfo(void);

#define hcryptMsg_KM_GetVersion(msg)    (((msg)[HCRYPT_MSG_KM_OFS_VERSION]>>4)& 0xF)
#define hcryptMsg_KM_GetPktType(msg)    (((msg)[HCRYPT_MSG_KM_OFS_PT]) & 0xF)
#define hcryptMsg_KM_GetSign(msg)       (((msg)[HCRYPT_MSG_KM_OFS_SIGN]<<8) | (msg)[HCRYPT_MSG_KM_OFS_SIGN+1])

#define hcryptMsg_KM_GetKeyIndex(msg)   (((msg)[HCRYPT_MSG_KM_OFS_KFLGS] & HCRYPT_MSG_F_xSEK)>>1)

#define hcryptMsg_KM_HasEvenSek(msg)    ((msg)[HCRYPT_MSG_KM_OFS_KFLGS] & HCRYPT_MSG_F_eSEK)
#define hcryptMsg_KM_HasOddSek(msg)     ((msg)[HCRYPT_MSG_KM_OFS_KFLGS] & HCRYPT_MSG_F_oSEK)
#define hcryptMsg_KM_HasBothSek(msg)    (HCRYPT_MSG_F_xSEK == ((msg)[HCRYPT_MSG_KM_OFS_KFLGS] & HCRYPT_MSG_F_xSEK))
#define hcryptMsg_KM_HasNoSek(msg)      (0 == ((msg)[HCRYPT_MSG_KM_OFS_KFLGS] & HCRYPT_MSG_F_xSEK))

#define hcryptMsg_KM_GetCipher(msg)     ((msg)[HCRYPT_MSG_KM_OFS_CIPHER])
#define hcryptMsg_KM_GetAuth(msg)       ((msg)[HCRYPT_MSG_KM_OFS_AUTH])
#define hcryptMsg_KM_GetSE(msg)         ((msg)[HCRYPT_MSG_KM_OFS_SE])

#define hcryptMsg_KM_GetSaltLen(msg)    (size_t)((msg)[HCRYPT_MSG_KM_OFS_SLEN] * 4)
#define hcryptMsg_KM_GetSekLen(msg)     (size_t)((msg)[HCRYPT_MSG_KM_OFS_KLEN] * 4)

#define hcryptMsg_KM_SetSaltLen(msg,len)do {(msg)[HCRYPT_MSG_KM_OFS_SLEN] = (len)/4;} while(0)
#define hcryptMsg_KM_SetSekLen(msg,len) do {(msg)[HCRYPT_MSG_KM_OFS_KLEN] = (len)/4;} while(0)


#endif /* HCRYPT_MSG_H */
