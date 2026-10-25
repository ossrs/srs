/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2026 Winlin
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
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_utest_manual_mock.hpp>

class MockAppFactoryForRtcConn : public SrsAppFactory
{
public:
    ISrsRtcSourceManager *rtc_sources_;
    MockProtocolUtility *mock_protocol_utility_;

public:
    MockAppFactoryForRtcConn()
    {
        mock_protocol_utility_ = NULL;
    }
    virtual ~MockAppFactoryForRtcConn()
    {
    }

public:
    virtual ISrsProtocolUtility *create_protocol_utility()
    {
        return mock_protocol_utility_;
    }
    virtual ISrsRtcPublishStream *create_rtc_publish_stream(ISrsExecRtcAsyncTask *exec, ISrsExpire *expire, ISrsRtcPacketReceiver *receiver, const SrsContextId &cid)
    {
        SrsRtcPublishStream *publisher = new SrsRtcPublishStream(exec, expire, receiver, cid);
        publisher->rtc_sources_ = rtc_sources_;
        return publisher;
    }
};

// A track description that counts its own destruction, to tell whether the play relations negotiated by add_player
// are freed.
class MockRtcTrackDescriptionForRelease : public SrsRtcTrackDescription
{
public:
    int *freed_;

public:
    MockRtcTrackDescriptionForRelease(int *freed, std::string type, uint32_t ssrc)
    {
        freed_ = freed;
        type_ = type;
        id_ = type;
        ssrc_ = ssrc;
    }
    virtual ~MockRtcTrackDescriptionForRelease()
    {
        (*freed_)++;
    }
};

// A player negotiator that fills the play relations with counted tracks, then fails or succeeds as the test chooses.
class MockRtcPlayerNegotiatorForRelease : public ISrsRtcPlayerNegotiator
{
public:
    int *freed_;
    int negotiated_;
    srs_error_t negotiate_error_;
    srs_error_t generate_error_;

public:
    MockRtcPlayerNegotiatorForRelease(int *freed)
    {
        freed_ = freed;
        negotiated_ = 0;
        negotiate_error_ = srs_success;
        generate_error_ = srs_success;
    }
    virtual ~MockRtcPlayerNegotiatorForRelease()
    {
        srs_freep(negotiate_error_);
        srs_freep(generate_error_);
    }

public:
    // Negotiate an audio track, then fail if asked, before the video track like a video m-line without a matching
    // codec does.
    virtual srs_error_t negotiate_play_capability(SrsRtcUserConfig *ruc, std::map<uint32_t, SrsRtcTrackDescription *> &sub_relations)
    {
        sub_relations[100] = new MockRtcTrackDescriptionForRelease(freed_, "audio", 100);
        negotiated_++;
        if (negotiate_error_ != srs_success) {
            return srs_error_copy(negotiate_error_);
        }

        sub_relations[200] = new MockRtcTrackDescriptionForRelease(freed_, "video", 200);
        negotiated_++;
        return srs_success;
    }
    virtual srs_error_t generate_play_local_sdp(ISrsRequest *req, SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc, bool unified_plan, bool audio_before_video)
    {
        return srs_error_copy(generate_error_);
    }
};

#endif
