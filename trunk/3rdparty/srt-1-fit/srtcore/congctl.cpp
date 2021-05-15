/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */


// This is a controversial thing, so temporarily blocking
//#define SRT_ENABLE_SYSTEMBUFFER_TRACE




#ifdef SRT_ENABLE_SYSTEMBUFFER_TRACE
#if defined(unix)
// XXX will be nonportable
#include <sys/ioctl.h>
#endif
#endif

#include <string>
#include <cmath>


#include "common.h"
#include "core.h"
#include "queue.h"
#include "packet.h"
#include "congctl.h"
#include "logging.h"

using namespace std;
using namespace srt_logging;

SrtCongestionControlBase::SrtCongestionControlBase(CUDT* parent)
{
    m_parent = parent;
    m_dMaxCWndSize = m_parent->flowWindowSize();
    // RcvRate (deliveryRate()), RTT and Bandwidth can be read directly from CUDT when needed.
    m_dCWndSize = 1000;
    m_dPktSndPeriod = 1;
}

void SrtCongestion::Check()
{
    if (!congctl)
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
}

// Useful macro to shorthand passing a method as argument
// Requires "Me" name by which a class refers to itself
#define SSLOT(method) EventSlot(this, &Me:: method)

class LiveCC: public SrtCongestionControlBase
{
    int64_t  m_llSndMaxBW;          //Max bandwidth (bytes/sec)
    size_t   m_zSndAvgPayloadSize;  //Average Payload Size of packets to xmit
    size_t   m_zMaxPayloadSize;

    // NAKREPORT stuff.
    int m_iMinNakInterval_us;                       // Minimum NAK Report Period (usec)
    int m_iNakReportAccel;                       // NAK Report Period (RTT) accelerator

    typedef LiveCC Me; // required for SSLOT macro

public:

    LiveCC(CUDT* parent)
        : SrtCongestionControlBase(parent)
    {
        m_llSndMaxBW = BW_INFINITE;    // 1 Gbbps in Bytes/sec BW_INFINITE
        m_zMaxPayloadSize = parent->OPT_PayloadSize();
        if ( m_zMaxPayloadSize == 0 )
            m_zMaxPayloadSize = parent->maxPayloadSize();
        m_zSndAvgPayloadSize = m_zMaxPayloadSize;

        m_iMinNakInterval_us = 20000;   //Minimum NAK Report Period (usec)
        m_iNakReportAccel = 2;       //Default NAK Report Period (RTT) accelerator

        HLOGC(cclog.Debug, log << "Creating LiveCC: bw=" << m_llSndMaxBW << " avgplsize=" << m_zSndAvgPayloadSize);

        updatePktSndPeriod();


        // NOTE: TEV_SEND gets dispatched from Sending thread, all others
        // from receiving thread.
        parent->ConnectSignal(TEV_SEND, SSLOT(updatePayloadSize));

        /*
         * Readjust the max SndPeriod onACK (and onTimeout)
         */
        parent->ConnectSignal(TEV_CHECKTIMER, SSLOT(updatePktSndPeriod_onTimer));
        parent->ConnectSignal(TEV_ACK, SSLOT(updatePktSndPeriod_onAck));
    }

    bool checkTransArgs(SrtCongestion::TransAPI api, SrtCongestion::TransDir dir, const char* , size_t size, int , bool ) ATR_OVERRIDE
    {
        if (api != SrtCongestion::STA_MESSAGE)
        {
            LOGC(cclog.Error, log << "LiveCC: invalid API use. Only sendmsg/recvmsg allowed.");
            return false;
        }

        if (dir == SrtCongestion::STAD_SEND)
        {
            // For sending, check if the size of data doesn't exceed the maximum live packet size.
            if (size > m_zMaxPayloadSize)
            {
                LOGC(cclog.Error, log << "LiveCC: payload size: " << size << " exceeds maximum allowed " << m_zMaxPayloadSize);
                return false;
            }
        }
        else
        {
            // For receiving, check if the buffer has enough space to keep the payload.
            if (size < m_zMaxPayloadSize)
            {
                LOGC(cclog.Error, log << "LiveCC: buffer size: " << size << " is too small for the maximum possible " << m_zMaxPayloadSize);
                return false;
            }
        }

        return true;
    }

    // XXX You can decide here if the not-fully-packed packet should require immediate ACK or not.
    // bool needsQuickACK(const CPacket& pkt) ATR_OVERRIDE

    virtual int64_t sndBandwidth() ATR_OVERRIDE { return m_llSndMaxBW; }

private:
    // SLOTS:

    // TEV_SEND -> CPacket*.
    void updatePayloadSize(ETransmissionEvent, EventVariant var)
    {
        const CPacket& packet = *var.get<EventVariant::PACKET>();

        // XXX NOTE: TEV_SEND is sent from CSndQueue::worker thread, which is
        // different to threads running any other events (TEV_CHECKTIMER and TEV_ACK).
        // The m_zSndAvgPayloadSize field is however left unguarded because 
        // there's no other modifier of this field.
        // Worst case scenario, the procedure running in CRcvQueue::worker
        // thread will pick up a "slightly outdated" average value from this
        // field - this is insignificant.
        m_zSndAvgPayloadSize = avg_iir<128, size_t>(m_zSndAvgPayloadSize, packet.getLength());
        HLOGC(cclog.Debug, log << "LiveCC: avg payload size updated: " << m_zSndAvgPayloadSize);
    }

    void updatePktSndPeriod_onTimer(ETransmissionEvent , EventVariant var)
    {
        if ( var.get<EventVariant::STAGE>() != TEV_CHT_INIT )
            updatePktSndPeriod();
    }

    void updatePktSndPeriod_onAck(ETransmissionEvent , EventVariant )
    {
        updatePktSndPeriod();
    }

    void updatePktSndPeriod()
    {
        // packet = payload + header
        const double pktsize = (double) m_zSndAvgPayloadSize + CPacket::SRT_DATA_HDR_SIZE;
        m_dPktSndPeriod = 1000 * 1000.0 * (pktsize / m_llSndMaxBW);
        HLOGC(cclog.Debug, log << "LiveCC: sending period updated: " << m_dPktSndPeriod
            << " (pktsize=" << pktsize << ", bw=" << m_llSndMaxBW);
    }

    void setMaxBW(int64_t maxbw)
    {
        m_llSndMaxBW = maxbw > 0 ? maxbw : BW_INFINITE;
        updatePktSndPeriod();

#ifdef SRT_ENABLE_NOCWND
        /*
         * UDT default flow control should not trigger under normal SRT operation
         * UDT stops sending if the number of packets in transit (not acknowledged)
         * is larger than the congestion window.
         * Up to SRT 1.0.6, this value was set at 1000 pkts, which may be insufficient
         * for satellite links with ~1000 msec RTT and high bit rate.
         */
        // XXX Consider making this a socket option.
        m_dCWndSize = m_dMaxCWndSize;
#else
        m_dCWndSize = 1000;
#endif
    }

    void updateBandwidth(int64_t maxbw, int64_t bw) ATR_OVERRIDE
    {
        // bw is the bandwidth calculated with regard to the
        // SRTO_INPUTBW and SRTO_OHEADBW parameters. The maxbw
        // value simply represents the SRTO_MAXBW setting.
        if (maxbw)
        {
            setMaxBW(maxbw);
            return;
        }

        if (bw == 0)
        {
            return;
        }

        setMaxBW(bw);
    }

    SrtCongestion::RexmitMethod rexmitMethod() ATR_OVERRIDE
    {
        return SrtCongestion::SRM_FASTREXMIT;
    }

    uint64_t updateNAKInterval(uint64_t nakint_tk, int /*rcv_speed*/, size_t /*loss_length*/) ATR_OVERRIDE
    {
        /*
         * duB:
         * The RTT accounts for the time for the last NAK to reach sender and start resending lost pkts.
         * The rcv_speed add the time to resend all the pkts in the loss list.
         * 
         * For realtime Transport Stream content, pkts/sec is not a good indication of time to transmit
         * since packets are not filled to m_iMSS and packet size average is lower than (7*188)
         * for low bit rates.
         * If NAK report is lost, another cycle (RTT) is requred which is bad for low latency so we
         * accelerate the NAK Reports frequency, at the cost of possible duplicate resend.
         * Finally, the UDT4 native minimum NAK interval (m_ullMinNakInt_tk) is 300 ms which is too high
         * (~10 i30 video frames) to maintain low latency.
         */

        // Note: this value will still be reshaped to defined minimum,
        // as per minNAKInterval.
        return nakint_tk / m_iNakReportAccel;
    }

    uint64_t minNAKInterval() ATR_OVERRIDE
    {
        return m_iMinNakInterval_us * CTimer::getCPUFrequency();
    }

};


class FileCC : public SrtCongestionControlBase
{
    typedef FileCC Me; // Required by SSLOT macro

    // Fields from CUDTCC
    int m_iRCInterval;          // UDT Rate control interval
    uint64_t m_LastRCTime;      // last rate increase time
    bool m_bSlowStart;          // if in slow start phase
    int32_t m_iLastAck;         // last ACKed seq no
    bool m_bLoss;               // if loss happened since last rate increase
    int32_t m_iLastDecSeq;      // max pkt seq no sent out when last decrease happened
    double m_dLastDecPeriod;    // value of pktsndperiod when last decrease happened
    int m_iNAKCount;            // NAK counter
    int m_iDecRandom;           // random threshold on decrease by number of loss events
    int m_iAvgNAKNum;           // average number of NAKs per congestion
    int m_iDecCount;            // number of decreases in a congestion epoch

    int64_t m_maxSR;

public:

    FileCC(CUDT* parent)
        : SrtCongestionControlBase(parent)
        , m_iRCInterval(CUDT::COMM_SYN_INTERVAL_US)
        , m_LastRCTime(CTimer::getTime())
        , m_bSlowStart(true)
        , m_iLastAck(parent->sndSeqNo())
        , m_bLoss(false)
        , m_iLastDecSeq(CSeqNo::decseq(m_iLastAck))
        , m_dLastDecPeriod(1)
        , m_iNAKCount(0)
        , m_iDecRandom(1)
        , m_iAvgNAKNum(0)
        , m_iDecCount(0)
        , m_maxSR(0)
    {
        // Note that this function is called at the moment of
        // calling m_Smoother.configure(this). It is placed more less
        // at the same position as the series-of-parameter-setting-then-init
        // in the original UDT code. So, old CUDTCC::init() can be moved
        // to constructor.

        // SmotherBase
        m_dCWndSize = 16;
        m_dPktSndPeriod = 1;

        parent->ConnectSignal(TEV_ACK,        SSLOT(updateSndPeriod));
        parent->ConnectSignal(TEV_LOSSREPORT, SSLOT(slowdownSndPeriod));
        parent->ConnectSignal(TEV_CHECKTIMER, SSLOT(speedupToWindowSize));

        HLOGC(cclog.Debug, log << "Creating FileCC");
    }

    bool checkTransArgs(SrtCongestion::TransAPI, SrtCongestion::TransDir, const char*, size_t, int, bool) ATR_OVERRIDE
    {
        // XXX
        // The FileCC has currently no restrictions, although it should be
        // rather required that the "message" mode or "buffer" mode be used on both sides the same.
        // This must be somehow checked separately.
        return true;
    }

    bool needsQuickACK(const CPacket& pkt) ATR_OVERRIDE
    {
        // For FileCC, treat non-full-buffer situation as an end-of-message situation;
        // request ACK to be sent immediately.
        if (pkt.getLength() < m_parent->maxPayloadSize())
        {
            // This is not a regular fixed size packet...
            // an irregular sized packet usually indicates the end of a message, so send an ACK immediately
            return true;
        }

        return false;
    }

    void updateBandwidth(int64_t maxbw, int64_t) ATR_OVERRIDE
    {
        if (maxbw != 0)
        {
            m_maxSR = maxbw;
            HLOGC(cclog.Debug, log << "FileCC: updated BW: " << m_maxSR);
        }
    }

private:

    // SLOTS
    void updateSndPeriod(ETransmissionEvent, EventVariant arg)
    {
        const int ack = arg.get<EventVariant::ACK>();

        const uint64_t currtime = CTimer::getTime();
        if (currtime - m_LastRCTime < (uint64_t)m_iRCInterval)
            return;

        m_LastRCTime = currtime;

        if (m_bSlowStart)
        {
            m_dCWndSize += CSeqNo::seqlen(m_iLastAck, ack);
            m_iLastAck = ack;

            if (m_dCWndSize > m_dMaxCWndSize)
            {
                m_bSlowStart = false;
                if (m_parent->deliveryRate() > 0)
                {
                    m_dPktSndPeriod = 1000000.0 / m_parent->deliveryRate();
                    HLOGC(cclog.Debug, log << "FileCC: UPD (slowstart:ENDED) wndsize="
                        << m_dCWndSize << "/" << m_dMaxCWndSize
                        << " sndperiod=" << m_dPktSndPeriod << "us = 1M/("
                        << m_parent->deliveryRate() << " pkts/s)");
                }
                else
                {
                    m_dPktSndPeriod = m_dCWndSize / (m_parent->RTT() + m_iRCInterval);
                    HLOGC(cclog.Debug, log << "FileCC: UPD (slowstart:ENDED) wndsize="
                        << m_dCWndSize << "/" << m_dMaxCWndSize
                        << " sndperiod=" << m_dPktSndPeriod << "us = wndsize/(RTT+RCIV) RTT="
                        << m_parent->RTT() << " RCIV=" << m_iRCInterval);
                }
            }
            else
            {
                HLOGC(cclog.Debug, log << "FileCC: UPD (slowstart:KEPT) wndsize="
                    << m_dCWndSize << "/" << m_dMaxCWndSize
                    << " sndperiod=" << m_dPktSndPeriod << "us");
            }
        }
        else
        {
            m_dCWndSize = m_parent->deliveryRate() / 1000000.0 * (m_parent->RTT() + m_iRCInterval) + 16;
            HLOGC(cclog.Debug, log << "FileCC: UPD (speed mode) wndsize="
                << m_dCWndSize << "/" << m_dMaxCWndSize << " RTT = " << m_parent->RTT()
                << " sndperiod=" << m_dPktSndPeriod << "us. deliverRate = "
                << m_parent->deliveryRate() << " pkts/s)");
        }

        if (!m_bSlowStart)
        {
            if (m_bLoss)
            {
                m_bLoss = false;
            }
            // During Slow Start, no rate increase
            else
            {
                double inc = 0;
                const int loss_bw = 2 * (1000000 / m_dLastDecPeriod); // 2 times last loss point
                const int bw_pktps = min(loss_bw, m_parent->bandwidth());

                int64_t B = (int64_t)(bw_pktps - 1000000.0 / m_dPktSndPeriod);
                if ((m_dPktSndPeriod > m_dLastDecPeriod) && ((bw_pktps / 9) < B))
                    B = bw_pktps / 9;
                if (B <= 0)
                    inc = 1.0 / m_parent->MSS();
                else
                {
                    // inc = max(10 ^ ceil(log10( B * MSS * 8 ) * Beta / MSS, 1/MSS)
                    // Beta = 1.5 * 10^(-6)

                    inc = pow(10.0, ceil(log10(B * m_parent->MSS() * 8.0))) * 0.0000015 / m_parent->MSS();
                    inc = max(inc, 1.0 / m_parent->MSS());
                }

                HLOGC(cclog.Debug, log << "FileCC: UPD (slowstart:OFF) loss_bw=" << loss_bw
                    << " bandwidth=" << m_parent->bandwidth() << " inc=" << inc
                    << " m_dPktSndPeriod=" << m_dPktSndPeriod
                    << "->" << (m_dPktSndPeriod * m_iRCInterval) / (m_dPktSndPeriod * inc + m_iRCInterval));

                m_dPktSndPeriod = (m_dPktSndPeriod * m_iRCInterval) / (m_dPktSndPeriod * inc + m_iRCInterval);
            }
        }

#if ENABLE_HEAVY_LOGGING
        // Try to do reverse-calculation for m_dPktSndPeriod, as per minSP below
        // sndperiod = mega / (maxbw / MSS)
        // 1/sndperiod = (maxbw/MSS) / mega
        // mega/sndperiod = maxbw/MSS
        // maxbw = (MSS*mega)/sndperiod
        uint64_t usedbw = (m_parent->MSS() * 1000000.0) / m_dPktSndPeriod;

#if defined(unix) && defined (SRT_ENABLE_SYSTEMBUFFER_TRACE)
        // Check the outgoing system queue level
        int udp_buffer_size = m_parent->sndQueue()->sockoptQuery(SOL_SOCKET, SO_SNDBUF);
        int udp_buffer_level = m_parent->sndQueue()->ioctlQuery(TIOCOUTQ);
        int udp_buffer_free = udp_buffer_size - udp_buffer_level;
#else
        int udp_buffer_free = -1;
#endif

        HLOGC(cclog.Debug, log << "FileCC: UPD (slowstart:"
            << (m_bSlowStart ? "ON" : "OFF") << ") wndsize=" << m_dCWndSize
            << " sndperiod=" << m_dPktSndPeriod << "us BANDWIDTH USED:" << usedbw << " (limit: " << m_maxSR << ")"
            " SYSTEM BUFFER LEFT: " << udp_buffer_free);
#endif

        //set maximum transfer rate
        if (m_maxSR)
        {
            double minSP = 1000000.0 / (double(m_maxSR) / m_parent->MSS());
            if (m_dPktSndPeriod < minSP)
            {
                m_dPktSndPeriod = minSP;
                HLOGC(cclog.Debug, log << "FileCC: BW limited to " << m_maxSR
                    << " - SLOWDOWN sndperiod=" << m_dPktSndPeriod << "us");
            }
        }

    }

    // When a lossreport has been received, it might be due to having
    // reached the available bandwidth limit. Slowdown to avoid further losses.
    void slowdownSndPeriod(ETransmissionEvent, EventVariant arg)
    {
        const int32_t* losslist = arg.get_ptr();
        size_t losslist_size = arg.get_len();

        // Sanity check. Should be impossible that TEV_LOSSREPORT event
        // is called with a nonempty loss list.
        if (losslist_size == 0)
        {
            LOGC(cclog.Error, log << "IPE: FileCC: empty loss list!");
            return;
        }

        //Slow Start stopped, if it hasn't yet
        if (m_bSlowStart)
        {
            m_bSlowStart = false;
            if (m_parent->deliveryRate() > 0)
            {
                m_dPktSndPeriod = 1000000.0 / m_parent->deliveryRate();
                HLOGC(cclog.Debug, log << "FileCC: LOSS, SLOWSTART:OFF, sndperiod=" << m_dPktSndPeriod << "us AS mega/rate (rate="
                    << m_parent->deliveryRate() << ")");
            }
            else
            {
                m_dPktSndPeriod = m_dCWndSize / (m_parent->RTT() + m_iRCInterval);
                HLOGC(cclog.Debug, log << "FileCC: LOSS, SLOWSTART:OFF, sndperiod=" << m_dPktSndPeriod << "us AS wndsize/(RTT+RCIV) (RTT="
                    << m_parent->RTT() << " RCIV=" << m_iRCInterval << ")");
            }

        }

        m_bLoss = true;

        // TODO: const int pktsInFlight = CSeqNo::seqoff(m_iLastAck, m_parent->sndSeqNo());
        const int pktsInFlight = m_parent->RTT() / m_dPktSndPeriod;
        const int numPktsLost = m_parent->sndLossLength();
        const int lost_pcent_x10 = pktsInFlight > 0 ? (numPktsLost * 1000) / pktsInFlight : 0;

        HLOGC(cclog.Debug, log << "FileCC: LOSS: "
            << "sent=" << CSeqNo::seqlen(m_iLastAck, m_parent->sndSeqNo()) << ", inFlight=" << pktsInFlight
            << ", lost=" << numPktsLost << " ("
            << lost_pcent_x10 / 10 << "." << lost_pcent_x10 % 10 << "\%)");
        if (lost_pcent_x10 < 20)    // 2.0%
        {
            HLOGC(cclog.Debug, log << "FileCC: LOSS: m_dLastDecPeriod=" << m_dLastDecPeriod << "->" << m_dPktSndPeriod);
            m_dLastDecPeriod = m_dPktSndPeriod;
            return;
        }

        // In contradiction to UDT, TEV_LOSSREPORT will be reported also when
        // the lossreport is being sent again, periodically, as a result of
        // NAKREPORT feature. You should make sure that NAKREPORT is off when
        // using FileCC, so relying on SRTO_TRANSTYPE rather than
        // just SRTO_CONGESTION is recommended.
        int32_t lossbegin = SEQNO_VALUE::unwrap(losslist[0]);

        if (CSeqNo::seqcmp(lossbegin, m_iLastDecSeq) > 0)
        {
            m_dLastDecPeriod = m_dPktSndPeriod;
            m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.03);

            const double loss_share_factor = 0.03;
            m_iAvgNAKNum = (int)ceil(m_iAvgNAKNum * (1 - loss_share_factor) + m_iNAKCount * loss_share_factor);
            m_iNAKCount = 1;
            m_iDecCount = 1;

            m_iLastDecSeq = m_parent->sndSeqNo();

            // remove global synchronization using randomization
            srand(m_iLastDecSeq);
            m_iDecRandom = (int)ceil(m_iAvgNAKNum * (double(rand()) / RAND_MAX));
            if (m_iDecRandom < 1)
                m_iDecRandom = 1;
            HLOGC(cclog.Debug, log << "FileCC: LOSS:NEW lseqno=" << lossbegin
                << ", lastsentseqno=" << m_iLastDecSeq
                << ", seqdiff=" << CSeqNo::seqoff(m_iLastDecSeq, lossbegin)
                << ", rand=" << m_iDecRandom
                << " avg NAK:" << m_iAvgNAKNum
                << ", sndperiod=" << m_dPktSndPeriod << "us");
        }
        else if ((m_iDecCount++ < 5) && (0 == (++m_iNAKCount % m_iDecRandom)))
        {
            // 0.875^5 = 0.51, rate should not be decreased by more than half within a congestion period
            m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.03);
            m_iLastDecSeq = m_parent->sndSeqNo();
            HLOGC(cclog.Debug, log << "FileCC: LOSS:PERIOD lseqno=" << lossbegin
                << ", lastsentseqno=" << m_iLastDecSeq
                << ", seqdiff=" << CSeqNo::seqoff(m_iLastDecSeq, lossbegin)
                << ", deccnt=" << m_iDecCount
                << ", decrnd=" << m_iDecRandom
                << ", sndperiod=" << m_dPktSndPeriod << "us");
        }
        else
        {
            HLOGC(cclog.Debug, log << "FileCC: LOSS:STILL lseqno=" << lossbegin
                << ", lastsentseqno=" << m_iLastDecSeq
                << ", seqdiff=" << CSeqNo::seqoff(m_iLastDecSeq, lossbegin)
                << ", deccnt=" << m_iDecCount
                << ", decrnd=" << m_iDecRandom
                << ", sndperiod=" << m_dPktSndPeriod << "us");
        }
    }

    void speedupToWindowSize(ETransmissionEvent, EventVariant arg)
    {
        ECheckTimerStage stg = arg.get<EventVariant::STAGE>();

        // TEV_INIT is in the beginning of checkTimers(), used
        // only to synchronize back the values (which is done in updateCC
        // after emitting the signal).
        if (stg == TEV_CHT_INIT)
            return;

        if (m_bSlowStart)
        {
            m_bSlowStart = false;
            if (m_parent->deliveryRate() > 0)
            {
                m_dPktSndPeriod = 1000000.0 / m_parent->deliveryRate();
                HLOGC(cclog.Debug, log << "FileCC: CHKTIMER, SLOWSTART:OFF, sndperiod=" << m_dPktSndPeriod << "us AS mega/rate (rate="
                    << m_parent->deliveryRate() << ")");
            }
            else
            {
                m_dPktSndPeriod = m_dCWndSize / (m_parent->RTT() + m_iRCInterval);
                HLOGC(cclog.Debug, log << "FileCC: CHKTIMER, SLOWSTART:OFF, sndperiod=" << m_dPktSndPeriod << "us AS wndsize/(RTT+RCIV) (wndsize="
                    << setprecision(6) << m_dCWndSize << " RTT=" << m_parent->RTT() << " RCIV=" << m_iRCInterval << ")");
            }
        }
        else
        {
            // XXX This code is a copy of legacy CUDTCC::onTimeout() body.
            // This part was commented out there already.
            /*
               m_dLastDecPeriod = m_dPktSndPeriod;
               m_dPktSndPeriod = ceil(m_dPktSndPeriod * 2);
               m_iLastDecSeq = m_iLastAck;
             */
        }
    }

    SrtCongestion::RexmitMethod rexmitMethod() ATR_OVERRIDE
    {
        return SrtCongestion::SRM_LATEREXMIT;
    }
};


#undef SSLOT

template <class Target>
struct Creator
{
    static SrtCongestionControlBase* Create(CUDT* parent) { return new Target(parent); }
};

SrtCongestion::NamePtr SrtCongestion::congctls[N_CONTROLLERS] =
{
    {"live", Creator<LiveCC>::Create },
    {"file", Creator<FileCC>::Create }
};


bool SrtCongestion::configure(CUDT* parent)
{
    if (selector == N_CONTROLLERS)
        return false;

    // Found a congctl, so call the creation function
    congctl = (*congctls[selector].second)(parent);

    // The congctl should have pinned in all events
    // that are of its interest. It's stated that
    // it's ready after creation.
    return !!congctl;
}

SrtCongestion::~SrtCongestion()
{
    delete congctl;
    congctl = 0;
}
