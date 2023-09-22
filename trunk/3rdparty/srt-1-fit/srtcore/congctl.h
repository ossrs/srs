/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC_SRT_CONGCTL_H
#define INC_SRT_CONGCTL_H

#include <algorithm>
#include <map>
#include <string>
#include <utility>

namespace srt {

class CUDT;
class SrtCongestionControlBase;
typedef SrtCongestionControlBase* srtcc_create_t(srt::CUDT* parent);

class SrtCongestion
{
    // Temporarily changed to linear searching, until this is exposed
    // for a user-defined controller.
    // Note that this is a pointer to function :)

    static const size_t N_CONTROLLERS = 2;
    // The first/second is to mimic the map.
    typedef struct { const char* first; srtcc_create_t* second; } NamePtr;
    static NamePtr congctls[N_CONTROLLERS];

    // This is a congctl container.
    SrtCongestionControlBase* congctl;
    size_t selector;

    void Check();

public:

    // If you predict to allow something to be done on controller also
    // before it is configured, call this first. If you need it configured,
    // you can rely on Check().
    bool ready() { return congctl; }
    SrtCongestionControlBase* operator->() { Check(); return congctl; }

    // In the beginning it's uninitialized
    SrtCongestion(): congctl(), selector(N_CONTROLLERS) {}

    struct IsName
    {
        const std::string n;
        IsName(const std::string& nn): n(nn) {}
        bool operator()(NamePtr np) { return n == np.first; }
    };

    static NamePtr* find(const std::string& name)
    {
        NamePtr* end = congctls+N_CONTROLLERS;
        NamePtr* try_selector = std::find_if(congctls, end, IsName(name));
        return try_selector != end ? try_selector : NULL;
    }

    static bool exists(const std::string& name)
    {
        return find(name);
    }

    // You can call select() multiple times, until finally
    // the 'configure' method is called.
    bool select(const std::string& name)
    {
        NamePtr* try_selector = find(name);
        if (!try_selector)
            return false;
        selector = try_selector - congctls;
        return true;
    }

    std::string selected_name()
    {
        if (selector == N_CONTROLLERS)
            return "";
        return congctls[selector].first;
    }

    // Copy constructor - important when listener-spawning
    // Things being done:
    // 1. The congctl is individual, so don't copy it. Set NULL.
    // 2. The selected name is copied so that it's configured correctly.
    SrtCongestion(const SrtCongestion& source): congctl(), selector(source.selector) {}
    void operator=(const SrtCongestion& source) { congctl = 0; selector = source.selector; }

    // This function will be called by the parent CUDT
    // in appropriate time. It should select appropriate
    // congctl basing on the value in selector, then
    // pin oneself in into CUDT for receiving event signals.
    bool configure(srt::CUDT* parent);

    // This function will intentionally delete the contained object.
    // This makes future calls to ready() return false. Calling
    // configure on it again will create it again.
    void dispose();

    // Will delete the pinned in congctl object.
    // This must be defined in *.cpp file due to virtual
    // destruction.
    ~SrtCongestion();

    enum RexmitMethod
    {
        SRM_LATEREXMIT,
        SRM_FASTREXMIT
    };

    enum TransAPI
    {
        STA_MESSAGE = 0x1, // sendmsg/recvmsg functions
        STA_BUFFER  = 0x2,  // send/recv functions
        STA_FILE    = 0x3, // sendfile/recvfile functions
    };

    enum TransDir
    {
        STAD_RECV = 0,
        STAD_SEND = 1
    };
};

class CPacket;

class SrtCongestionControlBase
{
protected:
    // Here can be some common fields
    srt::CUDT* m_parent;

    double m_dPktSndPeriod;
    double m_dCWndSize;

    //int m_iBandwidth; // NOT REQUIRED. Use m_parent->bandwidth() instead.
    double m_dMaxCWndSize;

    //int m_iMSS;              // NOT REQUIRED. Use m_parent->MSS() instead.
    //int32_t m_iSndCurrSeqNo; // NOT REQUIRED. Use m_parent->sndSeqNo().
    //int m_iRcvRate;          // NOT REQUIRED. Use m_parent->deliveryRate() instead.
    //int m_RTT;               // NOT REQUIRED. Use m_parent->SRTT() instead.
    //char* m_pcParam;         // Used to access m_llMaxBw. Use m_parent->maxBandwidth() instead.

    // Constructor in protected section so that this class is semi-abstract.
    SrtCongestionControlBase(srt::CUDT* parent);
public:

    // This could be also made abstract, but this causes a linkage
    // problem in C++: this would constitute the first virtual method,
    // and C++ compiler uses the location of the first virtual method as the
    // file to which it also emits the virtual call table. When this is
    // abstract, there would have to be simultaneously either defined
    // an empty method in congctl.cpp file (obviously never called),
    // or simply left empty body here.
    virtual ~SrtCongestionControlBase() { }

    // All these functions that return values interesting for processing
    // by CUDT can be overridden. Normally they should refer to the fields
    // and these fields should keep the values as a state.
    virtual double pktSndPeriod_us() { return m_dPktSndPeriod; }
    virtual double cgWindowSize() { return m_dCWndSize; }
    virtual double cgWindowMaxSize() { return m_dMaxCWndSize; }

    virtual int64_t sndBandwidth() { return 0; }

    // If user-defined, will return nonzero value.
    // If not, it will be internally calculated.
    virtual int RTO() { return 0; }

    // Maximum number of packets to trigger ACK sending.
    // Specifies the number of packets to receive before sending the ACK.
    // Used by CUDT together with ACKTimeout_us() to trigger ACK packet sending.
    virtual int ACKMaxPackets() const { return 0; }

    // Periodical interval to send an ACK, in microseconds.
    // If user-defined, this value will be used to calculate
    // the next ACK time every time ACK is considered to be sent (see CUDT::checkTimers).
    // Otherwise this will be calculated internally in CUDT, normally taken
    // from CUDT::COMM_SYN_INTERVAL_US.
    virtual int ACKTimeout_us() const { return 0; }

    // Called when the settings concerning m_llMaxBW were changed.
    // Arg 1: value of CUDT's m_config.m_llMaxBW
    // Arg 2: value calculated out of CUDT's m_config.llInputBW and m_config.iOverheadBW.
    virtual void updateBandwidth(int64_t, int64_t) {}

    virtual bool needsQuickACK(const srt::CPacket&)
    {
        return false;
    }

    // Particular controller is allowed to agree or disagree on the use of particular API.
    virtual bool checkTransArgs(SrtCongestion::TransAPI , SrtCongestion::TransDir , const char* /*buffer*/, size_t /*size*/, int /*ttl*/, bool /*inorder*/)
    {
        return true;
    }

    virtual SrtCongestion::RexmitMethod rexmitMethod() = 0; // Implementation enforced.

    virtual int64_t updateNAKInterval(int64_t nakint_us, int rcv_speed, size_t loss_length)
    {
        if (rcv_speed > 0)
            nakint_us += (loss_length * int64_t(1000000) / rcv_speed);

        return nakint_us;
    }

    virtual int64_t minNAKInterval()
    {
        return 0; // Leave default
    }
};


} // namespace srt

#endif
