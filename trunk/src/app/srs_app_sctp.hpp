/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 John
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

#ifndef SRS_APP_SCTP_HPP
#define SRS_APP_SCTP_HPP

#include <srs_core.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_hourglass.hpp>

#include <string>
#include <map>
#include <vector>

#if defined(SRS_VERBOSE) || defined(SRS_DEBUG)
#define SCTP_DEBUG 1
#endif

#include <usrsctp.h>
#include <srs_app_rtc_source.hpp>

class SrsHourGlass;
class SrsDtls;

enum DataChannelStatus
{
    DataChannelStatusClosed = 1,
    DataChannelStatusOpen = 2,
};

struct SrsDataChannel
{
    SrsDataChannel();

    std::string label_;
    uint16_t sid_;

    uint8_t channel_type_;
    uint32_t reliability_params_;
    DataChannelStatus status_;
};

class SrsSctpGlobalEnv : virtual public ISrsHourGlass
{
public:
    SrsSctpGlobalEnv();
    ~SrsSctpGlobalEnv();
public:
    virtual srs_error_t notify(int type, srs_utime_t interval, srs_utime_t tick);
private:
    SrsHourGlass* sctp_timer_;
};

class SrsSctp : public ISrsCoroutineHandler, public ISendDatachannel, public IComsumeDatachannel
{
public:
    SrsDtls* rtc_dtls_;
    struct socket* sctp_socket;
public:
    SrsSctp(SrsDtls* dtls, bool datachannel_from_rtmp = false, const std::string& stream_info = "", int cnt = 0);
    virtual ~SrsSctp();
public:
    virtual srs_error_t cycle();
    srs_error_t connect_to_class();
	srs_error_t send(const uint16_t sid, const char* buf, const int len);
    void broadcast(const char* buf, const int len);
    srs_error_t on_sctp_event(const struct sctp_rcvinfo& rcv, void* data, size_t len);
    srs_error_t on_sctp_data(const struct sctp_rcvinfo& rcv, void* data, size_t len);
    void feed(const char* buf, const int nb_buf);
private:
    srs_error_t on_data_channel_control(const struct sctp_rcvinfo& rcv, SrsBuffer* stream);
    srs_error_t on_data_channel_msg(const struct sctp_rcvinfo& rcv, SrsBuffer* stream);
    srs_error_t send_data_channel(const uint16_t sid, const char* buf, const int len);
    srs_error_t enqueue_datachannel(SrsSharedPtrMessage* msg);
    srs_error_t set_datachannel_metadata(SrsSharedPtrMessage* msg);
    srs_error_t set_datachannel_sequence(SrsSharedPtrMessage* msg);
private:
    SrsSTCoroutine* sctp_td_;
    bool datachannel_from_rtmp_;
    std::string stream_info_;
    int retry_cnt_;
    int loop_cnt_;
    sctp_rcvinfo sctp_info_;
    std::map<uint16_t, SrsDataChannel> data_channels_;
};

#endif
