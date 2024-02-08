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
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 03/12/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#include "platform_sys.h"
#include "buffer_tools.h"
#include "packet.h"
#include "logger_defs.h"
#include "utilities.h"

namespace srt {

using namespace std;
using namespace srt_logging;
using namespace sync;

// You can change this value at build config by using "ENFORCE" options.
#if !defined(SRT_MAVG_SAMPLING_RATE)
#define SRT_MAVG_SAMPLING_RATE 40
#endif

bool AvgBufSize::isTimeToUpdate(const time_point& now) const
{
    const int      usMAvgBasePeriod = 1000000; // 1s in microseconds
    const int      us2ms            = 1000;
    const int      msMAvgPeriod     = (usMAvgBasePeriod / SRT_MAVG_SAMPLING_RATE) / us2ms;
    const uint64_t elapsed_ms       = count_milliseconds(now - m_tsLastSamplingTime); // ms since last sampling
    return (elapsed_ms >= msMAvgPeriod);
}

void AvgBufSize::update(const steady_clock::time_point& now, int pkts, int bytes, int timespan_ms)
{
    const uint64_t elapsed_ms       = count_milliseconds(now - m_tsLastSamplingTime); // ms since last sampling
    m_tsLastSamplingTime            = now;
    const uint64_t one_second_in_ms = 1000;
    if (elapsed_ms > one_second_in_ms)
    {
        // No sampling in last 1 sec, initialize average
        m_dCountMAvg      = pkts;
        m_dBytesCountMAvg = bytes;
        m_dTimespanMAvg   = timespan_ms;
        return;
    }

    //
    // weight last average value between -1 sec and last sampling time (LST)
    // and new value between last sampling time and now
    //                                      |elapsed_ms|
    //   +----------------------------------+-------+
    //  -1                                 LST      0(now)
    //
    m_dCountMAvg      = avg_iir_w<1000, double>(m_dCountMAvg, pkts, elapsed_ms);
    m_dBytesCountMAvg = avg_iir_w<1000, double>(m_dBytesCountMAvg, bytes, elapsed_ms);
    m_dTimespanMAvg   = avg_iir_w<1000, double>(m_dTimespanMAvg, timespan_ms, elapsed_ms);
}

CRateEstimator::CRateEstimator()
    : m_iInRatePktsCount(0)
    , m_iInRateBytesCount(0)
    , m_InRatePeriod(INPUTRATE_FAST_START_US) // 0.5 sec (fast start)
    , m_iInRateBps(INPUTRATE_INITIAL_BYTESPS)
{}

void CRateEstimator::setInputRateSmpPeriod(int period)
{
    m_InRatePeriod = (uint64_t)period; //(usec) 0=no input rate calculation
}

void CRateEstimator::updateInputRate(const time_point& time, int pkts, int bytes)
{
    // no input rate calculation
    if (m_InRatePeriod == 0)
        return;

    if (is_zero(m_tsInRateStartTime))
    {
        m_tsInRateStartTime = time;
        return;
    }
    else if (time < m_tsInRateStartTime)
    {
        // Old packets are being submitted for estimation, e.g. during the backup link activation.
        return;
    }

    m_iInRatePktsCount  += pkts;
    m_iInRateBytesCount += bytes;

    // Trigger early update in fast start mode
    const bool early_update = (m_InRatePeriod < INPUTRATE_RUNNING_US) && (m_iInRatePktsCount > INPUTRATE_MAX_PACKETS);

    const uint64_t period_us = count_microseconds(time - m_tsInRateStartTime);
    if (!early_update && period_us <= m_InRatePeriod)
        return;

    // Required Byte/sec rate (payload + headers)
    m_iInRateBytesCount += (m_iInRatePktsCount * CPacket::SRT_DATA_HDR_SIZE);
    m_iInRateBps = (int)(((int64_t)m_iInRateBytesCount * 1000000) / period_us);
    HLOGC(bslog.Debug,
        log << "updateInputRate: pkts:" << m_iInRateBytesCount << " bytes:" << m_iInRatePktsCount
        << " rate=" << (m_iInRateBps * 8) / 1000 << "kbps interval=" << period_us);
    m_iInRatePktsCount  = 0;
    m_iInRateBytesCount = 0;
    m_tsInRateStartTime = time;

    setInputRateSmpPeriod(INPUTRATE_RUNNING_US);
}

CSndRateEstimator::CSndRateEstimator(const time_point& tsNow)
    : m_tsFirstSampleTime(tsNow)
    , m_iFirstSampleIdx(0)
    , m_iCurSampleIdx(0)
    , m_iRateBps(0)
{
    
}

void CSndRateEstimator::addSample(const time_point& ts, int pkts, size_t bytes)
{
    const int iSampleDeltaIdx = (int) count_milliseconds(ts - m_tsFirstSampleTime) / SAMPLE_DURATION_MS;
    const int delta = NUM_PERIODS - iSampleDeltaIdx;

    // TODO: -delta <= NUM_PERIODS, then just reset the state on the estimator.

    if (iSampleDeltaIdx >= 2 * NUM_PERIODS)
    {
        // Just reset the estimator and start like if new.
        for (int i = 0; i < NUM_PERIODS; ++i)
        {
            const int idx = incSampleIdx(m_iFirstSampleIdx, i);
            m_Samples[idx].reset();

            if (idx == m_iCurSampleIdx)
                break;
        }

        m_iFirstSampleIdx = 0;
        m_iCurSampleIdx = 0;
        m_iRateBps = 0;
        m_tsFirstSampleTime += milliseconds_from(iSampleDeltaIdx * SAMPLE_DURATION_MS);
    }
    else if (iSampleDeltaIdx > NUM_PERIODS)
    {
        // In run-time a constant flow of samples is expected. Once all periods are filled (after 1 second of sampling),
        // the iSampleDeltaIdx should be either (NUM_PERIODS - 1),
        // or NUM_PERIODS. In the later case it means the start of a new sampling period.
        int d = delta;
        while (d < 0)
        {
            m_Samples[m_iFirstSampleIdx].reset();
            m_iFirstSampleIdx = incSampleIdx(m_iFirstSampleIdx);
            m_tsFirstSampleTime += milliseconds_from(SAMPLE_DURATION_MS);
            m_iCurSampleIdx = incSampleIdx(m_iCurSampleIdx);
            ++d;
        }
    }

    // Check if the new sample period has started.
    const int iNewDeltaIdx = (int) count_milliseconds(ts - m_tsFirstSampleTime) / SAMPLE_DURATION_MS;
    if (incSampleIdx(m_iFirstSampleIdx, iNewDeltaIdx) != m_iCurSampleIdx)
    {
        // Now there should be some periods (at most last NUM_PERIODS) ready to be summed,
        // rate estimation updated, after which all the new entry should be added.
        Sample sum;
        int iNumPeriods = 0;
        bool bMetNonEmpty = false;
        for (int i = 0; i < NUM_PERIODS; ++i)
        {
            const int idx = incSampleIdx(m_iFirstSampleIdx, i);
            const Sample& s = m_Samples[idx];
            sum += s;
            if (bMetNonEmpty || !s.empty())
            {
                ++iNumPeriods;
                bMetNonEmpty = true;
            }

            if (idx == m_iCurSampleIdx)
                break;
        }

        if (iNumPeriods == 0)
        {
            m_iRateBps = 0;
        }
        else
        {
            m_iRateBps = sum.m_iBytesCount * 1000 / (iNumPeriods * SAMPLE_DURATION_MS);
        }

        HLOGC(bslog.Note,
            log << "CSndRateEstimator: new rate estimation :" << (m_iRateBps * 8) / 1000 << " kbps. Based on "
                 << iNumPeriods << " periods, " << sum.m_iPktsCount << " packets, " << sum.m_iBytesCount << " bytes.");

        // Shift one sampling period to start collecting the new one.
        m_iCurSampleIdx = incSampleIdx(m_iCurSampleIdx);
        m_Samples[m_iCurSampleIdx].reset();
        
        // If all NUM_SAMPLES are recorded, the first position has to be shifted as well.
        if (delta <= 0)
        {
            m_iFirstSampleIdx = incSampleIdx(m_iFirstSampleIdx);
            m_tsFirstSampleTime += milliseconds_from(SAMPLE_DURATION_MS);
        }
    }

    m_Samples[m_iCurSampleIdx].m_iBytesCount += bytes;
    m_Samples[m_iCurSampleIdx].m_iPktsCount += pkts;
}

int CSndRateEstimator::getCurrentRate() const
{
    SRT_ASSERT(m_iCurSampleIdx >= 0 && m_iCurSampleIdx < NUM_PERIODS);
    return (int) avg_iir<16, unsigned long long>(m_iRateBps, m_Samples[m_iCurSampleIdx].m_iBytesCount * 1000 / SAMPLE_DURATION_MS);
}

int CSndRateEstimator::incSampleIdx(int val, int inc) const
{
    SRT_ASSERT(inc >= 0 && inc <= NUM_PERIODS);
    val += inc;
    while (val >= NUM_PERIODS)
        val -= NUM_PERIODS;
    return val;
}

}

