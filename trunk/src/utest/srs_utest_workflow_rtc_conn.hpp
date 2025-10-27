/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2025 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_UTEST_WORKFLOW_RTC_CONN_HPP
#define SRS_UTEST_WORKFLOW_RTC_CONN_HPP

/*
#include <srs_utest_workflow_rtc_conn.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_factory.hpp>
#include <srs_protocol_utility.hpp>

#include <string>
#include <vector>

class MockAppFactoryForRtcConn;

class MockProtocolUtilityForRtcConn : public ISrsProtocolUtility
{
public:
    std::vector<SrsIPAddress *> ips_;
    std::string mock_ip_;

public:
    MockProtocolUtilityForRtcConn(std::string ip);
    virtual ~MockProtocolUtilityForRtcConn();

public:
    virtual std::vector<SrsIPAddress *> &local_ips();
};

class MockAppFactoryForRtcConn : public SrsAppFactory
{
public:
    ISrsRtcSourceManager *rtc_sources_;
    MockProtocolUtilityForRtcConn *mock_protocol_utility_;

public:
    MockAppFactoryForRtcConn();
    virtual ~MockAppFactoryForRtcConn();

public:
    virtual ISrsProtocolUtility *create_protocol_utility();
    virtual ISrsRtcPublishStream *create_rtc_publish_stream(ISrsExecRtcAsyncTask *exec, ISrsExpire *expire, ISrsRtcPacketReceiver *receiver, const SrsContextId &cid);
};

class MockRtcSourceForRtcConn : public SrsRtcSource
{
public:
    int rtp_audio_count_;
    int rtp_video_count_;

public:
    MockRtcSourceForRtcConn();
    virtual ~MockRtcSourceForRtcConn();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
};

#endif
