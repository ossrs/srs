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
 *****************************************************************************/

#ifndef INC_SRT_CRYPTO_H
#define INC_SRT_CRYPTO_H

#include <cstring>
#include <string>

// UDT
#include "udt.h"
#include "packet.h"
#include "utilities.h"
#include "logging.h"

#include <haicrypt.h>
#include <hcrypt_msg.h>



namespace srt_logging
{
std::string KmStateStr(SRT_KM_STATE state);
#if ENABLE_LOGGING
extern Logger cnlog;
#endif
}

namespace srt
{
class CUDT;
struct CSrtConfig;


// For KMREQ/KMRSP. Only one field is used.
const size_t SRT_KMR_KMSTATE = 0;

#define SRT_CMD_MAXSZ       HCRYPT_MSG_KM_MAX_SZ  /* Maximum SRT custom messages payload size (bytes) */
const size_t SRTDATA_MAXSIZE = SRT_CMD_MAXSZ/sizeof(uint32_t);

class CCryptoControl
{
    SRTSOCKET m_SocketID;

    size_t    m_iSndKmKeyLen;        //Key length
    size_t    m_iRcvKmKeyLen;        //Key length from rx KM

    // Temporarily allow these to be accessed.
public:
    SRT_KM_STATE m_SndKmState;         //Sender Km State (imposed by agent)
    SRT_KM_STATE m_RcvKmState;         //Receiver Km State (informed by peer)

private:
    // Partial haicrypt configuration, consider
    // putting the whole HaiCrypt_Cfg object here.
    int m_KmRefreshRatePkt;
    int m_KmPreAnnouncePkt;
    int m_iCryptoMode;

    HaiCrypt_Secret m_KmSecret;     //Key material shared secret
    // Sender
    sync::steady_clock::time_point m_SndKmLastTime;
    sync::Mutex m_mtxLock; // A mutex to protect concurrent access to CCryptoControl.
    struct {
        unsigned char Msg[HCRYPT_MSG_KM_MAX_SZ];
        size_t MsgLen;
        int iPeerRetry;
    } m_SndKmMsg[2];
    HaiCrypt_Handle m_hSndCrypto;
    // Receiver
    HaiCrypt_Handle m_hRcvCrypto;

    bool m_bErrorReported;

public:
    static void globalInit();

    static bool isAESGCMSupported();

    bool sendingAllowed()
    {
        // This function is called to state as to whether the
        // crypter allows the packet to be sent over the link.
        // This is possible in two cases:
        // - when Agent didn't set a password, no matter the crypto state
        if (m_KmSecret.len == 0)
            return true;
        // - when Agent did set a password and the crypto state is SECURED.
        if (m_KmSecret.len > 0 && m_SndKmState == SRT_KM_S_SECURED
                // && m_iRcvPeerKmState == SRT_KM_S_SECURED ?
           )
            return true;

        return false;
    }

    bool hasPassphrase() const
    {
        return m_KmSecret.len > 0;
    }

    int getCryptoMode() const
    {
        return m_iCryptoMode;
    }

    /// Regenerate cryptographic key material if needed.
    /// @param[in] sock If not null, the socket will be used to send the KM message to the peer (e.g. KM refresh).
    /// @param[in] bidirectional If true, the key material will be regenerated for both directions (receiver and sender).
    SRT_ATTR_EXCLUDES(m_mtxLock)
    void regenCryptoKm(CUDT* sock, bool bidirectional);

    size_t KeyLen() { return m_iSndKmKeyLen; }

    // Needed for CUDT
    void updateKmState(int cmd, size_t srtlen);

    // Detailed processing
    int processSrtMsg_KMREQ(const uint32_t* srtdata, size_t len, int hsv,
            uint32_t srtdata_out[], size_t&);

    // This returns:
    // 1 - the given payload is the same as the currently used key
    // 0 - there's no key in agent or the payload is error message with agent NOSECRET.
    // -1 - the payload is error message with other state or it doesn't match the key
    int processSrtMsg_KMRSP(const uint32_t* srtdata, size_t len, int hsv);
    void createFakeSndContext();

    const unsigned char* getKmMsg_data(size_t ki) const { return m_SndKmMsg[ki].Msg; }
    size_t getKmMsg_size(size_t ki) const { return m_SndKmMsg[ki].MsgLen; }

    /// Check if the key stored at @c ki shall be sent. When during the handshake,
    /// it only matters if the KM message for that index is recorded at all.
    /// Otherwise returns true only if also the retry counter didn't expire.
    ///
    /// @param ki Key index (0 or 1)
    /// @param runtime True, if this happens as a key update
    ///                during transmission (otherwise it's during the handshake)
    /// @return Whether the KM message at given index needs to be sent.
    bool getKmMsg_needSend(size_t ki, bool runtime) const
    {
        if (runtime)
            return (m_SndKmMsg[ki].iPeerRetry > 0 && m_SndKmMsg[ki].MsgLen > 0);
        else
            return m_SndKmMsg[ki].MsgLen > 0;
    }

    /// Mark the key as already sent. When no 'runtime' (during the handshake)
    /// it actually does nothing so that this will be retried as long as the handshake
    /// itself is being retried. Otherwise this is during transmission and will expire
    /// after several retries.
    ///
    /// @param ki Key index (0 or 1)
    /// @param runtime True, if this happens as a key update
    ///                during transmission (otherwise it's during the handshake)
    void getKmMsg_markSent(size_t ki, bool runtime)
    {
#if ENABLE_LOGGING
        using srt_logging::cnlog;
#endif

        m_SndKmLastTime = sync::steady_clock::now();
        if (runtime)
        {
            m_SndKmMsg[ki].iPeerRetry--;
            HLOGC(cnlog.Debug, log << "getKmMsg_markSent: key[" << ki << "]: len=" << m_SndKmMsg[ki].MsgLen << " retry=" << m_SndKmMsg[ki].iPeerRetry);
        }
        else
        {
            HLOGC(cnlog.Debug, log << "getKmMsg_markSent: key[" << ki << "]: len=" << m_SndKmMsg[ki].MsgLen << " STILL IN USE.");
        }
    }

    /// Check if the response returned by KMRSP matches the recorded KM message.
    /// When it is, set also the retry counter to 0 to prevent further retries.
    ///
    /// @param ki KM message index (0 or 1)
    /// @param srtmsg Message received through KMRSP
    /// @param bytesize Size of the message
    /// @return True if the message is identical to the recorded KM message at given index.
    bool getKmMsg_acceptResponse(size_t ki, const uint32_t* srtmsg, size_t bytesize)
    {
        if ( m_SndKmMsg[ki].MsgLen == bytesize
                && 0 == memcmp(m_SndKmMsg[ki].Msg, srtmsg, m_SndKmMsg[ki].MsgLen))
        {
            m_SndKmMsg[ki].iPeerRetry = 0;
            return true;
        }
        return false;
    }

    CCryptoControl(SRTSOCKET id);

    // DEBUG PURPOSES:
    std::string CONID() const;
    std::string FormatKmMessage(std::string hdr, int cmd, size_t srtlen);

    bool init(HandshakeSide, const CSrtConfig&, bool);
    SRT_ATTR_EXCLUDES(m_mtxLock)
    void close();

    /// (Re)send KM request to a peer on timeout.
    /// This function is used in:
    /// - HSv4 (initial key material exchange - in HSv5 it's attached to handshake).
    /// - The case of key regeneration (KM refresh), when a new key has to be sent again.
    ///   In this case the first sending happens in regenCryptoKm(..). This function
    ///   retransmits the KM request by timeout if not KM response has been received.
    SRT_ATTR_EXCLUDES(m_mtxLock)
    void sendKeysToPeer(CUDT* sock, int iSRTT);

    void setCryptoSecret(const HaiCrypt_Secret& secret)
    {
        m_KmSecret = secret;
    }

    void setCryptoKeylen(size_t keylen)
    {
        m_iSndKmKeyLen = keylen;
        m_iRcvKmKeyLen = keylen;
    }

    bool createCryptoCtx(HaiCrypt_Handle& rh, size_t keylen, HaiCrypt_CryptoDir tx, bool bAESGCM);

    int getSndCryptoFlags() const
    {
#ifdef SRT_ENABLE_ENCRYPTION
        return(m_hSndCrypto ?
                HaiCrypt_Tx_GetKeyFlags(m_hSndCrypto) :
                // When encryption isn't on, check if it was required
                // If it was, return -1 as flags, which means that
                // encryption was requested and not possible.
                hasPassphrase() ? -1 :
                0);
#else
        return 0;
#endif
    }

    bool isSndEncryptionOK() const
    {
        // Similar to this above, just quickly check if the encryption
        // is required and possible, or not possible
        if (!hasPassphrase())
            return true; // no encryption required

        if (m_hSndCrypto)
            return true; // encryption is required and possible

        return false;
    }

    /// Encrypts the packet. If encryption is not turned on, it
    /// does nothing. If the encryption is not correctly configured,
    /// the encryption will fail.
    /// XXX Encryption flags in the PH_MSGNO
    /// field in the header must be correctly set before calling.
    EncryptionStatus encrypt(CPacket& w_packet);

    /// Decrypts the packet. If the packet has ENCKEYSPEC part
    /// in PH_MSGNO set to EK_NOENC, it does nothing. It decrypts
    /// only if the encryption correctly configured, otherwise it
    /// fails. After successful decryption, the ENCKEYSPEC part
    // in PH_MSGNO is set to EK_NOENC.
    EncryptionStatus decrypt(CPacket& w_packet);

    ~CCryptoControl();
};

} // namespace srt

#endif // SRT_CONGESTION_CONTROL_H
