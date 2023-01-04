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

#include "platform_sys.h"

#include <cstring>
#include <string>
#include <sstream>
#include <iterator>

#include "udt.h"
#include "utilities.h"
#include <haicrypt.h>
#include "crypto.h"
#include "logging.h"
#include "core.h"
#include "api.h"

using namespace srt_logging;

#define SRT_MAX_KMRETRY     10
 
//#define SRT_CMD_KMREQ       3           /* HaiCryptTP SRT Keying Material */
//#define SRT_CMD_KMRSP       4           /* HaiCryptTP SRT Keying Material ACK */
#define SRT_CMD_KMREQ_SZ    HCRYPT_MSG_KM_MAX_SZ          /* */
#if     SRT_CMD_KMREQ_SZ > SRT_CMD_MAXSZ
#error  SRT_CMD_MAXSZ too small
#endif
/*      Key Material Request (Network Order)
        See HaiCryptTP SRT (hcrypt_xpt_srt.c)
*/

// 10* HAICRYPT_DEF_KM_PRE_ANNOUNCE
const int SRT_CRYPT_KM_PRE_ANNOUNCE SRT_ATR_UNUSED = 0x10000;

namespace srt_logging
{
std::string KmStateStr(SRT_KM_STATE state)
{
    switch (state)
    {
#define TAKE(val) case SRT_KM_S_##val : return #val
        TAKE(UNSECURED);
        TAKE(SECURED);
        TAKE(SECURING);
        TAKE(NOSECRET);
        TAKE(BADSECRET);
#undef TAKE
    default:
        {
            char buf[256];
            sprintf(buf, "??? (%d)", state);
            return buf;
        }
    }
}
} // namespace

using srt_logging::KmStateStr;

void srt::CCryptoControl::globalInit()
{
#ifdef SRT_ENABLE_ENCRYPTION
    // We need to force the Cryspr to be initialized during startup to avoid the
    // possibility of multiple threads initialzing the same static data later on.
    HaiCryptCryspr_Get_Instance();
#endif
}

#if ENABLE_LOGGING
std::string srt::CCryptoControl::FormatKmMessage(std::string hdr, int cmd, size_t srtlen)
{
    std::ostringstream os;
    os << hdr << ": cmd=" << cmd << "(" << (cmd == SRT_CMD_KMREQ ? "KMREQ":"KMRSP") <<") len="
        << size_t(srtlen*sizeof(int32_t)) << " KmState: SND="
        << KmStateStr(m_SndKmState)
        << " RCV=" << KmStateStr(m_RcvKmState);
    return os.str();
}
#endif

void srt::CCryptoControl::updateKmState(int cmd, size_t srtlen SRT_ATR_UNUSED)
{
    if (cmd == SRT_CMD_KMREQ)
    {
        if ( SRT_KM_S_UNSECURED == m_SndKmState)
        {
            m_SndKmState = SRT_KM_S_SECURING;
        }
        LOGP(cnlog.Note, FormatKmMessage("sendSrtMsg", cmd, srtlen));
    }
    else
    {
        LOGP(cnlog.Note, FormatKmMessage("sendSrtMsg", cmd, srtlen));
    }
}

void srt::CCryptoControl::createFakeSndContext()
{
    if (!m_iSndKmKeyLen)
        m_iSndKmKeyLen = 16;

    if (!createCryptoCtx(m_iSndKmKeyLen, HAICRYPT_CRYPTO_DIR_TX, (m_hSndCrypto)))
    {
        HLOGC(cnlog.Debug, log << "Error: Can't create fake crypto context for sending - sending will return ERROR!");
        m_hSndCrypto = 0;
    }
}

int srt::CCryptoControl::processSrtMsg_KMREQ(
        const uint32_t* srtdata SRT_ATR_UNUSED,
        size_t bytelen SRT_ATR_UNUSED,
        int hsv SRT_ATR_UNUSED,
        uint32_t pw_srtdata_out[], size_t& w_srtlen)
{
    //Receiver
    /* All 32-bit msg fields swapped on reception
     * But HaiCrypt expect network order message
     * Re-swap to cancel it.
     */
#ifdef SRT_ENABLE_ENCRYPTION
    w_srtlen = bytelen/sizeof(srtdata[SRT_KMR_KMSTATE]);
    HtoNLA((pw_srtdata_out), srtdata, w_srtlen);
    unsigned char* kmdata = reinterpret_cast<unsigned char*>(pw_srtdata_out);

    // The side that has received KMREQ is always an HSD_RESPONDER, regardless of
    // what has called this function. The HSv5 handshake only enforces bidirectional
    // connection.

    bool bidirectional = hsv > CUDT::HS_VERSION_UDT4;

    // Local macro to return rejection appropriately.
    // CHANGED. The first version made HSv5 reject the connection.
    // This isn't well handled by applications, so the connection is
    // still established, but unable to handle any transport.
//#define KMREQ_RESULT_REJECTION() if (bidirectional) { return SRT_CMD_NONE; } else { w_srtlen = 1; goto HSv4_ErrorReport; }
#define KMREQ_RESULT_REJECTION() { w_srtlen = 1; goto HSv4_ErrorReport; }

    int rc = HAICRYPT_OK; // needed before 'goto' run from KMREQ_RESULT_REJECTION macro
    bool wasb4 SRT_ATR_UNUSED = false;
    size_t sek_len = 0;

    // What we have to do:
    // If encryption is on (we know that by having m_KmSecret nonempty), create
    // the crypto context (if bidirectional, create for both sending and receiving).
    // Both crypto contexts should be set with the same length of the key.
    // The problem with interpretinting this should be reported as SRT_CMD_NONE,
    // should be appropriately handled by the caller, as it expects that this
    // function normally return SRT_CMD_KMRSP.
    if ( bytelen <= HCRYPT_MSG_KM_OFS_SALT )  //Sanity on message
    {
        LOGC(cnlog.Error, log << "processSrtMsg_KMREQ: size of the KM (" << bytelen << ") is too small, must be >" << HCRYPT_MSG_KM_OFS_SALT);
        m_RcvKmState = SRT_KM_S_BADSECRET;
        KMREQ_RESULT_REJECTION();
    }

    HLOGC(cnlog.Debug, log << "KMREQ: getting SEK and creating receiver crypto");
    sek_len = hcryptMsg_KM_GetSekLen(kmdata);
    if ( sek_len == 0 )
    {
        LOGC(cnlog.Error, log << "processSrtMsg_KMREQ: Received SEK is empty - REJECTING!");
        m_RcvKmState = SRT_KM_S_BADSECRET;
        KMREQ_RESULT_REJECTION();
    }

    // Write the key length
    m_iRcvKmKeyLen = sek_len;
    // Overwrite the key length anyway - it doesn't make sense to somehow
    // keep the original setting because it will only make KMX impossible.
#if ENABLE_HEAVY_LOGGING
    if (m_iSndKmKeyLen != m_iRcvKmKeyLen)
    {
        LOGC(cnlog.Debug, log << "processSrtMsg_KMREQ: Agent's PBKEYLEN=" << m_iSndKmKeyLen
                << " overwritten by Peer's PBKEYLEN=" << m_iRcvKmKeyLen);
    }
#endif
    m_iSndKmKeyLen = m_iRcvKmKeyLen;

    // This is checked only now so that the SRTO_PBKEYLEN return always the correct value,
    // even if encryption is not possible because Agent didn't set a password, or supplied
    // a wrong password.
    if (m_KmSecret.len == 0)  //We have a shared secret <==> encryption is on
    {
        LOGC(cnlog.Warn, log << "processSrtMsg_KMREQ: Agent does not declare encryption - won't decrypt incoming packets!");
        m_RcvKmState = SRT_KM_S_NOSECRET;
        KMREQ_RESULT_REJECTION();
    }
    wasb4 = m_hRcvCrypto;

    if (!createCryptoCtx(m_iRcvKmKeyLen, HAICRYPT_CRYPTO_DIR_RX, (m_hRcvCrypto)))
    {
        LOGC(cnlog.Error, log << "processSrtMsg_KMREQ: Can't create RCV CRYPTO CTX - must reject...");
        m_RcvKmState = SRT_KM_S_NOSECRET;
        KMREQ_RESULT_REJECTION();
    }

    if (!wasb4)
    {
        HLOGC(cnlog.Debug, log << "processSrtMsg_KMREQ: created RX ENC with KeyLen=" << m_iRcvKmKeyLen);
    }
    // We have both sides set with password, so both are pending for security
    m_RcvKmState = SRT_KM_S_SECURING;
    // m_SndKmState is set to SECURING or UNSECURED in init(),
    // or it might have been set to SECURED, NOSECRET or BADSECRET in the previous
    // handshake iteration (handshakes may be sent multiple times for the same connection).

    rc = HaiCrypt_Rx_Process(m_hRcvCrypto, kmdata, bytelen, NULL, NULL, 0);
    switch(rc >= 0 ? HAICRYPT_OK : rc)
    {
    case HAICRYPT_OK:
        m_RcvKmState = SRT_KM_S_SECURED;
        HLOGC(cnlog.Debug, log << "KMREQ/rcv: (snd) Rx process successful - SECURED.");
        //Send back the whole message to confirm
        break;
    case HAICRYPT_ERROR_WRONG_SECRET: //Unmatched shared secret to decrypt wrapped key
        m_RcvKmState = m_SndKmState = SRT_KM_S_BADSECRET;
        //Send status KMRSP message to tel error
        w_srtlen = 1;
        LOGC(cnlog.Warn, log << "KMREQ/rcv: (snd) Rx process failure - BADSECRET");
        break;
    case HAICRYPT_ERROR: //Other errors
    default:
        m_RcvKmState = m_SndKmState = SRT_KM_S_NOSECRET;
        w_srtlen = 1;
        LOGC(cnlog.Warn, log << "KMREQ/rcv: (snd) Rx process failure (IPE) - NOSECRET");
        break;
    }

    LOGP(cnlog.Note, FormatKmMessage("processSrtMsg_KMREQ", SRT_CMD_KMREQ, bytelen));

    // Since now, when CCryptoControl::decrypt() encounters an error, it will print it, ONCE,
    // until the next KMREQ is received as a key regeneration.
    m_bErrorReported = false;


    if (w_srtlen == 1)
        goto HSv4_ErrorReport;

    // Configure the sender context also, if it succeeded to configure the
    // receiver context and we are using bidirectional mode.
    if (bidirectional)
    {
        // Note: 'bidirectional' means that we want a bidirectional key update,
        // which happens only and exclusively with HSv5 handshake - not when the
        // usual key update through UMSG_EXT+SRT_CMD_KMREQ was done (which is used
        // in HSv4 versions also to initialize the first key, unlike HSv5).
        if (m_RcvKmState == SRT_KM_S_SECURED)
        {
            if (m_SndKmState == SRT_KM_S_SECURING && !m_hSndCrypto)
            {
                m_iSndKmKeyLen = m_iRcvKmKeyLen;
                if (HaiCrypt_Clone(m_hRcvCrypto, HAICRYPT_CRYPTO_DIR_TX, &m_hSndCrypto) != HAICRYPT_OK)
                {
                    LOGC(cnlog.Error, log << "processSrtMsg_KMREQ: Can't create SND CRYPTO CTX - WILL NOT SEND-ENCRYPT correctly!");
                    if (hasPassphrase())
                        m_SndKmState = SRT_KM_S_BADSECRET;
                    else
                        m_SndKmState = SRT_KM_S_NOSECRET;
                }
                else
                {
                    m_SndKmState = SRT_KM_S_SECURED;
                }

                LOGC(cnlog.Note, log << FormatKmMessage("processSrtMsg_KMREQ", SRT_CMD_KMREQ, bytelen)
                        << " SndKeyLen=" << m_iSndKmKeyLen
                        << " TX CRYPTO CTX CLONED FROM RX"
                    );

                // Write the KM message into the field from which it will be next sent.
                memcpy((m_SndKmMsg[0].Msg), kmdata, bytelen);
                m_SndKmMsg[0].MsgLen = bytelen;
                m_SndKmMsg[0].iPeerRetry = 0; // Don't start sending them upon connection :)
            }
            else
            {
                HLOGC(cnlog.Debug, log << "processSrtMsg_KMREQ: NOT cloning RX to TX crypto: already in "
                        << KmStateStr(m_SndKmState) << " state");
            }
        }
        else
        {
            HLOGP(cnlog.Debug, "processSrtMsg_KMREQ: NOT SECURED - not replaying failed security association to TX CRYPTO CTX");
        }
    }
    else
    {
        HLOGC(cnlog.Debug, log << "processSrtMsg_KMREQ: NOT REPLAYING the key update to TX CRYPTO CTX.");
    }

    return SRT_CMD_KMRSP;

HSv4_ErrorReport:

    if (bidirectional && hasPassphrase())
    {
        // If the Forward KMX process has failed, the reverse-KMX process was not done at all.
        // This will lead to incorrect object configuration and will fail to properly declare
        // the transmission state.
        // Create the "fake crypto" with the passphrsae you currently have.
        createFakeSndContext();
    }
#undef KMREQ_RESULT_REJECTION

#else
    // It's ok that this is reported as error because this happens in a scenario,
    // when non-encryption-enabled SRT application is contacted by encryption-enabled SRT
    // application which tries to make a security association.
    LOGC(cnlog.Warn, log << "processSrtMsg_KMREQ: Encryption not enabled at compile time - must reject...");
    m_RcvKmState = SRT_KM_S_NOSECRET;
#endif

    w_srtlen = 1;

    pw_srtdata_out[SRT_KMR_KMSTATE] = m_RcvKmState;
    return SRT_CMD_KMRSP;
}

int srt::CCryptoControl::processSrtMsg_KMRSP(const uint32_t* srtdata, size_t len, int /* XXX unused? hsv*/)
{
    /* All 32-bit msg fields (if present) swapped on reception
     * But HaiCrypt expect network order message
     * Re-swap to cancel it.
     */
    uint32_t srtd[SRTDATA_MAXSIZE];
    size_t srtlen = len/sizeof(uint32_t);
    HtoNLA(srtd, srtdata, srtlen);

    int retstatus = -1;

    // Unused?
    //bool bidirectional = hsv > CUDT::HS_VERSION_UDT4;

    // Since now, when CCryptoControl::decrypt() encounters an error, it will print it, ONCE,
    // until the next KMREQ is received as a key regeneration.
    m_bErrorReported = false;

    if (srtlen == 1) // Error report. Set accordingly.
    {
        SRT_KM_STATE peerstate = SRT_KM_STATE(srtd[SRT_KMR_KMSTATE]); /* Bad or no passphrase */
        m_SndKmMsg[0].iPeerRetry = 0;
        m_SndKmMsg[1].iPeerRetry = 0;

        switch (peerstate)
        {
        case SRT_KM_S_BADSECRET:
            m_SndKmState = m_RcvKmState = SRT_KM_S_BADSECRET;
            retstatus = -1;
            break;

            // Default embraces two cases:
            // NOSECRET: this KMRSP was sent by secured Peer, but Agent supplied no password.
            // UNSECURED: this KMRSP was sent by unsecure Peer because Agent sent KMREQ.

        case SRT_KM_S_NOSECRET:
            // This means that the peer did not set the password, while Agent did.
            m_RcvKmState = SRT_KM_S_UNSECURED;
            m_SndKmState = SRT_KM_S_NOSECRET;
            retstatus = -1;
            break;

        case SRT_KM_S_UNSECURED:
            // This means that KMRSP was sent without KMREQ, to inform the Agent,
            // that the Peer, unlike Agent, does use password. Agent can send then,
            // but can't decrypt what Peer would send.
            m_RcvKmState = SRT_KM_S_NOSECRET;
            m_SndKmState = SRT_KM_S_UNSECURED;
            retstatus = 0;
            break;

        default:
            LOGC(cnlog.Fatal, log << "processSrtMsg_KMRSP: IPE: unknown peer error state: "
                    << KmStateStr(peerstate) << " (" << int(peerstate) << ")");
            m_RcvKmState = SRT_KM_S_NOSECRET;
            m_SndKmState = SRT_KM_S_NOSECRET;
            retstatus = -1; //This is IPE
            break;
        }

        LOGC(cnlog.Warn, log << "processSrtMsg_KMRSP: received failure report. STATE: " << KmStateStr(m_RcvKmState));
    }
    else
    {
        HLOGC(cnlog.Debug, log << "processSrtMsg_KMRSP: received key response len=" << len);
        // XXX INSECURE << ": [" << FormatBinaryString((uint8_t*)srtd, len) << "]";
        bool key1 = getKmMsg_acceptResponse(0, srtd, len);
        bool key2 = true;
        if ( !key1 )
            key2 = getKmMsg_acceptResponse(1, srtd, len); // <--- NOTE SEQUENCING!

        if (key1 || key2)
        {
            m_SndKmState = m_RcvKmState = SRT_KM_S_SECURED;
            HLOGC(cnlog.Debug, log << "processSrtMsg_KMRSP: KM response matches " << (key1 ? "EVEN" : "ODD") << " key");
            retstatus = 1;
        }
        else
        {
            retstatus = -1;
            LOGC(cnlog.Error, log << "processSrtMsg_KMRSP: IPE??? KM response key matches no key");
            /* XXX INSECURE
            LOGC(cnlog.Error, log << "processSrtMsg_KMRSP: KM response: [" << FormatBinaryString((uint8_t*)srtd, len)
                << "] matches no key 0=[" << FormatBinaryString((uint8_t*)m_SndKmMsg[0].Msg, m_SndKmMsg[0].MsgLen)
                << "] 1=[" << FormatBinaryString((uint8_t*)m_SndKmMsg[1].Msg, m_SndKmMsg[1].MsgLen) << "]");
                */

            m_SndKmState = m_RcvKmState = SRT_KM_S_BADSECRET;
        }
        HLOGC(cnlog.Debug, log << "processSrtMsg_KMRSP: key[0]: len=" << m_SndKmMsg[0].MsgLen << " retry=" << m_SndKmMsg[0].iPeerRetry
            << "; key[1]: len=" << m_SndKmMsg[1].MsgLen << " retry=" << m_SndKmMsg[1].iPeerRetry);
    }

    LOGP(cnlog.Note, FormatKmMessage("processSrtMsg_KMRSP", SRT_CMD_KMRSP, len));

    return retstatus;
}

void srt::CCryptoControl::sendKeysToPeer(CUDT* sock SRT_ATR_UNUSED, int iSRTT SRT_ATR_UNUSED, Whether2RegenKm regen SRT_ATR_UNUSED)
{
    if (!m_hSndCrypto || m_SndKmState == SRT_KM_S_UNSECURED)
    {
        HLOGC(cnlog.Debug, log << "sendKeysToPeer: NOT sending/regenerating keys: "
                << (m_hSndCrypto ? "CONNECTION UNSECURED" : "NO TX CRYPTO CTX created"));
        return;
    }
#ifdef SRT_ENABLE_ENCRYPTION
    srt::sync::steady_clock::time_point now = srt::sync::steady_clock::now();
    /*
     * Crypto Key Distribution to peer:
     * If...
     * - we want encryption; and
     * - we have not tried more than CSRTCC_MAXRETRY times (peer may not be SRT); and
     * - and did not get answer back from peer; and
     * - last sent Keying Material req should have been replied (RTT*1.5 elapsed);
     * then (re-)send handshake request.
     */
    if (((m_SndKmMsg[0].iPeerRetry > 0) || (m_SndKmMsg[1].iPeerRetry > 0))
        && ((m_SndKmLastTime + srt::sync::microseconds_from((iSRTT * 3)/2)) <= now))
    {
        for (int ki = 0; ki < 2; ki++)
        {
            if (m_SndKmMsg[ki].iPeerRetry > 0 && m_SndKmMsg[ki].MsgLen > 0)
            {
                m_SndKmMsg[ki].iPeerRetry--;
                HLOGC(cnlog.Debug, log << "sendKeysToPeer: SENDING ki=" << ki << " len=" << m_SndKmMsg[ki].MsgLen
                        << " retry(updated)=" << m_SndKmMsg[ki].iPeerRetry);
                m_SndKmLastTime = now;
                sock->sendSrtMsg(SRT_CMD_KMREQ, (uint32_t *)m_SndKmMsg[ki].Msg, m_SndKmMsg[ki].MsgLen / sizeof(uint32_t));
            }
        }
    }


    if (regen)
    {
        regenCryptoKm(
            sock, // send UMSG_EXT + SRT_CMD_KMREQ to the peer using this socket
            false // Do not apply the regenerated key to the to the receiver context
        ); // regenerate and send
    }
#endif
}

#ifdef SRT_ENABLE_ENCRYPTION
void srt::CCryptoControl::regenCryptoKm(CUDT* sock, bool bidirectional)
{
    if (!m_hSndCrypto)
        return;

    void *out_p[2];
    size_t out_len_p[2];
    int nbo = HaiCrypt_Tx_ManageKeys(m_hSndCrypto, out_p, out_len_p, 2);
    int sent = 0;

    HLOGC(cnlog.Debug, log << "regenCryptoKm: regenerating crypto keys nbo=" << nbo <<
            " THEN=" << (sock ? "SEND" : "KEEP") << " DIR=" << (bidirectional ? "BOTH" : "SENDER"));

    for (int i = 0; i < nbo && i < 2; i++)
    {
        /*
         * New connection keying material
         * or regenerated after crypto_cfg.km_refresh_rate_pkt packets .
         * Send to peer
         */
        // XXX Need to make it clearer and less hardcoded values
        int kix = hcryptMsg_KM_GetKeyIndex((unsigned char *)(out_p[i]));
        int ki = kix & 0x1;
        if ((out_len_p[i] != m_SndKmMsg[ki].MsgLen)
                ||  (0 != memcmp(out_p[i], m_SndKmMsg[ki].Msg, m_SndKmMsg[ki].MsgLen))) 
        {

            uint8_t* oldkey SRT_ATR_UNUSED = m_SndKmMsg[ki].Msg;
            HLOGC(cnlog.Debug, log << "new key[" << ki << "] index=" << kix
                    << " OLD=[" << m_SndKmMsg[ki].MsgLen << "]"
                    << FormatBinaryString(m_SndKmMsg[ki].Msg, m_SndKmMsg[ki].MsgLen)
                    << " NEW=[" << out_len_p[i] << "]"
                    << FormatBinaryString((const uint8_t*)out_p[i], out_len_p[i]));

            /* New Keying material, send to peer */
            memcpy((m_SndKmMsg[ki].Msg), out_p[i], out_len_p[i]);
            m_SndKmMsg[ki].MsgLen = out_len_p[i];
            m_SndKmMsg[ki].iPeerRetry = SRT_MAX_KMRETRY;  

            if (bidirectional && !sock)
            {
                // "Send" this key also to myself, just to be applied to the receiver crypto,
                // exactly the same way how this key is interpreted on the peer side into its receiver crypto
                int rc = HaiCrypt_Rx_Process(m_hRcvCrypto, m_SndKmMsg[ki].Msg, m_SndKmMsg[ki].MsgLen, NULL, NULL, 0);
                if ( rc < 0 )
                {
                    LOGC(cnlog.Fatal, log << "regenCryptoKm: IPE: applying key generated in snd crypto into rcv crypto: failed code=" << rc);
                    // The party won't be able to decrypt incoming data!
                    // Not sure if anything has to be reported.
                }
            }

            if (sock)
            {
                HLOGC(cnlog.Debug, log << "regenCryptoKm: SENDING ki=" << ki << " len=" << m_SndKmMsg[ki].MsgLen
                        << " retry(updated)=" << m_SndKmMsg[ki].iPeerRetry);
                sock->sendSrtMsg(SRT_CMD_KMREQ, (uint32_t *)m_SndKmMsg[ki].Msg, m_SndKmMsg[ki].MsgLen / sizeof(uint32_t));
                sent++;
            }
        }
        else if (out_len_p[i] == 0)
        {
            HLOGC(cnlog.Debug, log << "no key[" << ki << "] index=" << kix << ": not generated");
        }
        else
        {
            HLOGC(cnlog.Debug, log << "no key[" << ki << "] index=" << kix << ": key unchanged");
        }
    }

    HLOGC(cnlog.Debug, log << "regenCryptoKm: key[0]: len=" << m_SndKmMsg[0].MsgLen << " retry=" << m_SndKmMsg[0].iPeerRetry
            << "; key[1]: len=" << m_SndKmMsg[1].MsgLen << " retry=" << m_SndKmMsg[1].iPeerRetry);

    if (sent)
        m_SndKmLastTime = srt::sync::steady_clock::now();
}
#endif

srt::CCryptoControl::CCryptoControl(SRTSOCKET id)
    : m_SocketID(id)
    , m_iSndKmKeyLen(0)
    , m_iRcvKmKeyLen(0)
    , m_SndKmState(SRT_KM_S_UNSECURED)
    , m_RcvKmState(SRT_KM_S_UNSECURED)
    , m_KmRefreshRatePkt(0)
    , m_KmPreAnnouncePkt(0)
    , m_bErrorReported(false)
{

    m_KmSecret.len = 0;
    //send
    m_SndKmMsg[0].MsgLen = 0;
    m_SndKmMsg[0].iPeerRetry = 0;
    m_SndKmMsg[1].MsgLen = 0;
    m_SndKmMsg[1].iPeerRetry = 0;
    m_hSndCrypto = NULL;
    //recv
    m_hRcvCrypto = NULL;
}

bool srt::CCryptoControl::init(HandshakeSide side, const CSrtConfig& cfg, bool bidirectional SRT_ATR_UNUSED)
{
    // NOTE: initiator creates m_hSndCrypto. When bidirectional,
    // it creates also m_hRcvCrypto with the same key length.
    // Acceptor creates nothing - it will create appropriate
    // contexts when receiving KMREQ from the initiator.

    HLOGC(cnlog.Debug, log << "CCryptoControl::init: HS SIDE:"
        << (side == HSD_INITIATOR ? "INITIATOR" : "RESPONDER")
        << " DIRECTION:" << (bidirectional ? "BOTH" : (side == HSD_INITIATOR) ? "SENDER" : "RECEIVER"));

    // Set UNSECURED state as default
    m_RcvKmState = SRT_KM_S_UNSECURED;

    // Set security-pending state, if a password was set.
    m_SndKmState = hasPassphrase() ? SRT_KM_S_SECURING : SRT_KM_S_UNSECURED;

    m_KmPreAnnouncePkt = cfg.uKmPreAnnouncePkt;
    m_KmRefreshRatePkt = cfg.uKmRefreshRatePkt;

    if ( side == HSD_INITIATOR )
    {
        if (hasPassphrase())
        {
#ifdef SRT_ENABLE_ENCRYPTION
            if (m_iSndKmKeyLen == 0)
            {
                HLOGC(cnlog.Debug, log << "CCryptoControl::init: PBKEYLEN still 0, setting default 16");
                m_iSndKmKeyLen = 16;
            }

            bool ok = createCryptoCtx(m_iSndKmKeyLen, HAICRYPT_CRYPTO_DIR_TX, (m_hSndCrypto));
            HLOGC(cnlog.Debug, log << "CCryptoControl::init: creating SND crypto context: " << ok);

            if (ok && bidirectional)
            {
                m_iRcvKmKeyLen = m_iSndKmKeyLen;
                int st = HaiCrypt_Clone(m_hSndCrypto, HAICRYPT_CRYPTO_DIR_RX, &m_hRcvCrypto);
                HLOGC(cnlog.Debug, log << "CCryptoControl::init: creating CLONED RCV crypto context: status=" << st);
                ok = st == 0;
            }

            // Note: this is sanity check, it should never happen.
            if (!ok)
            {
                m_SndKmState = SRT_KM_S_NOSECRET; // wanted to secure, but error occurred.
                if (bidirectional)
                    m_RcvKmState = SRT_KM_S_NOSECRET;

                return false;
            }

            regenCryptoKm(
                NULL, // Do not send the key (the KM msg will be attached to the HSv5 handshake)
                bidirectional // replicate the key to the receiver context, if bidirectional
            );
#else
            // This error would be a consequence of setting the passphrase, while encryption
            // is turned off at compile time. Setting the password itself should be not allowed
            // so this could only happen as a consequence of an IPE.
            LOGC(cnlog.Error, log << "CCryptoControl::init: IPE: encryption not supported");
            return true;
#endif
        }
        else
        {
            HLOGC(cnlog.Debug, log << "CCryptoControl::init: CAN'T CREATE crypto: key length for SND = " << m_iSndKmKeyLen);
        }
    }
    else
    {
        HLOGC(cnlog.Debug, log << "CCryptoControl::init: NOT creating crypto contexts - will be created upon reception of KMREQ");
    }

    return true;
}

void srt::CCryptoControl::close() 
{
    /* Wipeout secrets */
    memset(&m_KmSecret, 0, sizeof(m_KmSecret));
}

std::string srt::CCryptoControl::CONID() const
{
    if (m_SocketID == 0)
        return "";

    std::ostringstream os;
    os << "@" << m_SocketID << ":";

    return os.str();
}

#ifdef SRT_ENABLE_ENCRYPTION

#if ENABLE_HEAVY_LOGGING
namespace srt {
static std::string CryptoFlags(int flg)
{
    using namespace std;

    vector<string> f;
    if (flg & HAICRYPT_CFG_F_CRYPTO)
        f.push_back("crypto");
    if (flg & HAICRYPT_CFG_F_TX)
        f.push_back("TX");
    if (flg & HAICRYPT_CFG_F_FEC)
        f.push_back("fec");

    ostringstream os;
    copy(f.begin(), f.end(), ostream_iterator<string>(os, "|"));
    return os.str();
}
} // namespace srt
#endif // ENABLE_HEAVY_LOGGING

bool srt::CCryptoControl::createCryptoCtx(size_t keylen, HaiCrypt_CryptoDir cdir, HaiCrypt_Handle& w_hCrypto)
{

    if (w_hCrypto)
    {
        // XXX You can check here if the existing handle represents
        // a correctly defined crypto. But this doesn't seem to be
        // necessary - the whole CCryptoControl facility seems to be valid only
        // within the frames of one connection.
        return true;
    }

    if ((m_KmSecret.len <= 0) || (keylen <= 0))
    {
        LOGC(cnlog.Error, log << CONID() << "cryptoCtx: IPE missing secret (" << m_KmSecret.len << ") or key length (" << keylen << ")");
        return false;
    }

    HaiCrypt_Cfg crypto_cfg;
    memset(&crypto_cfg, 0, sizeof(crypto_cfg));
#if 0//test key refresh (fast rate)
    m_KmRefreshRatePkt = 2000;
    m_KmPreAnnouncePkt = 500;
#endif
    crypto_cfg.flags = HAICRYPT_CFG_F_CRYPTO | (cdir == HAICRYPT_CRYPTO_DIR_TX ? HAICRYPT_CFG_F_TX : 0);
    crypto_cfg.xport = HAICRYPT_XPT_SRT;
    crypto_cfg.cryspr = HaiCryptCryspr_Get_Instance();
    crypto_cfg.key_len = (size_t)keylen;
    crypto_cfg.data_max_len = HAICRYPT_DEF_DATA_MAX_LENGTH;    //MTU
    crypto_cfg.km_tx_period_ms = 0;//No HaiCrypt KM inject period, handled in SRT;
    crypto_cfg.km_refresh_rate_pkt = m_KmRefreshRatePkt == 0 ? HAICRYPT_DEF_KM_REFRESH_RATE : m_KmRefreshRatePkt;
    crypto_cfg.km_pre_announce_pkt = m_KmPreAnnouncePkt == 0 ? SRT_CRYPT_KM_PRE_ANNOUNCE : m_KmPreAnnouncePkt;
    crypto_cfg.secret = m_KmSecret;
    //memcpy(&crypto_cfg.secret, &m_KmSecret, sizeof(crypto_cfg.secret));

    HLOGC(cnlog.Debug, log << "CRYPTO CFG: flags=" << CryptoFlags(crypto_cfg.flags) << " xport=" << crypto_cfg.xport << " cryspr=" << crypto_cfg.cryspr
        << " keylen=" << crypto_cfg.key_len << " passphrase_length=" << crypto_cfg.secret.len);

    if (HaiCrypt_Create(&crypto_cfg, (&w_hCrypto)) != HAICRYPT_OK)
    {
        LOGC(cnlog.Error, log << CONID() << "cryptoCtx: could not create " << (cdir == HAICRYPT_CRYPTO_DIR_TX ? "tx" : "rx") << " crypto ctx");
        return false;
    }

    HLOGC(cnlog.Debug, log << CONID() << "cryptoCtx: CREATED crypto for dir=" << (cdir == HAICRYPT_CRYPTO_DIR_TX ? "tx" : "rx") << " keylen=" << keylen);

    return true;
}
#else
bool srt::CCryptoControl::createCryptoCtx(size_t, HaiCrypt_CryptoDir, HaiCrypt_Handle&)
{
    return false;
}
#endif // SRT_ENABLE_ENCRYPTION


srt::EncryptionStatus srt::CCryptoControl::encrypt(CPacket& w_packet SRT_ATR_UNUSED)
{
#ifdef SRT_ENABLE_ENCRYPTION
    // Encryption not enabled - do nothing.
    if ( getSndCryptoFlags() == EK_NOENC )
        return ENCS_CLEAR;

    int rc = HaiCrypt_Tx_Data(m_hSndCrypto, ((uint8_t*)w_packet.getHeader()), ((uint8_t*)w_packet.m_pcData), w_packet.getLength());
    if (rc < 0)
    {
        return ENCS_FAILED;
    }
    else if ( rc > 0 )
    {
        // XXX what happens if the encryption is said to be "succeeded",
        // but the length is 0? Shouldn't this be treated as unwanted?
        w_packet.setLength(rc);
    }

    return ENCS_CLEAR;
#else
    return ENCS_NOTSUP;
#endif
}

srt::EncryptionStatus srt::CCryptoControl::decrypt(CPacket& w_packet SRT_ATR_UNUSED)
{
#ifdef SRT_ENABLE_ENCRYPTION
    if (w_packet.getMsgCryptoFlags() == EK_NOENC)
    {
        HLOGC(cnlog.Debug, log << "CPacket::decrypt: packet not encrypted");
        return ENCS_CLEAR; // not encrypted, no need do decrypt, no flags to be modified
    }

    if (m_RcvKmState == SRT_KM_S_UNSECURED)
    {
        if (m_KmSecret.len != 0)
        {
            // We were unaware that the peer has set password,
            // but now here we are.
            m_RcvKmState = SRT_KM_S_SECURING;
            LOGC(cnlog.Note, log << "SECURITY UPDATE: Peer has surprised Agent with encryption, but KMX is pending - current packet size="
                    << w_packet.getLength() << " dropped");
            return ENCS_FAILED;
        }
        else
        {
            // Peer has set a password, but Agent did not,
            // which means that it will be unable to decrypt
            // sent payloads anyway.
            m_RcvKmState = SRT_KM_S_NOSECRET;
            LOGP(cnlog.Warn, "SECURITY FAILURE: Agent has no PW, but Peer sender has declared one, can't decrypt");
            // This only informs about the state change; it will be also caught by the condition below
        }
    }

    if (m_RcvKmState != SRT_KM_S_SECURED)
    {
        // If not "secured", it means that it won't be able to decrypt packets,
        // so there's no point to even try to send them to HaiCrypt_Rx_Data.
        // Actually the current conditions concerning m_hRcvCrypto are such that this object
        // is cretaed in case of SRT_KM_S_BADSECRET, so it will simply fail to decrypt,
        // but with SRT_KM_S_NOSECRET m_hRcvCrypto is not even created (is NULL), which
        // will then cause an error to be reported, misleadingly. Simply don't try to
        // decrypt anything as long as you are not sure that the connection is secured.

        // This problem will occur every time a packet comes in, it's worth reporting,
        // but not with every single packet arriving. Print it once and turn off the flag;
        // it will be restored at the next attempt of KMX.
        if (!m_bErrorReported)
        {
            m_bErrorReported = true;
            LOGC(cnlog.Error, log << "SECURITY STATUS: " << KmStateStr(m_RcvKmState) << " - can't decrypt w_packet.");
        }
        HLOGC(cnlog.Debug, log << "Packet still not decrypted, status=" << KmStateStr(m_RcvKmState)
                << " - dropping size=" << w_packet.getLength());
        return ENCS_FAILED;
    }

    int rc = HaiCrypt_Rx_Data(m_hRcvCrypto, ((uint8_t *)w_packet.getHeader()), ((uint8_t *)w_packet.m_pcData), w_packet.getLength());
    if ( rc <= 0 )
    {
        LOGC(cnlog.Error, log << "decrypt ERROR (IPE): HaiCrypt_Rx_Data failure=" << rc << " - returning failed decryption");
        // -1: decryption failure
        // 0: key not received yet
        return ENCS_FAILED;
    }
    // Otherwise: rc == decrypted text length.
    w_packet.setLength(rc); /* In case clr txt size is different from cipher txt */

    // Decryption succeeded. Update flags.
    w_packet.setMsgCryptoFlags(EK_NOENC);

    HLOGC(cnlog.Debug, log << "decrypt: successfully decrypted, resulting length=" << rc);
    return ENCS_CLEAR;
#else
    return ENCS_NOTSUP;
#endif
}


srt::CCryptoControl::~CCryptoControl()
{
#ifdef SRT_ENABLE_ENCRYPTION
    close();
    if (m_hSndCrypto)
    {
        HaiCrypt_Close(m_hSndCrypto);
    }

    if (m_hRcvCrypto)
    {
        HaiCrypt_Close(m_hRcvCrypto);
    }
#endif
}
