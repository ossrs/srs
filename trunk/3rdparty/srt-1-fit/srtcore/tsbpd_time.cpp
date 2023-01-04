/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2021 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */
#include "tsbpd_time.h"

#include "logging.h"
#include "logger_defs.h"
#include "packet.h"

using namespace srt_logging;
using namespace srt::sync;

namespace srt
{

#if SRT_DEBUG_TRACE_DRIFT
class drift_logger
{
    typedef srt::sync::steady_clock steady_clock;

public:
    drift_logger() {}

    ~drift_logger()
    {
        ScopedLock lck(m_mtx);
        m_fout.close();
    }

    void trace(unsigned                                   ackack_timestamp,
               int                                        rtt_us,
               int64_t                                    drift_sample,
               int64_t                                    drift,
               int64_t                                    overdrift,
               const srt::sync::steady_clock::time_point& pkt_base,
               const srt::sync::steady_clock::time_point& tsbpd_base)
    {
        using namespace srt::sync;
        ScopedLock lck(m_mtx);
        create_file();

        // std::string str_tnow = srt::sync::FormatTime(steady_clock::now());
        // str_tnow.resize(str_tnow.size() - 7); // remove trailing ' [STDY]' part

        std::string str_tbase = srt::sync::FormatTime(tsbpd_base);
        str_tbase.resize(str_tbase.size() - 7); // remove trailing ' [STDY]' part

        std::string str_pkt_base = srt::sync::FormatTime(pkt_base);
        str_pkt_base.resize(str_pkt_base.size() - 7); // remove trailing ' [STDY]' part

        // m_fout << str_tnow << ",";
        m_fout << count_microseconds(steady_clock::now() - m_start_time) << ",";
        m_fout << ackack_timestamp << ",";
        m_fout << rtt_us << ",";
        m_fout << drift_sample << ",";
        m_fout << drift << ",";
        m_fout << overdrift << ",";
        m_fout << str_pkt_base << ",";
        m_fout << str_tbase << "\n";
        m_fout.flush();
    }

private:
    void print_header()
    {
        m_fout << "usElapsedStd,usAckAckTimestampStd,";
        m_fout << "usRTTStd,usDriftSampleStd,usDriftStd,usOverdriftStd,tsPktBase,TSBPDBase\n";
    }

    void create_file()
    {
        if (m_fout.is_open())
            return;

        m_start_time         = srt::sync::steady_clock::now();
        std::string str_tnow = srt::sync::FormatTimeSys(m_start_time);
        str_tnow.resize(str_tnow.size() - 7); // remove trailing ' [SYST]' part
        while (str_tnow.find(':') != std::string::npos)
        {
            str_tnow.replace(str_tnow.find(':'), 1, 1, '_');
        }
        const std::string fname = "drift_trace_" + str_tnow + ".csv";
        m_fout.open(fname, std::ofstream::out);
        if (!m_fout)
            std::cerr << "IPE: Failed to open " << fname << "!!!\n";

        print_header();
    }

private:
    srt::sync::Mutex                    m_mtx;
    std::ofstream                       m_fout;
    srt::sync::steady_clock::time_point m_start_time;
};

drift_logger g_drift_logger;

#endif // SRT_DEBUG_TRACE_DRIFT

bool CTsbpdTime::addDriftSample(uint32_t usPktTimestamp, const time_point& tsPktArrival, int usRTTSample)
{
    if (!m_bTsbPdMode)
        return false;

    ScopedLock lck(m_mtxRW);

    // Remember the first RTT sample measured. Ideally we need RTT0 - the one from the handshaking phase,
    // because TSBPD base is initialized there. But HS-based RTT is not yet implemented.
    // Take the first one assuming it is close to RTT0.
    if (m_iFirstRTT == -1)
    {
        m_iFirstRTT = usRTTSample;
    }

    // A change in network delay has to be taken into account. The only way to get some estimation of it
    // is to estimate RTT change and assume that the change of the one way network delay is
    // approximated by the half of the RTT change.
    const duration               tdRTTDelta    = usRTTSample >= 0 ? microseconds_from((usRTTSample - m_iFirstRTT) / 2) : duration(0);
    const time_point             tsPktBaseTime = getPktTsbPdBaseTime(usPktTimestamp);
    const steady_clock::duration tdDrift       = tsPktArrival - tsPktBaseTime - tdRTTDelta;

    const bool updated = m_DriftTracer.update(count_microseconds(tdDrift));

    if (updated)
    {
        IF_HEAVY_LOGGING(const steady_clock::time_point oldbase = m_tsTsbPdTimeBase);
        steady_clock::duration overdrift = microseconds_from(m_DriftTracer.overdrift());
        m_tsTsbPdTimeBase += overdrift;

        HLOGC(brlog.Debug,
              log << "DRIFT=" << FormatDuration(tdDrift) << " AVG=" << (m_DriftTracer.drift() / 1000.0)
                  << "ms, TB: " << FormatTime(oldbase) << " EXCESS: " << FormatDuration(overdrift)
                  << " UPDATED TO: " << FormatTime(m_tsTsbPdTimeBase));
    }
    else
    {
        HLOGC(brlog.Debug,
              log << "DRIFT=" << FormatDuration(tdDrift) << " TB REMAINS: " << FormatTime(m_tsTsbPdTimeBase));
    }

#if SRT_DEBUG_TRACE_DRIFT
    g_drift_logger.trace(usPktTimestamp,
                         usRTTSample,
                         count_microseconds(tdDrift),
                         m_DriftTracer.drift(),
                         m_DriftTracer.overdrift(),
                         tsPktBaseTime,
                         m_tsTsbPdTimeBase);
#endif
    return updated;
}

void CTsbpdTime::setTsbPdMode(const steady_clock::time_point& timebase, bool wrap, duration delay)
{
    m_bTsbPdMode      = true;
    m_bTsbPdWrapCheck = wrap;

    // Timebase passed here comes is calculated as:
    // Tnow - hspkt.m_iTimeStamp
    // where hspkt is the packet with SRT_CMD_HSREQ message.
    //
    // This function is called in the HSREQ reception handler only.
    m_tsTsbPdTimeBase = timebase;
    m_tdTsbPdDelay    = delay;
}

void CTsbpdTime::applyGroupTime(const steady_clock::time_point& timebase,
                                bool                            wrp,
                                uint32_t                        delay,
                                const steady_clock::duration&   udrift)
{
    // Same as setTsbPdMode, but predicted to be used for group members.
    // This synchronizes the time from the INTERNAL TIMEBASE of an existing
    // socket's internal timebase. This is required because the initial time
    // base stays always the same, whereas the internal timebase undergoes
    // adjustment as the 32-bit timestamps in the sockets wrap. The socket
    // newly added to the group must get EXACTLY the same internal timebase
    // or otherwise the TsbPd time calculation will ship different results
    // on different member sockets.

    m_bTsbPdMode = true;

    m_tsTsbPdTimeBase = timebase;
    m_bTsbPdWrapCheck = wrp;
    m_tdTsbPdDelay    = microseconds_from(delay);
    m_DriftTracer.forceDrift(count_microseconds(udrift));
}

void CTsbpdTime::applyGroupDrift(const steady_clock::time_point& timebase,
                                 bool                            wrp,
                                 const steady_clock::duration&   udrift)
{
    // This is only when a drift was updated on one of the group members.
    HLOGC(brlog.Debug,
          log << "rcv-buffer: group synch uDRIFT: " << m_DriftTracer.drift() << " -> " << FormatDuration(udrift)
              << " TB: " << FormatTime(m_tsTsbPdTimeBase) << " -> " << FormatTime(timebase));

    m_tsTsbPdTimeBase = timebase;
    m_bTsbPdWrapCheck = wrp;

    m_DriftTracer.forceDrift(count_microseconds(udrift));
}

CTsbpdTime::time_point CTsbpdTime::getTsbPdTimeBase(uint32_t timestamp_us) const
{
    // A data packet within [TSBPD_WRAP_PERIOD; 2 * TSBPD_WRAP_PERIOD] would end TSBPD wrap-aware state.
    // Some incoming control packets may not update the TSBPD base (calling updateTsbPdTimeBase(..)),
    // but may come before a data packet with a timestamp in this range. Therefore the whole range should be tracked.
    const int64_t carryover_us =
        (m_bTsbPdWrapCheck && timestamp_us <= 2 * TSBPD_WRAP_PERIOD) ? int64_t(CPacket::MAX_TIMESTAMP) + 1 : 0;

    return (m_tsTsbPdTimeBase + microseconds_from(carryover_us));
}

CTsbpdTime::time_point CTsbpdTime::getPktTsbPdTime(uint32_t usPktTimestamp) const
{
    return getPktTsbPdBaseTime(usPktTimestamp) + m_tdTsbPdDelay + microseconds_from(m_DriftTracer.drift());
}

CTsbpdTime::time_point CTsbpdTime::getPktTsbPdBaseTime(uint32_t usPktTimestamp) const
{
    return getTsbPdTimeBase(usPktTimestamp) + microseconds_from(usPktTimestamp);
}

void CTsbpdTime::updateTsbPdTimeBase(uint32_t usPktTimestamp)
{
    if (m_bTsbPdWrapCheck)
    {
        // Wrap check period.
        if ((usPktTimestamp >= TSBPD_WRAP_PERIOD) && (usPktTimestamp <= (TSBPD_WRAP_PERIOD * 2)))
        {
            /* Exiting wrap check period (if for packet delivery head) */
            m_bTsbPdWrapCheck = false;
            m_tsTsbPdTimeBase += microseconds_from(int64_t(CPacket::MAX_TIMESTAMP) + 1);
            LOGC(tslog.Debug,
                 log << "tsbpd wrap period ends with ts=" << usPktTimestamp << " - NEW TIME BASE: "
                     << FormatTime(m_tsTsbPdTimeBase) << " drift: " << m_DriftTracer.drift() << "us");
        }
        return;
    }

    // Check if timestamp is within the TSBPD_WRAP_PERIOD before reaching the MAX_TIMESTAMP.
    if (usPktTimestamp > (CPacket::MAX_TIMESTAMP - TSBPD_WRAP_PERIOD))
    {
        // Approching wrap around point, start wrap check period (if for packet delivery head)
        m_bTsbPdWrapCheck = true;
        LOGC(tslog.Debug,
             log << "tsbpd wrap period begins with ts=" << usPktTimestamp
                 << " TIME BASE: " << FormatTime(m_tsTsbPdTimeBase) << " drift: " << m_DriftTracer.drift() << "us.");
    }
}

void CTsbpdTime::getInternalTimeBase(time_point& w_tb, bool& w_wrp, duration& w_udrift) const
{
    ScopedLock lck(m_mtxRW);
    w_tb     = m_tsTsbPdTimeBase;
    w_udrift = microseconds_from(m_DriftTracer.drift());
    w_wrp    = m_bTsbPdWrapCheck;
}

} // namespace srt
