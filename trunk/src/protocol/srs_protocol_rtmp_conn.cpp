//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_rtmp_conn.hpp>

#include <unistd.h>
using namespace std;

#include <srs_kernel_kbps.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_utility.hpp>

ISrsBasicRtmpClient::ISrsBasicRtmpClient()
{
}

ISrsBasicRtmpClient::~ISrsBasicRtmpClient()
{
}

SrsBasicRtmpClient::SrsBasicRtmpClient(string r, srs_utime_t ctm, srs_utime_t stm)
{
    kbps_ = new SrsNetworkKbps();

    url_ = r;
    connect_timeout_ = ctm;
    stream_timeout_ = stm;

    req_ = new SrsRequest();
    srs_net_url_parse_rtmp_url(url_, req_->tcUrl_, req_->stream_);
    srs_net_url_parse_tcurl(req_->tcUrl_, req_->schema_, req_->host_, req_->vhost_, req_->app_, req_->stream_, req_->port_, req_->param_);

    transport_ = NULL;
    client_ = NULL;

    stream_id_ = 0;
}

SrsBasicRtmpClient::~SrsBasicRtmpClient()
{
    close();
    srs_freep(kbps_);
    srs_freep(req_);
}

SrsAmf0Object *SrsBasicRtmpClient::extra_args()
{
    if (req_->args_ == NULL) {
        req_->args_ = SrsAmf0Any::object();
    }
    return req_->args_;
}

srs_error_t SrsBasicRtmpClient::connect()
{
    srs_error_t err = srs_success;

    close();

    transport_ = new SrsTcpClient(req_->host_, req_->port_, srs_utime_t(connect_timeout_));
    client_ = new SrsRtmpClient(transport_);
    kbps_->set_io(transport_, transport_);

    if ((err = transport_->connect()) != srs_success) {
        close();
        return srs_error_wrap(err, "connect");
    }

    client_->set_recv_timeout(stream_timeout_);
    client_->set_send_timeout(stream_timeout_);

    // connect to vhost/app
    if ((err = client_->handshake()) != srs_success) {
        return srs_error_wrap(err, "handshake");
    }
    if ((err = connect_app()) != srs_success) {
        return srs_error_wrap(err, "connect app");
    }
    if ((err = client_->create_stream(stream_id_)) != srs_success) {
        return srs_error_wrap(err, "create stream_id=%d", stream_id_);
    }

    return err;
}

void SrsBasicRtmpClient::close()
{
    kbps_->set_io(NULL, NULL);
    srs_freep(client_);
    srs_freep(transport_);
}

srs_error_t SrsBasicRtmpClient::connect_app()
{
    SrsProtocolUtility utility;
    return do_connect_app(utility.public_internet_address(), false);
}

srs_error_t SrsBasicRtmpClient::do_connect_app(string local_ip, bool debug)
{
    srs_error_t err = srs_success;

    // notify server the edge identity,
    SrsAmf0Object *data = extra_args();
    data->set("srs_sig", SrsAmf0Any::str(RTMP_SIG_SRS_KEY));
    data->set("srs_server", SrsAmf0Any::str(RTMP_SIG_SRS_SERVER));
    data->set("srs_license", SrsAmf0Any::str(RTMP_SIG_SRS_LICENSE));
    data->set("srs_url", SrsAmf0Any::str(RTMP_SIG_SRS_URL));
    data->set("srs_version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));
    // for edge to directly get the id of client.
    data->set("srs_pid", SrsAmf0Any::number(getpid()));
    data->set("srs_id", SrsAmf0Any::str(_srs_context->get_id().c_str()));

    // local ip of edge
    data->set("srs_server_ip", SrsAmf0Any::str(local_ip.c_str()));

    // generate the tcUrl
    std::string param = "";
    std::string target_vhost = req_->vhost_;
    std::string tc_url = srs_net_url_encode_tcurl("rtmp", req_->host_, req_->vhost_, req_->app_, req_->port_);

    // replace the tcUrl in request,
    // which will replace the tc_url in client.connect_app().
    req_->tcUrl_ = tc_url;

    // upnode server identity will show in the connect_app of client.
    // the debug_srs_upnode is config in vhost and default to true.
    SrsServerInfo si;
    if ((err = client_->connect_app(req_->app_, tc_url, req_, debug, &si)) != srs_success) {
        return srs_error_wrap(err, "connect app tcUrl=%s, debug=%d", tc_url.c_str(), debug);
    }

    return err;
}

srs_error_t SrsBasicRtmpClient::publish(int chunk_size, bool with_vhost, std::string *pstream)
{
    srs_error_t err = srs_success;

    // Pass params in stream, @see https://github.com/ossrs/srs/issues/1031#issuecomment-409745733
    string stream = srs_net_url_encode_stream(req_->host_, req_->vhost_, req_->stream_, req_->param_, with_vhost);

    // Return the generated stream.
    if (pstream) {
        *pstream = stream;
    }

    // publish.
    if ((err = client_->publish(stream, stream_id_, chunk_size)) != srs_success) {
        return srs_error_wrap(err, "publish failed, stream=%s, stream_id=%d", stream.c_str(), stream_id_);
    }

    return err;
}

srs_error_t SrsBasicRtmpClient::play(int chunk_size, bool with_vhost, std::string *pstream)
{
    srs_error_t err = srs_success;

    // Pass params in stream, @see https://github.com/ossrs/srs/issues/1031#issuecomment-409745733
    string stream = srs_net_url_encode_stream(req_->host_, req_->vhost_, req_->stream_, req_->param_, with_vhost);

    // Return the generated stream.
    if (pstream) {
        *pstream = stream;
    }

    if ((err = client_->play(stream, stream_id_, chunk_size)) != srs_success) {
        return srs_error_wrap(err, "connect with server failed, stream=%s, stream_id=%d", stream.c_str(), stream_id_);
    }

    return err;
}

void SrsBasicRtmpClient::kbps_sample(const char *label, srs_utime_t age)
{
    kbps_->sample();

    int sr = kbps_->get_send_kbps();
    int sr30s = kbps_->get_send_kbps_30s();
    int sr5m = kbps_->get_send_kbps_5m();
    int rr = kbps_->get_recv_kbps();
    int rr30s = kbps_->get_recv_kbps_30s();
    int rr5m = kbps_->get_recv_kbps_5m();

    srs_trace("<- %s time=%" PRId64 ", okbps=%d,%d,%d, ikbps=%d,%d,%d", label, srsu2ms(age), sr, sr30s, sr5m, rr, rr30s, rr5m);
}

void SrsBasicRtmpClient::kbps_sample(const char *label, srs_utime_t age, int msgs)
{
    kbps_->sample();

    int sr = kbps_->get_send_kbps();
    int sr30s = kbps_->get_send_kbps_30s();
    int sr5m = kbps_->get_send_kbps_5m();
    int rr = kbps_->get_recv_kbps();
    int rr30s = kbps_->get_recv_kbps_30s();
    int rr5m = kbps_->get_recv_kbps_5m();

    srs_trace("<- %s time=%" PRId64 ", msgs=%d, okbps=%d,%d,%d, ikbps=%d,%d,%d", label, srsu2ms(age), msgs, sr, sr30s, sr5m, rr, rr30s, rr5m);
}

int SrsBasicRtmpClient::sid()
{
    return stream_id_;
}

srs_error_t SrsBasicRtmpClient::recv_message(SrsRtmpCommonMessage **pmsg)
{
    return client_->recv_message(pmsg);
}

srs_error_t SrsBasicRtmpClient::decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket)
{
    return client_->decode_message(msg, ppacket);
}

srs_error_t SrsBasicRtmpClient::send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs)
{
    return client_->send_and_free_messages(msgs, nb_msgs, stream_id_);
}

srs_error_t SrsBasicRtmpClient::send_and_free_message(SrsMediaPacket *msg)
{
    return client_->send_and_free_message(msg, stream_id_);
}

void SrsBasicRtmpClient::set_recv_timeout(srs_utime_t timeout)
{
    transport_->set_recv_timeout(timeout);
}
