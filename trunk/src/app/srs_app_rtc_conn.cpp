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

#include <srs_app_rtc_conn.hpp>

using namespace std;

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <netinet/udp.h>
// Define macro for UDP GSO.
// @see https://github.com/torvalds/linux/blob/master/tools/testing/selftests/net/udpgso.c
#ifndef UDP_SEGMENT
#define UDP_SEGMENT             103
#endif

#include <sstream>

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_rtp.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_stun_stack.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_app_dtls.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_config.hpp>
#include <srs_app_rtc.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_service_utility.hpp>
#include <srs_http_stack.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_pithy_print.hpp>

// The RTP payload max size, reserved some paddings for SRTP as such:
//      kRtpPacketSize = kRtpMaxPayloadSize + paddings
// For example, if kRtpPacketSize is 1500, recommend to set kRtpMaxPayloadSize to 1400,
// which reserves 100 bytes for SRTP or paddings.
const int kRtpMaxPayloadSize = kRtpPacketSize - 200;

static bool is_stun(const uint8_t* data, const int size) 
{
    return data != NULL && size > 0 && (data[0] == 0 || data[0] == 1); 
}

static bool is_dtls(const uint8_t* data, size_t len) 
{
      return (len >= 13 && (data[0] > 19 && data[0] < 64));
}

static bool is_rtp_or_rtcp(const uint8_t* data, size_t len) 
{
      return (len >= 12 && (data[0] & 0xC0) == 0x80);
}

static bool is_rtcp(const uint8_t* data, size_t len)
{
    return (len >= 12) && (data[0] & 0x80) && (data[1] >= 200 && data[1] <= 209);
}

static string gen_random_str(int len)
{
    static string random_table = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

    string ret;
    ret.reserve(len);
    for (int i = 0; i < len; ++i) {
        ret.append(1, random_table[random() % random_table.size()]);
    }

    return ret;
}

const int SRTP_MASTER_KEY_KEY_LEN = 16;
const int SRTP_MASTER_KEY_SALT_LEN = 14;

static std::vector<std::string> get_candidate_ips()
{
    std::vector<std::string> candidate_ips;

    string candidate = _srs_config->get_rtc_server_candidates();
    if (candidate == "*" || candidate == "0.0.0.0") {
        std::vector<std::string> tmp = srs_get_local_ips();
        for (int i = 0; i < (int)tmp.size(); ++i) {
            if (tmp[i] != "127.0.0.1") {
                candidate_ips.push_back(tmp[i]);
            }
        }
    } else {
        candidate_ips.push_back(candidate);
    }

    return candidate_ips;
}

SrsDtlsSession::SrsDtlsSession(SrsRtcSession* s)
{
    rtc_session = s;

    dtls = NULL;
    bio_in = NULL;
    bio_out = NULL;

    client_key = "";
    server_key = "";

    srtp_send = NULL;
    srtp_recv = NULL;

    handshake_done = false;
}

SrsDtlsSession::~SrsDtlsSession()
{
    if (dtls) {
        // this function will free bio_in and bio_out
        SSL_free(dtls);
        dtls = NULL;
    }

    if (srtp_send) {
        srtp_dealloc(srtp_send);
    }

    if (srtp_recv) {
        srtp_dealloc(srtp_recv);
    }
}

srs_error_t SrsDtlsSession::initialize(const SrsRequest& req)
{    
    srs_error_t err = srs_success;

    if ((err = SrsDtls::instance()->init(req)) != srs_success) {
        return srs_error_wrap(err, "DTLS init");
    }

    // TODO: FIXME: Support config by vhost to use RSA or ECDSA certificate.
    if ((dtls = SSL_new(SrsDtls::instance()->get_dtls_ctx())) == NULL) {
        return srs_error_new(ERROR_OpenSslCreateSSL, "SSL_new dtls");
    }

    // Dtls setup passive, as server role.
    SSL_set_accept_state(dtls);

    if ((bio_in = BIO_new(BIO_s_mem())) == NULL) {
        return srs_error_new(ERROR_OpenSslBIONew, "BIO_new in");
    }

    if ((bio_out = BIO_new(BIO_s_mem())) == NULL) {
        BIO_free(bio_in);
        return srs_error_new(ERROR_OpenSslBIONew, "BIO_new out");
    }

    SSL_set_bio(dtls, bio_in, bio_out);

    return err;
}

srs_error_t SrsDtlsSession::handshake(SrsUdpMuxSocket* skt)
{
    srs_error_t err = srs_success;

    int ret = SSL_do_handshake(dtls);

    unsigned char *out_bio_data;
    int out_bio_len = BIO_get_mem_data(bio_out, &out_bio_data);

    int ssl_err = SSL_get_error(dtls, ret); 
    switch(ssl_err) {   
        case SSL_ERROR_NONE: {   
            if ((err = on_dtls_handshake_done(skt)) != srs_success) {
                return srs_error_wrap(err, "dtls handshake done handle");
            }
            break;
        }  

        case SSL_ERROR_WANT_READ: {   
            break;
        }   

        case SSL_ERROR_WANT_WRITE: {   
            break;
        }

        default: {   
            break;
        }   
    }   

    if (out_bio_len) {   
        if ((err = skt->sendto(out_bio_data, out_bio_len, 0)) != srs_success) {
            return srs_error_wrap(err, "send dtls packet");
        }
    }

    return err;
}

srs_error_t SrsDtlsSession::on_dtls(SrsUdpMuxSocket* skt)
{
    srs_error_t err = srs_success;
    if (BIO_reset(bio_in) != 1) {
        return srs_error_new(ERROR_OpenSslBIOReset, "BIO_reset");
    }
    if (BIO_reset(bio_out) != 1) {
        return srs_error_new(ERROR_OpenSslBIOReset, "BIO_reset");
    }

    if (BIO_write(bio_in, skt->data(), skt->size()) <= 0) {
        // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
        return srs_error_new(ERROR_OpenSslBIOWrite, "BIO_write");
    }

    if (! handshake_done) {
        err = handshake(skt);
    } else {
        while (BIO_ctrl_pending(bio_in) > 0) {
            char dtls_read_buf[8092];
            int nb = SSL_read(dtls, dtls_read_buf, sizeof(dtls_read_buf));

            if (nb > 0) {
                if ((err =on_dtls_application_data(dtls_read_buf, nb)) != srs_success) {
                    return srs_error_wrap(err, "dtls application data process");
                }
            }
        }
    }

    return err;
}

srs_error_t SrsDtlsSession::on_dtls_handshake_done(SrsUdpMuxSocket* skt)
{
    srs_error_t err = srs_success;
    srs_trace("dtls handshake done");

    handshake_done = true;
    if ((err = srtp_initialize()) != srs_success) {
        return srs_error_wrap(err, "srtp init failed");
    }

    return rtc_session->on_connection_established(skt);
}

srs_error_t SrsDtlsSession::on_dtls_application_data(const char* buf, const int nb_buf)
{
    srs_error_t err = srs_success;

    // TODO: process SCTP protocol(WebRTC datachannel support)

    return err;
}

srs_error_t SrsDtlsSession::srtp_initialize() 
{
    srs_error_t err = srs_success;

    unsigned char material[SRTP_MASTER_KEY_LEN * 2] = {0};  // client(SRTP_MASTER_KEY_KEY_LEN + SRTP_MASTER_KEY_SALT_LEN) + server
    static const string dtls_srtp_lable = "EXTRACTOR-dtls_srtp";
    if (! SSL_export_keying_material(dtls, material, sizeof(material), dtls_srtp_lable.c_str(), dtls_srtp_lable.size(), NULL, 0, 0)) {   
        return srs_error_new(ERROR_RTC_SRTP_INIT, "SSL_export_keying_material failed");
    }   

    size_t offset = 0;

    std::string client_master_key(reinterpret_cast<char*>(material), SRTP_MASTER_KEY_KEY_LEN);
    offset += SRTP_MASTER_KEY_KEY_LEN;
    std::string server_master_key(reinterpret_cast<char*>(material + offset), SRTP_MASTER_KEY_KEY_LEN);
    offset += SRTP_MASTER_KEY_KEY_LEN;
    std::string client_master_salt(reinterpret_cast<char*>(material + offset), SRTP_MASTER_KEY_SALT_LEN);
    offset += SRTP_MASTER_KEY_SALT_LEN;
    std::string server_master_salt(reinterpret_cast<char*>(material + offset), SRTP_MASTER_KEY_SALT_LEN);

    client_key = client_master_key + client_master_salt;
    server_key = server_master_key + server_master_salt;

    if ((err = srtp_send_init()) != srs_success) {
        return srs_error_wrap(err, "srtp send init failed");
    }

    if ((err = srtp_recv_init()) != srs_success) {
        return srs_error_wrap(err, "srtp recv init failed");
    }

    return err;
}

srs_error_t SrsDtlsSession::srtp_send_init()
{
    srs_error_t err = srs_success;

    srtp_policy_t policy;
    bzero(&policy, sizeof(policy));

    // TODO: Maybe we can use SRTP-GCM in future.
    // @see https://bugs.chromium.org/p/chromium/issues/detail?id=713701
    // @see https://groups.google.com/forum/#!topic/discuss-webrtc/PvCbWSetVAQ
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);

    policy.ssrc.type = ssrc_any_outbound;
    
    policy.ssrc.value = 0;
    // TODO: adjust window_size
    policy.window_size = 8192;
    policy.allow_repeat_tx = 1;
    policy.next = NULL;

    uint8_t *key = new uint8_t[server_key.size()];
    memcpy(key, server_key.data(), server_key.size());
    policy.key = key;

    if (srtp_create(&srtp_send, &policy) != srtp_err_status_ok) {
        srs_freepa(key);
        return srs_error_new(ERROR_RTC_SRTP_INIT, "srtp_create failed");
    }

    srs_freepa(key);

    return err;
}

srs_error_t SrsDtlsSession::srtp_recv_init()
{
    srs_error_t err = srs_success;

    srtp_policy_t policy;
    bzero(&policy, sizeof(policy));

    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);

    policy.ssrc.type = ssrc_any_inbound;

    policy.ssrc.value = 0;
    // TODO: adjust window_size
    policy.window_size = 8192;
    policy.allow_repeat_tx = 1;
    policy.next = NULL;

    uint8_t *key = new uint8_t[client_key.size()];
    memcpy(key, client_key.data(), client_key.size());
    policy.key = key;

    if (srtp_create(&srtp_recv, &policy) != srtp_err_status_ok) {
        srs_freepa(key);
        return srs_error_new(ERROR_RTC_SRTP_INIT, "srtp_create failed");
    }

    srs_freepa(key);

    return err;
}

srs_error_t SrsDtlsSession::protect_rtp(char* out_buf, const char* in_buf, int& nb_out_buf)
{
    srs_error_t err = srs_success;

    if (srtp_send) {
        memcpy(out_buf, in_buf, nb_out_buf);
        if (srtp_protect(srtp_send, out_buf, &nb_out_buf) != 0) {
            return srs_error_new(ERROR_RTC_SRTP_PROTECT, "rtp protect failed");
        }

        return err;
    }

    return srs_error_new(ERROR_RTC_SRTP_PROTECT, "rtp protect failed");
}

srs_error_t SrsDtlsSession::protect_rtp2(void* rtp_hdr, int* len_ptr)
{
    srs_error_t err = srs_success;

    if (!srtp_send) {
        return srs_error_new(ERROR_RTC_SRTP_PROTECT, "rtp protect");
    }

    if (srtp_protect(srtp_send, rtp_hdr, len_ptr) != 0) {
        return srs_error_new(ERROR_RTC_SRTP_PROTECT, "rtp protect");
    }

    return err;
}

srs_error_t SrsDtlsSession::unprotect_rtp(char* out_buf, const char* in_buf, int& nb_out_buf)
{
    srs_error_t err = srs_success;

    if (srtp_recv) {
        memcpy(out_buf, in_buf, nb_out_buf);
        if (srtp_unprotect(srtp_recv, out_buf, &nb_out_buf) != 0) {
            return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "rtp unprotect failed");
        }

        return err;
    }

    return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "rtp unprotect failed");
}

srs_error_t SrsDtlsSession::protect_rtcp(char* out_buf, const char* in_buf, int& nb_out_buf)
{
    srs_error_t err = srs_success;

    if (srtp_send) {
        memcpy(out_buf, in_buf, nb_out_buf);
        if (srtp_protect_rtcp(srtp_send, out_buf, &nb_out_buf) != 0) {
            return srs_error_new(ERROR_RTC_SRTP_PROTECT, "rtcp protect failed");
        }

        return err;
    }

    return srs_error_new(ERROR_RTC_SRTP_PROTECT, "rtcp protect failed");
}

srs_error_t SrsDtlsSession::unprotect_rtcp(char* out_buf, const char* in_buf, int& nb_out_buf)
{
    srs_error_t err = srs_success;

    if (srtp_recv) {
        memcpy(out_buf, in_buf, nb_out_buf);
        if (srtp_unprotect_rtcp(srtp_recv, out_buf, &nb_out_buf) != srtp_err_status_ok) {
            return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "rtcp unprotect failed");
        }

        return err;
    }

    return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "rtcp unprotect failed");
}

SrsRtcPackets::SrsRtcPackets(int nn_cache_max)
{
#if defined(SRS_DEBUG)
    debug_id = 0;
#endif

    use_gso = false;
    should_merge_nalus = false;

    nn_rtp_pkts = 0;
    nn_audios = nn_extras = 0;
    nn_videos = nn_samples = 0;
    nn_bytes = nn_rtp_bytes = 0;
    nn_padding_bytes = nn_paddings = 0;
    nn_dropped = 0;

    cursor = 0;
    nn_cache = nn_cache_max;
    // TODO: FIXME: We should allocate a smaller cache, and increase it when exhausted.
    cache = new SrsRtpPacket2[nn_cache];
}

SrsRtcPackets::~SrsRtcPackets()
{
    srs_freepa(cache);
    nn_cache = 0;
}

void SrsRtcPackets::reset(bool gso, bool merge_nalus)
{
    for (int i = 0; i < cursor; i++) {
        SrsRtpPacket2* packet = cache + i;
        packet->reset();
    }

#if defined(SRS_DEBUG)
    debug_id++;
#endif

    use_gso = gso;
    should_merge_nalus = merge_nalus;

    nn_rtp_pkts = 0;
    nn_audios = nn_extras = 0;
    nn_videos = nn_samples = 0;
    nn_bytes = nn_rtp_bytes = 0;
    nn_padding_bytes = nn_paddings = 0;
    nn_dropped = 0;

    cursor = 0;
}

SrsRtpPacket2* SrsRtcPackets::fetch()
{
    if (cursor >= nn_cache) {
        return NULL;
    }
    return cache + (cursor++);
}

SrsRtpPacket2* SrsRtcPackets::back()
{
    srs_assert(cursor > 0);
    return cache + cursor - 1;
}

int SrsRtcPackets::size()
{
    return cursor;
}

int SrsRtcPackets::capacity()
{
    return nn_cache;
}

SrsRtpPacket2* SrsRtcPackets::at(int index)
{
    srs_assert(index < cursor);
    return cache + index;
}

SrsRtcSenderThread::SrsRtcSenderThread(SrsRtcSession* s, SrsUdpMuxSocket* u, int parent_cid)
    : sendonly_ukt(NULL)
{
    _parent_cid = parent_cid;
    trd = new SrsDummyCoroutine();

    rtc_session = s;
    sendonly_ukt = u->copy_sendonly();
    sender = u->sender();

    gso = false;
    merge_nalus = false;
    max_padding = 0;

    audio_timestamp = 0;
    audio_sequence = 0;

    video_sequence = 0;

    mw_sleep = 0;
    mw_msgs = 0;
    realtime = true;

    _srs_config->subscribe(this);
}

SrsRtcSenderThread::~SrsRtcSenderThread()
{
    _srs_config->unsubscribe(this);

    srs_freep(trd);
    srs_freep(sendonly_ukt);
}

srs_error_t SrsRtcSenderThread::initialize(const uint32_t& vssrc, const uint32_t& assrc, const uint16_t& v_pt, const uint16_t& a_pt)
{
    srs_error_t err = srs_success;

    video_ssrc = vssrc;
    audio_ssrc = assrc;

    video_payload_type = v_pt;
    audio_payload_type = a_pt;

    gso = _srs_config->get_rtc_server_gso();
    merge_nalus = _srs_config->get_rtc_server_merge_nalus();
    max_padding = _srs_config->get_rtc_server_padding();
    srs_trace("RTC sender video(ssrc=%d, pt=%d), audio(ssrc=%d, pt=%d), package(gso=%d, merge_nalus=%d), padding=%d",
        video_ssrc, video_payload_type, audio_ssrc, audio_payload_type, gso, merge_nalus, max_padding);

    return err;
}

srs_error_t SrsRtcSenderThread::on_reload_rtc_server()
{
    gso = _srs_config->get_rtc_server_gso();
    merge_nalus = _srs_config->get_rtc_server_merge_nalus();
    max_padding = _srs_config->get_rtc_server_padding();

    srs_trace("Reload rtc_server gso=%d, merge_nalus=%d, max_padding=%d", gso, merge_nalus, max_padding);

    return srs_success;
}

srs_error_t SrsRtcSenderThread::on_reload_vhost_play(string vhost)
{
    SrsRequest* req = &rtc_session->request;

    if (req->vhost != vhost) {
        return srs_success;
    }

    realtime = _srs_config->get_realtime_enabled(req->vhost, true);
    mw_msgs = _srs_config->get_mw_msgs(req->vhost, realtime, true);
    mw_sleep = _srs_config->get_mw_sleep(req->vhost, true);

    srs_trace("Reload play realtime=%d, mw_msgs=%d, mw_sleep=%d", realtime, mw_msgs, mw_sleep);

    return srs_success;
}

srs_error_t SrsRtcSenderThread::on_reload_vhost_realtime(string vhost)
{
    return on_reload_vhost_play(vhost);
}

int SrsRtcSenderThread::cid()
{
    return trd->cid();
}

srs_error_t SrsRtcSenderThread::start()
{
    srs_error_t err = srs_success;
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("rtc_sender", this, _parent_cid);
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "rtc_sender");
    }
    
    return err;
}

void SrsRtcSenderThread::stop()
{
    trd->stop();
}

void SrsRtcSenderThread::stop_loop()
{
    trd->interrupt();
}

void SrsRtcSenderThread::update_sendonly_socket(SrsUdpMuxSocket* skt)
{
    srs_trace("session %s address changed, update %s -> %s",
        rtc_session->id().c_str(), sendonly_ukt->get_peer_id().c_str(), skt->get_peer_id().c_str());

    srs_freep(sendonly_ukt);
    sendonly_ukt = skt->copy_sendonly();
    sender = skt->sender();
}

srs_error_t SrsRtcSenderThread::cycle()
{
    srs_error_t err = srs_success;

    SrsSource* source = NULL;
    SrsRequest* req = &rtc_session->request;

    // TODO: FIXME: Should refactor it, directly use http server as handler.
    ISrsSourceHandler* handler = _srs_hybrid->srs()->instance();
    if ((err = _srs_sources->fetch_or_create(req, handler, &source)) != srs_success) {
        return srs_error_wrap(err, "rtc fetch source failed");
    }

    SrsConsumer* consumer = NULL;
    SrsAutoFree(SrsConsumer, consumer);
    if ((err = source->create_consumer(NULL, consumer)) != srs_success) {
        return srs_error_wrap(err, "rtc create consumer, source url=%s", req->get_stream_url().c_str());
    }

    // For RTC, we enable pass-timestamp mode, ignore the timestamp in queue, never depends on the duration,
    // because RTC allows the audio and video has its own timebase, that is the audio timestamp and video timestamp
    // maybe not monotonically increase.
    // In this mode, we use mw_msgs to set the delay. We never shrink the consumer queue, instead, we dumps the
    // messages and drop them if the shared sender queue is full.
    consumer->enable_pass_timestamp();

    realtime = _srs_config->get_realtime_enabled(req->vhost, true);
    mw_sleep = _srs_config->get_mw_sleep(req->vhost, true);
    mw_msgs = _srs_config->get_mw_msgs(req->vhost, realtime, true);

    // We merged write more messages, so we need larger queue.
    if (mw_msgs > 2) {
        sender->set_extra_ratio(150);
    } else if (mw_msgs > 0) {
        sender->set_extra_ratio(80);
    }

    srs_trace("RTC source url=%s, source_id=[%d][%d], encrypt=%d, realtime=%d, mw_sleep=%dms, mw_msgs=%d", req->get_stream_url().c_str(),
        ::getpid(), source->source_id(), rtc_session->encrypt, realtime, srsu2msi(mw_sleep), mw_msgs);

    SrsMessageArray msgs(SRS_PERF_MW_MSGS);
    SrsRtcPackets pkts(SRS_PERF_RTC_RTP_PACKETS);

    SrsPithyPrint* pprint = SrsPithyPrint::create_rtc_play();
    SrsAutoFree(SrsPithyPrint, pprint);

    bool stat_enabled = _srs_config->get_rtc_server_perf_stat();
    SrsStatistic* stat = SrsStatistic::instance();

    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "rtc sender thread");
        }

#ifdef SRS_PERF_QUEUE_COND_WAIT
        // Wait for amount of messages or a duration.
        consumer->wait(mw_msgs, mw_sleep);
#endif

        // Try to read some messages.
        int msg_count = 0;
        if ((err = consumer->dump_packets(&msgs, msg_count)) != srs_success) {
            continue;
        }

        if (msg_count <= 0) {
#ifndef SRS_PERF_QUEUE_COND_WAIT
            srs_usleep(mw_sleep);
#endif
            continue;
        }

        // Transmux and send out messages.
        pkts.reset(gso, merge_nalus);

        if ((err = send_messages(source, msgs.msgs, msg_count, pkts)) != srs_success) {
            srs_warn("send err %s", srs_error_summary(err).c_str()); srs_error_reset(err);
        }

        // Do cleanup messages.
        for (int i = 0; i < msg_count; i++) {
            SrsSharedPtrMessage* msg = msgs.msgs[i];
            srs_freep(msg);
        }

        // Stat for performance analysis.
        if (!stat_enabled) {
            continue;
        }

        // Stat the original RAW AV frame, maybe h264+aac.
        stat->perf_on_msgs(msg_count);
        // Stat the RTC packets, RAW AV frame, maybe h.264+opus.
        int nn_rtc_packets = srs_max(pkts.nn_audios, pkts.nn_extras) + pkts.nn_videos;
        stat->perf_on_rtc_packets(nn_rtc_packets);
        // Stat the RAW RTP packets, which maybe group by GSO.
        stat->perf_on_rtp_packets(pkts.size());
        // Stat the RTP packets going into kernel.
        stat->perf_on_gso_packets(pkts.nn_rtp_pkts);
        // Stat the bytes and paddings.
        stat->perf_on_rtc_bytes(pkts.nn_bytes, pkts.nn_rtp_bytes, pkts.nn_padding_bytes);
        // Stat the messages and dropped count.
        stat->perf_on_dropped(msg_count, nn_rtc_packets, pkts.nn_dropped);

#if defined(SRS_DEBUG)
        srs_trace("RTC PLAY perf, msgs %d/%d, rtp %d, gso %d, %d audios, %d extras, %d videos, %d samples, %d/%d/%d bytes",
            msg_count, nn_rtc_packets, pkts.size(), pkts.nn_rtp_pkts, pkts.nn_audios, pkts.nn_extras, pkts.nn_videos,
            pkts.nn_samples, pkts.nn_bytes, pkts.nn_rtp_bytes, pkts.nn_padding_bytes);
#endif

        pprint->elapse();
        if (pprint->can_print()) {
            // TODO: FIXME: Print stat like frame/s, packet/s, loss_packets.
            srs_trace("-> RTC PLAY %d/%d msgs, %d/%d packets, %d audios, %d extras, %d videos, %d samples, %d/%d/%d bytes, %d pad, %d/%d cache",
                msg_count, pkts.nn_dropped, pkts.size(), pkts.nn_rtp_pkts, pkts.nn_audios, pkts.nn_extras, pkts.nn_videos, pkts.nn_samples, pkts.nn_bytes,
                pkts.nn_rtp_bytes, pkts.nn_padding_bytes, pkts.nn_paddings, pkts.size(), pkts.capacity());
        }
    }
}

srs_error_t SrsRtcSenderThread::send_messages(
    SrsSource* source, SrsSharedPtrMessage** msgs, int nb_msgs, SrsRtcPackets& packets
) {
    srs_error_t err = srs_success;

    // If DTLS is not OK, drop all messages.
    if (!rtc_session->dtls_session) {
        return err;
    }

    // Covert kernel messages to RTP packets.
    if ((err = messages_to_packets(source, msgs, nb_msgs, packets)) != srs_success) {
        return srs_error_wrap(err, "messages to packets");
    }

#ifndef SRS_AUTO_OSX
    // If enabled GSO, send out some packets in a msghdr.
    if (packets.use_gso) {
        if ((err = send_packets_gso(packets)) != srs_success) {
            return srs_error_wrap(err, "gso send");
        }
        return err;
    }
#endif

    // By default, we send packets by sendmmsg.
    if ((err = send_packets(packets)) != srs_success) {
        return srs_error_wrap(err, "raw send");
    }

    return err;
}

srs_error_t SrsRtcSenderThread::messages_to_packets(
    SrsSource* source, SrsSharedPtrMessage** msgs, int nb_msgs, SrsRtcPackets& packets
) {
    srs_error_t err = srs_success;

    for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = msgs[i];

        // If overflow, drop all messages.
        if (sender->overflow()) {
            packets.nn_dropped += nb_msgs - i;
            return err;
        }

        // Update stats.
        packets.nn_bytes += msg->size;

        int nn_extra_payloads = msg->nn_extra_payloads();
        packets.nn_extras += nn_extra_payloads;

        int nn_samples = msg->nn_samples();
        packets.nn_samples += nn_samples;

        // For audio, we transcoded AAC to opus in extra payloads.
        if (msg->is_audio()) {
            packets.nn_audios++;

            for (int i = 0; i < nn_extra_payloads; i++) {
                SrsSample* sample = msg->extra_payloads() + i;
                if ((err = packet_opus(sample, packets, msg->nn_max_extra_payloads())) != srs_success) {
                    return srs_error_wrap(err, "opus package");
                }
            }
            continue;
        }

        // For video, we should process all NALUs in samples.
        packets.nn_videos++;

        // Well, for each IDR, we append a SPS/PPS before it, which is packaged in STAP-A.
        if (msg->has_idr()) {
            if ((err = packet_stap_a(source, msg, packets)) != srs_success) {
                return srs_error_wrap(err, "packet stap-a");
            }
        }

        // If merge Nalus, we pcakges all NALUs(samples) as one NALU, in a RTP or FUA packet.
        if (packets.should_merge_nalus && nn_samples > 1) {
            if ((err = packet_nalus(msg, packets)) != srs_success) {
                return srs_error_wrap(err, "packet stap-a");
            }
            continue;
        }

        // By default, we package each NALU(sample) to a RTP or FUA packet.
        for (int i = 0; i < nn_samples; i++) {
            SrsSample* sample = msg->samples() + i;

            // We always ignore bframe here, if config to discard bframe,
            // the bframe flag will not be set.
            if (sample->bframe) {
                continue;
            }

            if (sample->size <= kRtpMaxPayloadSize) {
                if ((err = packet_single_nalu(msg, sample, packets)) != srs_success) {
                    return srs_error_wrap(err, "packet single nalu");
                }
            } else {
                if ((err = packet_fu_a(msg, sample, kRtpMaxPayloadSize, packets)) != srs_success) {
                    return srs_error_wrap(err, "packet fu-a");
                }
            }

            if (i == nn_samples - 1) {
                packets.back()->rtp_header.set_marker(true);
            }
        }
    }

    return err;
}

srs_error_t SrsRtcSenderThread::send_packets(SrsRtcPackets& packets)
{
    srs_error_t err = srs_success;

    // Cache the encrypt flag.
    bool encrypt = rtc_session->encrypt;

    int nn_packets = packets.size();
    for (int i = 0; i < nn_packets; i++) {
        SrsRtpPacket2* packet = packets.at(i);

        // Fetch a cached message from queue.
        // TODO: FIXME: Maybe encrypt in async, so the state of mhdr maybe not ready.
        mmsghdr* mhdr = NULL;
        if ((err = sender->fetch(&mhdr)) != srs_success) {
            return srs_error_wrap(err, "fetch msghdr");
        }

        // For this message, select the first iovec.
        iovec* iov = mhdr->msg_hdr.msg_iov;
        mhdr->msg_hdr.msg_iovlen = 1;

        if (!iov->iov_base) {
            iov->iov_base = new char[kRtpPacketSize];
        }
        iov->iov_len = kRtpPacketSize;

        // Marshal packet to bytes in iovec.
        if (true) {
            SrsBuffer stream((char*)iov->iov_base, iov->iov_len);
            if ((err = packet->encode(&stream)) != srs_success) {
                return srs_error_wrap(err, "encode packet");
            }
            iov->iov_len = stream.pos();
        }

        // Whether encrypt the RTP bytes.
        if (encrypt) {
            int nn_encrypt = (int)iov->iov_len;
            if ((err = rtc_session->dtls_session->protect_rtp2(iov->iov_base, &nn_encrypt)) != srs_success) {
                return srs_error_wrap(err, "srtp protect");
            }
            iov->iov_len = (size_t)nn_encrypt;
        }

        packets.nn_rtp_bytes += (int)iov->iov_len;

        // Set the address and control information.
        sockaddr_in* addr = (sockaddr_in*)sendonly_ukt->peer_addr();
        socklen_t addrlen = (socklen_t)sendonly_ukt->peer_addrlen();

        mhdr->msg_hdr.msg_name = (sockaddr_in*)addr;
        mhdr->msg_hdr.msg_namelen = (socklen_t)addrlen;
        mhdr->msg_hdr.msg_controllen = 0;

        // When we send out a packet, we commit a RTP packet.
        packets.nn_rtp_pkts++;

        if ((err = sender->sendmmsg(mhdr)) != srs_success) {
            return srs_error_wrap(err, "send msghdr");
        }
    }

    return err;
}

// TODO: FIXME: We can gather and pad audios, because they have similar size.
srs_error_t SrsRtcSenderThread::send_packets_gso(SrsRtcPackets& packets)
{
    srs_error_t err = srs_success;

    // Cache the encrypt flag.
    bool encrypt = rtc_session->encrypt;

    // Previous handler, if has the same size, we can use GSO.
    mmsghdr* gso_mhdr = NULL; int gso_size = 0; int gso_encrypt = 0; int gso_cursor = 0;
    // GSO, N packets has same length, the final one may not.
    bool using_gso = false; bool gso_final = false;
    // The message will marshal in iovec.
    iovec* iov = NULL;

    int nn_packets = packets.size();
    for (int i = 0; i < nn_packets; i++) {
        SrsRtpPacket2* packet = packets.at(i);
        int nn_packet = packet->nb_bytes();
        int padding = 0;

        SrsRtpPacket2* next_packet = NULL;
        int nn_next_packet = 0;
        if (max_padding > 0) {
            if (i < nn_packets - 1) {
                next_packet = (i < nn_packets - 1)? packets.at(i + 1):NULL;
                nn_next_packet = next_packet? next_packet->nb_bytes() : 0;
            }

            // Padding the packet to next or GSO size.
            if (next_packet) {
                if (!using_gso) {
                    // Padding to the next packet to merge with it.
                    if (nn_next_packet > nn_packet) {
                        padding = nn_next_packet - nn_packet;
                    }
                } else {
                    // Padding to GSO size for next one to merge with us.
                    if (nn_next_packet < gso_size) {
                        padding = gso_size - nn_packet;
                    }
                }

                // Reset padding if exceed max.
                if (padding > max_padding) {
                    padding = 0;
                }

                if (padding > 0) {
#if defined(SRS_DEBUG)
                    srs_trace("#%d, Padding %d bytes %d=>%d, packets %d, max_padding %d", packets.debug_id,
                        padding, nn_packet, nn_packet + padding, nn_packets, max_padding);
#endif
                    packet->add_padding(padding);
                    nn_packet += padding;
                    packets.nn_paddings++;
                    packets.nn_padding_bytes += padding;
                }
            }
        }

        // Check whether we can use GSO to send it.
        if (using_gso && !gso_final) {
            gso_final = (gso_size != nn_packet);
        }

        if (next_packet) {
            // If not GSO, maybe the first fresh packet, we should see whether the next packet is smaller than this one,
            // if smaller, we can still enter GSO.
            if (!using_gso) {
                using_gso = (nn_packet >= nn_next_packet);
            }

            // If GSO, but next is bigger than this one, we must enter the final state.
            if (using_gso && !gso_final) {
                gso_final = (nn_packet < nn_next_packet);
            }
        }

        // For GSO, reuse mhdr if possible.
        mmsghdr* mhdr = gso_mhdr;
        if (!mhdr) {
            // Fetch a cached message from queue.
            // TODO: FIXME: Maybe encrypt in async, so the state of mhdr maybe not ready.
            if ((err = sender->fetch(&mhdr)) != srs_success) {
                return srs_error_wrap(err, "fetch msghdr");
            }

            // Now, GSO will use this message and size.
            gso_mhdr = mhdr;
            gso_size = nn_packet;
        }

        // For this message, select a new iovec.
        if (!iov) {
            iov = mhdr->msg_hdr.msg_iov;
        } else {
            iov++;
        }
        gso_cursor++;
        mhdr->msg_hdr.msg_iovlen = gso_cursor;

        if (gso_cursor > SRS_PERF_RTC_GSO_IOVS && !iov->iov_base) {
            iov->iov_base = new char[kRtpPacketSize];
        }
        iov->iov_len = kRtpPacketSize;

        // Marshal packet to bytes in iovec.
        if (true) {
            SrsBuffer stream((char*)iov->iov_base, iov->iov_len);
            if ((err = packet->encode(&stream)) != srs_success) {
                return srs_error_wrap(err, "encode packet");
            }
            iov->iov_len = stream.pos();
        }

        // Whether encrypt the RTP bytes.
        if (encrypt) {
            int nn_encrypt = (int)iov->iov_len;
            if ((err = rtc_session->dtls_session->protect_rtp2(iov->iov_base, &nn_encrypt)) != srs_success) {
                return srs_error_wrap(err, "srtp protect");
            }
            iov->iov_len = (size_t)nn_encrypt;
        }

        packets.nn_rtp_bytes += (int)iov->iov_len;

        // If GSO, they must has same size, except the final one.
        if (using_gso && !gso_final && gso_encrypt && gso_encrypt != (int)iov->iov_len) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "GSO size=%d/%d, encrypt=%d/%d", gso_size, nn_packet, gso_encrypt, iov->iov_len);
        }

        if (using_gso && !gso_final) {
            gso_encrypt = iov->iov_len;
        }

        // If exceed the max GSO size, set to final.
        if (using_gso && gso_cursor + 1 >= SRS_PERF_RTC_GSO_MAX) {
            gso_final = true;
        }

        // For last message, or final gso, or determined not using GSO, send it now.
        bool do_send = (i == nn_packets - 1 || gso_final || !using_gso);

#if defined(SRS_DEBUG)
        bool is_video = packet->rtp_header.get_payload_type() == video_payload_type;
        srs_trace("#%d, Packet %s SSRC=%d, SN=%d, %d/%d bytes", packets.debug_id, is_video? "Video":"Audio",
            packet->rtp_header.get_ssrc(), packet->rtp_header.get_sequence(), nn_packet - padding, padding);
        if (do_send) {
            for (int j = 0; j < (int)mhdr->msg_hdr.msg_iovlen; j++) {
                iovec* iov = mhdr->msg_hdr.msg_iov + j;
                srs_trace("#%d, %s #%d/%d/%d, %d/%d bytes, size %d/%d", packets.debug_id, (using_gso? "GSO":"RAW"), j,
                    gso_cursor + 1, mhdr->msg_hdr.msg_iovlen, iov->iov_len, padding, gso_size, gso_encrypt);
            }
        }
#endif

        if (do_send) {
            // Set the address and control information.
            sockaddr_in* addr = (sockaddr_in*)sendonly_ukt->peer_addr();
            socklen_t addrlen = (socklen_t)sendonly_ukt->peer_addrlen();

            mhdr->msg_hdr.msg_name = (sockaddr_in*)addr;
            mhdr->msg_hdr.msg_namelen = (socklen_t)addrlen;
            mhdr->msg_hdr.msg_controllen = 0;

#ifndef SRS_AUTO_OSX
            if (using_gso) {
                mhdr->msg_hdr.msg_controllen = CMSG_SPACE(sizeof(uint16_t));
                if (!mhdr->msg_hdr.msg_control) {
                    mhdr->msg_hdr.msg_control = new char[mhdr->msg_hdr.msg_controllen];
                }

                cmsghdr* cm = CMSG_FIRSTHDR(&mhdr->msg_hdr);
                cm->cmsg_level = SOL_UDP;
                cm->cmsg_type = UDP_SEGMENT;
                cm->cmsg_len = CMSG_LEN(sizeof(uint16_t));
                *((uint16_t*)CMSG_DATA(cm)) = gso_encrypt;
            }
#endif

            // When we send out a packet, we commit a RTP packet.
            packets.nn_rtp_pkts++;

            if ((err = sender->sendmmsg(mhdr)) != srs_success) {
                return srs_error_wrap(err, "send msghdr");
            }

            // Reset the GSO flag.
            gso_mhdr = NULL; gso_size = 0; gso_encrypt = 0; gso_cursor = 0;
            using_gso = gso_final = false; iov = NULL;
        }
    }

#if defined(SRS_DEBUG)
    srs_trace("#%d, RTC PLAY summary, rtp %d/%d, videos %d/%d, audios %d/%d, pad %d/%d/%d", packets.debug_id, packets.size(),
        packets.nn_rtp_pkts, packets.nn_videos, packets.nn_samples, packets.nn_audios, packets.nn_extras, packets.nn_paddings,
        packets.nn_padding_bytes, packets.nn_rtp_bytes);
#endif

    return err;
}

srs_error_t SrsRtcSenderThread::packet_nalus(SrsSharedPtrMessage* msg, SrsRtcPackets& packets)
{
    srs_error_t err = srs_success;

    SrsRtpRawNALUs* raw = new SrsRtpRawNALUs();

    for (int i = 0; i < msg->nn_samples(); i++) {
        SrsSample* sample = msg->samples() + i;

        // We always ignore bframe here, if config to discard bframe,
        // the bframe flag will not be set.
        if (sample->bframe) {
            continue;
        }

        raw->push_back(sample->copy());
    }

    // Ignore empty.
    int nn_bytes = raw->nb_bytes();
    if (nn_bytes <= 0) {
        srs_freep(raw);
        return err;
    }

    if (nn_bytes < kRtpMaxPayloadSize) {
        // Package NALUs in a single RTP packet.
        SrsRtpPacket2* packet = packets.fetch();
        if (!packet) {
            srs_freep(raw);
            return srs_error_new(ERROR_RTC_RTP_MUXER, "cache empty");
        }

        packet->rtp_header.set_timestamp(msg->timestamp * 90);
        packet->rtp_header.set_sequence(video_sequence++);
        packet->rtp_header.set_ssrc(video_ssrc);
        packet->rtp_header.set_payload_type(video_payload_type);

        packet->payload = raw;
    } else {
        // We must free it, should never use RTP packets to free it,
        // because more than one RTP packet will refer to it.
        SrsAutoFree(SrsRtpRawNALUs, raw);

        // Package NALUs in FU-A RTP packets.
        int fu_payload_size = kRtpMaxPayloadSize;

        // The first byte is store in FU-A header.
        uint8_t header = raw->skip_first_byte();
        uint8_t nal_type = header & kNalTypeMask;
        int nb_left = nn_bytes - 1;

        int num_of_packet = 1 + (nn_bytes - 1) / fu_payload_size;
        for (int i = 0; i < num_of_packet; ++i) {
            int packet_size = srs_min(nb_left, fu_payload_size);

            SrsRtpPacket2* packet = packets.fetch();
            if (!packet) {
                srs_freep(raw);
                return srs_error_new(ERROR_RTC_RTP_MUXER, "cache empty");
            }

            packet->rtp_header.set_timestamp(msg->timestamp * 90);
            packet->rtp_header.set_sequence(video_sequence++);
            packet->rtp_header.set_ssrc(video_ssrc);
            packet->rtp_header.set_payload_type(video_payload_type);

            SrsRtpFUAPayload* fua = new SrsRtpFUAPayload();
            packet->payload = fua;

            fua->nri = (SrsAvcNaluType)header;
            fua->nalu_type = (SrsAvcNaluType)nal_type;
            fua->start = bool(i == 0);
            fua->end = bool(i == num_of_packet - 1);

            if ((err = raw->read_samples(fua->nalus, packet_size)) != srs_success) {
                return srs_error_wrap(err, "read samples %d bytes, left %d, total %d", packet_size, nb_left, nn_bytes);
            }

            nb_left -= packet_size;
        }
    }

    if (packets.size() > 0) {
        packets.back()->rtp_header.set_marker(true);
    }

    return err;
}

srs_error_t SrsRtcSenderThread::packet_opus(SrsSample* sample, SrsRtcPackets& packets, int nn_max_payload)
{
    srs_error_t err = srs_success;

    SrsRtpPacket2* packet = packets.fetch();
    if (!packet) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "cache empty");
    }
    packet->rtp_header.set_marker(true);
    packet->rtp_header.set_timestamp(audio_timestamp);
    packet->rtp_header.set_sequence(audio_sequence++);
    packet->rtp_header.set_ssrc(audio_ssrc);
    packet->rtp_header.set_payload_type(audio_payload_type);

    SrsRtpRawPayload* raw = packet->reuse_raw();
    raw->payload = sample->bytes;
    raw->nn_payload = sample->size;

    if (max_padding > 0) {
        if (sample->size < nn_max_payload && nn_max_payload - sample->size < max_padding) {
            int padding = nn_max_payload - sample->size;
            packet->set_padding(padding);

#if defined(SRS_DEBUG)
            srs_trace("#%d, Fast Padding %d bytes %d=>%d, SN=%d, max_payload %d, max_padding %d", packets.debug_id,
                padding, sample->size, sample->size + padding, packet->rtp_header.get_sequence(), nn_max_payload, max_padding);
#endif
        }
    }

    // TODO: FIXME: Why 960? Need Refactoring?
    audio_timestamp += 960;

    return err;
}

srs_error_t SrsRtcSenderThread::packet_fu_a(SrsSharedPtrMessage* msg, SrsSample* sample, int fu_payload_size, SrsRtcPackets& packets)
{
    srs_error_t err = srs_success;

    char* p = sample->bytes + 1;
    int nb_left = sample->size - 1;
    uint8_t header = sample->bytes[0];
    uint8_t nal_type = header & kNalTypeMask;

    int num_of_packet = 1 + (sample->size - 1) / fu_payload_size;
    for (int i = 0; i < num_of_packet; ++i) {
        int packet_size = srs_min(nb_left, fu_payload_size);

        SrsRtpPacket2* packet = packets.fetch();
        if (!packet) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "cache empty");
        }

        packet->rtp_header.set_timestamp(msg->timestamp * 90);
        packet->rtp_header.set_sequence(video_sequence++);
        packet->rtp_header.set_ssrc(video_ssrc);
        packet->rtp_header.set_payload_type(video_payload_type);

        SrsRtpFUAPayload2* fua = packet->reuse_fua();

        fua->nri = (SrsAvcNaluType)header;
        fua->nalu_type = (SrsAvcNaluType)nal_type;
        fua->start = bool(i == 0);
        fua->end = bool(i == num_of_packet - 1);

        fua->payload = p;
        fua->size = packet_size;

        p += packet_size;
        nb_left -= packet_size;
    }

    return err;
}

// Single NAL Unit Packet @see https://tools.ietf.org/html/rfc6184#section-5.6
srs_error_t SrsRtcSenderThread::packet_single_nalu(SrsSharedPtrMessage* msg, SrsSample* sample, SrsRtcPackets& packets)
{
    srs_error_t err = srs_success;

    SrsRtpPacket2* packet = packets.fetch();
    if (!packet) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "cache empty");
    }
    packet->rtp_header.set_timestamp(msg->timestamp * 90);
    packet->rtp_header.set_sequence(video_sequence++);
    packet->rtp_header.set_ssrc(video_ssrc);
    packet->rtp_header.set_payload_type(video_payload_type);

    SrsRtpRawPayload* raw = packet->reuse_raw();
    raw->payload = sample->bytes;
    raw->nn_payload = sample->size;

    return err;
}

srs_error_t SrsRtcSenderThread::packet_stap_a(SrsSource* source, SrsSharedPtrMessage* msg, SrsRtcPackets& packets)
{
    srs_error_t err = srs_success;

    SrsMetaCache* meta = source->cached_meta();
    if (!meta) {
        return err;
    }

    SrsFormat* format = meta->vsh_format();
    if (!format || !format->vcodec) {
        return err;
    }

    const vector<char>& sps = format->vcodec->sequenceParameterSetNALUnit;
    const vector<char>& pps = format->vcodec->pictureParameterSetNALUnit;
    if (sps.empty() || pps.empty()) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "sps/pps empty");
    }

    SrsRtpPacket2* packet = packets.fetch();
    if (!packet) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "cache empty");
    }
    packet->rtp_header.set_marker(false);
    packet->rtp_header.set_timestamp(msg->timestamp * 90);
    packet->rtp_header.set_sequence(video_sequence++);
    packet->rtp_header.set_ssrc(video_ssrc);
    packet->rtp_header.set_payload_type(video_payload_type);

    SrsRtpSTAPPayload* stap = new SrsRtpSTAPPayload();
    packet->payload = stap;

    uint8_t header = sps[0];
    stap->nri = (SrsAvcNaluType)header;

    if (true) {
        SrsSample* sample = new SrsSample();
        sample->bytes = (char*)&sps[0];
        sample->size = (int)sps.size();
        stap->nalus.push_back(sample);
    }

    if (true) {
        SrsSample* sample = new SrsSample();
        sample->bytes = (char*)&pps[0];
        sample->size = (int)pps.size();
        stap->nalus.push_back(sample);
    }

    return err;
}

SrsRtcSession::SrsRtcSession(SrsRtcServer* rtc_svr, const SrsRequest& req, const std::string& un, int context_id)
{
    rtc_server = rtc_svr;
    session_state = INIT;
    dtls_session = new SrsDtlsSession(this);
    dtls_session->initialize(req);
    strd = NULL;

    username = un;
    
    last_stun_time = srs_get_system_time();

    request = req;
    source = NULL;

    cid = context_id;
    encrypt = true;

    // TODO: FIXME: Support reload.
    sessionStunTimeout = _srs_config->get_rtc_stun_timeout(req.vhost);
}

SrsRtcSession::~SrsRtcSession()
{
    srs_freep(dtls_session);

    if (strd) {
        strd->stop();
    }
    srs_freep(strd);
}

void SrsRtcSession::set_local_sdp(const SrsSdp& sdp)
{
    local_sdp = sdp;
}

void SrsRtcSession::switch_to_context()
{
    _srs_context->set_id(cid);
}

srs_error_t SrsRtcSession::on_stun(SrsUdpMuxSocket* skt, SrsStunPacket* stun_req)
{
    srs_error_t err = srs_success;

    if (stun_req->is_binding_request()) {
        if ((err = on_binding_request(skt, stun_req)) != srs_success) {
            return srs_error_wrap(err, "stun binding request failed");
        }

        last_stun_time = srs_get_system_time();

        if (strd && strd->sendonly_ukt) {
            // We are running in the ice-lite(server) mode. If client have multi network interface,
            // we only choose one candidate pair which is determined by client.
            if (stun_req->get_use_candidate() && strd->sendonly_ukt->get_peer_id() != skt->get_peer_id()) {
                strd->update_sendonly_socket(skt);
            }
        }
    }

    return err;
}

srs_error_t SrsRtcSession::check_source()
{
    srs_error_t err = srs_success;

    if (source == NULL) {
        // TODO: FIXME: Should refactor it, directly use http server as handler.
        ISrsSourceHandler* handler = _srs_hybrid->srs()->instance();
        if ((err = _srs_sources->fetch_or_create(&request, handler, &source)) != srs_success) {
            return srs_error_wrap(err, "create source");
        }
    }

    return err;
}

#ifdef SRS_AUTO_OSX
// These functions are similar to the older byteorder(3) family of functions.
// For example, be32toh() is identical to ntohl().
// @see https://linux.die.net/man/3/be32toh
#define be32toh ntohl
#endif

srs_error_t SrsRtcSession::on_binding_request(SrsUdpMuxSocket* skt, SrsStunPacket* stun_req)
{
    srs_error_t err = srs_success;

    bool strict_check = _srs_config->get_rtc_stun_strict_check(request.vhost);
    if (strict_check && stun_req->get_ice_controlled()) {
        // @see: https://tools.ietf.org/html/draft-ietf-ice-rfc5245bis-00#section-6.1.3.1
        // TODO: Send 487 (Role Conflict) error response.
        return srs_error_new(ERROR_RTC_STUN, "Peer must not in ice-controlled role in ice-lite mode.");
    }

    SrsStunPacket stun_binding_response;
    char buf[kRtpPacketSize];
    SrsBuffer* stream = new SrsBuffer(buf, sizeof(buf));
    SrsAutoFree(SrsBuffer, stream);

    stun_binding_response.set_message_type(BindingResponse);
    stun_binding_response.set_local_ufrag(stun_req->get_remote_ufrag());
    stun_binding_response.set_remote_ufrag(stun_req->get_local_ufrag());
    stun_binding_response.set_transcation_id(stun_req->get_transcation_id());
    // FIXME: inet_addr is deprecated, IPV6 support
    stun_binding_response.set_mapped_address(be32toh(inet_addr(skt->get_peer_ip().c_str())));
    stun_binding_response.set_mapped_port(skt->get_peer_port());

    if ((err = stun_binding_response.encode(get_local_sdp()->get_ice_pwd(), stream)) != srs_success) {
        return srs_error_wrap(err, "stun binding response encode failed");
    }

    if ((err = skt->sendto(stream->data(), stream->pos(), 0)) != srs_success) {
        return srs_error_wrap(err, "stun binding response send failed");
    }

    if (get_session_state() == WAITING_STUN) {
        set_session_state(DOING_DTLS_HANDSHAKE);

        peer_id = skt->get_peer_id();
        rtc_server->insert_into_id_sessions(peer_id, this);
    }

    return err;
}

srs_error_t SrsRtcSession::on_rtcp_feedback(char* buf, int nb_buf, SrsUdpMuxSocket* skt)
{
    srs_error_t err = srs_success;

    if (nb_buf < 12) {
        return srs_error_new(ERROR_RTC_RTCP_CHECK, "invalid rtp feedback packet, nb_buf=%d", nb_buf);
    }

    SrsBuffer* stream = new SrsBuffer(buf, nb_buf);
    SrsAutoFree(SrsBuffer, stream);

    // @see: https://tools.ietf.org/html/rfc4585#section-6.1
    /*
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |V=2|P|   FMT   |       PT      |          length               |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                  SSRC of packet sender                        |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                  SSRC of media source                         |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       :            Feedback Control Information (FCI)                 :
       :                                                               :
    */
    /*uint8_t first = */stream->read_1bytes();
    //uint8_t version = first & 0xC0;
    //uint8_t padding = first & 0x20;
    //uint8_t fmt = first & 0x1F;

    /*uint8_t payload_type = */stream->read_1bytes();
    /*uint16_t length = */stream->read_2bytes();
    /*uint32_t ssrc_of_sender = */stream->read_4bytes();
    /*uint32_t ssrc_of_media_source = */stream->read_4bytes();

    /*
         0                   1                   2                   3
         0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |            PID                |             BLP               |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */

    uint16_t pid = stream->read_2bytes();
    int blp = stream->read_2bytes();

    srs_verbose("pid=%u, blp=%d", pid, blp);

    if ((err = check_source()) != srs_success) {
        return srs_error_wrap(err, "check");
    }
    if (! source) {
        return srs_error_new(ERROR_RTC_SOURCE_CHECK, "can not found source");
    }

    vector<SrsRtpSharedPacket*> resend_pkts;
    SrsRtpSharedPacket* pkt = source->find_rtp_packet(pid);
    if (pkt) {
        resend_pkts.push_back(pkt);
    }

    uint16_t mask = 0x01;
    for (int i = 1; i < 16 && blp; ++i, mask <<= 1) {
        if (! (blp & mask)) {
            continue;
        }

        uint32_t loss_seq = pid + i;

        SrsRtpSharedPacket* pkt = source->find_rtp_packet(loss_seq);
        if (! pkt) {
            continue;
        }

        resend_pkts.push_back(pkt);
    }

    for (int i = 0; i < (int)resend_pkts.size(); ++i) {
        if (dtls_session) {
            char protected_buf[kRtpPacketSize];
            int nb_protected_buf = resend_pkts[i]->size;

            srs_verbose("resend pkt sequence=%u", resend_pkts[i]->rtp_header.get_sequence());

            dtls_session->protect_rtp(protected_buf, resend_pkts[i]->payload, nb_protected_buf);
            skt->sendto(protected_buf, nb_protected_buf, 0);
        }
    }

    return err;
}

srs_error_t SrsRtcSession::on_rtcp_ps_feedback(char* buf, int nb_buf, SrsUdpMuxSocket* skt)
{
    srs_error_t err = srs_success;

    if (nb_buf < 12) {
        return srs_error_new(ERROR_RTC_RTCP_CHECK, "invalid rtp feedback packet, nb_buf=%d", nb_buf);
    }

    SrsBuffer* stream = new SrsBuffer(buf, nb_buf);
    SrsAutoFree(SrsBuffer, stream);

    uint8_t first = stream->read_1bytes();
    //uint8_t version = first & 0xC0;
    //uint8_t padding = first & 0x20;
    uint8_t fmt = first & 0x1F;

    // TODO: FIXME: Dead code?
    /*uint8_t payload_type = */stream->read_1bytes();
    /*uint16_t length = */stream->read_2bytes();
    /*uint32_t ssrc_of_sender = */stream->read_4bytes();
    /*uint32_t ssrc_of_media_source = */stream->read_4bytes();

    switch (fmt) {
        case kPLI: {
            srs_verbose("pli");
            break;
        }
        case kSLI: {
            srs_verbose("sli");
            break;
        }
        case kRPSI: {
            srs_verbose("rpsi");
            break;
        }
        case kAFB: {
            srs_verbose("afb");
            break;
        }
        default: {
            return srs_error_new(ERROR_RTC_RTCP, "unknown payload specific feedback=%u", fmt);
        }
    }

    return err;
}

srs_error_t SrsRtcSession::on_rtcp_receiver_report(char* buf, int nb_buf, SrsUdpMuxSocket* skt)
{
    srs_error_t err = srs_success;

    if (nb_buf < 8) {
        return srs_error_new(ERROR_RTC_RTCP_CHECK, "invalid rtp receiver report packet, nb_buf=%d", nb_buf);
    }

    SrsBuffer* stream = new SrsBuffer(buf, nb_buf);
    SrsAutoFree(SrsBuffer, stream);

    // @see: https://tools.ietf.org/html/rfc3550#section-6.4.2
    /*
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
header |V=2|P|    RC   |   PT=RR=201   |             length            |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                     SSRC of packet sender                     |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
report |                 SSRC_1 (SSRC of first source)                 |
block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  1    | fraction lost |       cumulative number of packets lost       |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |           extended highest sequence number received           |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                      interarrival jitter                      |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         last SR (LSR)                         |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                   delay since last SR (DLSR)                  |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
report |                 SSRC_2 (SSRC of second source)                |
block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  2    :                               ...                             :
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
       |                  profile-specific extensions                  |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    uint8_t first = stream->read_1bytes();
    //uint8_t version = first & 0xC0;
    //uint8_t padding = first & 0x20;
    uint8_t rc = first & 0x1F;

    /*uint8_t payload_type = */stream->read_1bytes();
    uint16_t length = stream->read_2bytes();
    /*uint32_t ssrc_of_sender = */stream->read_4bytes();

    if (((length + 1) * 4) != (rc * 24 + 8)) {
        return srs_error_new(ERROR_RTC_RTCP_CHECK, "invalid rtcp receiver packet, length=%u, rc=%u", length, rc);
    }

    for (int i = 0; i < rc; ++i) {
        uint32_t ssrc = stream->read_4bytes();
        uint8_t fraction_lost = stream->read_1bytes();
        uint32_t cumulative_number_of_packets_lost = stream->read_3bytes();
        uint32_t highest_seq = stream->read_4bytes();
        uint32_t jitter = stream->read_4bytes();
        uint32_t lst = stream->read_4bytes();
        uint32_t dlsr = stream->read_4bytes();

        (void)ssrc; (void)fraction_lost; (void)cumulative_number_of_packets_lost; (void)highest_seq; (void)jitter; (void)lst; (void)dlsr;
        srs_verbose("ssrc=%u, fraction_lost=%u, cumulative_number_of_packets_lost=%u, highest_seq=%u, jitter=%u, lst=%u, dlst=%u",
            ssrc, fraction_lost, cumulative_number_of_packets_lost, highest_seq, jitter, lst, dlsr);
    }

    return err;
}

srs_error_t SrsRtcSession::on_connection_established(SrsUdpMuxSocket* skt)
{
    srs_trace("rtc session=%s, to=%dms connection established", id().c_str(), srsu2msi(sessionStunTimeout));
    return start_play(skt);
}

srs_error_t SrsRtcSession::start_play(SrsUdpMuxSocket* skt)
{
    srs_error_t err = srs_success;

    srs_freep(strd);
    strd = new SrsRtcSenderThread(this, skt, _srs_context->get_id());

    uint32_t video_ssrc = 0;
    uint32_t audio_ssrc = 0;
    uint16_t video_payload_type = 0;
    uint16_t audio_payload_type = 0;
    for (size_t i = 0; i < local_sdp.media_descs_.size(); ++i) {
        const SrsMediaDesc& media_desc = local_sdp.media_descs_[i];
        if (media_desc.is_audio()) {
            audio_ssrc = media_desc.ssrc_infos_[0].ssrc_;
            audio_payload_type = media_desc.payload_types_[0].payload_type_;
        } else if (media_desc.is_video()) {
            video_ssrc = media_desc.ssrc_infos_[0].ssrc_;
            video_payload_type = media_desc.payload_types_[0].payload_type_;
        }
    }

    if ((err =strd->initialize(video_ssrc, audio_ssrc, video_payload_type, audio_payload_type)) != srs_success) {
        return srs_error_wrap(err, "SrsRtcSenderThread init");
    }

    if ((err = strd->start()) != srs_success) {
        return srs_error_wrap(err, "start SrsRtcSenderThread");
    }

    return err;
}

bool SrsRtcSession::is_stun_timeout()
{
    return last_stun_time + sessionStunTimeout < srs_get_system_time();
}

srs_error_t SrsRtcSession::on_dtls(SrsUdpMuxSocket* skt)
{
    return dtls_session->on_dtls(skt);
}

srs_error_t SrsRtcSession::on_rtcp(SrsUdpMuxSocket* skt)
{
    srs_error_t err = srs_success;

    if (dtls_session == NULL) {
        return srs_error_new(ERROR_RTC_RTCP, "recv unexpect rtp packet before dtls done");
    }

    char unprotected_buf[kRtpPacketSize];
    int nb_unprotected_buf = skt->size();
    if ((err = dtls_session->unprotect_rtcp(unprotected_buf, skt->data(), nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "rtcp unprotect failed");
    }

    char* ph = unprotected_buf;
    int nb_left = nb_unprotected_buf;
    while (nb_left) {
        uint8_t payload_type = ph[1];
        uint16_t length_4bytes = (((uint16_t)ph[2]) << 8) | ph[3];

        int length = (length_4bytes + 1) * 4;

        if (length > nb_unprotected_buf) {
            return srs_error_new(ERROR_RTC_RTCP, "invalid rtcp packet, length=%u", length);
        }

        srs_verbose("on rtcp, payload_type=%u", payload_type);

        switch (payload_type) {
            case kSR: {
                break;
            }
            case kRR: {
                err = on_rtcp_receiver_report(ph, length, skt);
                break;
            }
            case kSDES: {
                break;
            }
            case kBye: {
                break;
            }
            case kApp: {
                break;
            }
            case kRtpFb: {
                err = on_rtcp_feedback(ph, length, skt);
                break;
            }
            case kPsFb: {
                err = on_rtcp_ps_feedback(ph, length, skt);
                break;
            }
            default:{
                return srs_error_new(ERROR_RTC_RTCP_CHECK, "unknown rtcp type=%u", payload_type);
                break;
            }
        }

        if (err != srs_success) {
            return srs_error_wrap(err, "rtcp");
        }

        ph += length;
        nb_left -= length;
    }

    return err;
}

SrsUdpMuxSender::SrsUdpMuxSender(SrsRtcServer* s)
{
    lfd = NULL;
    server = s;

    waiting_msgs = false;
    cond = srs_cond_new();
    trd = new SrsDummyCoroutine();

    cache_pos = 0;
    max_sendmmsg = 0;
    queue_length = 0;
    extra_ratio = 0;
    extra_queue = 0;
    gso = false;
    nn_senders = 0;

    _srs_config->subscribe(this);
}

SrsUdpMuxSender::~SrsUdpMuxSender()
{
    _srs_config->unsubscribe(this);

    srs_freep(trd);
    srs_cond_destroy(cond);

    free_mhdrs(hotspot);
    hotspot.clear();

    free_mhdrs(cache);
    cache.clear();
}

srs_error_t SrsUdpMuxSender::initialize(srs_netfd_t fd, int senders)
{
    srs_error_t err = srs_success;

    lfd = fd;

    srs_freep(trd);
    trd = new SrsSTCoroutine("udp", this);
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "start coroutine");
    }

    max_sendmmsg = _srs_config->get_rtc_server_sendmmsg();
    gso = _srs_config->get_rtc_server_gso();
    queue_length = srs_max(128, _srs_config->get_rtc_server_queue_length());
    nn_senders = senders;

    // For no GSO, we need larger queue.
    if (!gso) {
        queue_length *= 2;
    }

    srs_trace("RTC sender #%d init ok, max_sendmmsg=%d, gso=%d, queue_max=%dx%d, extra_ratio=%d/%d", srs_netfd_fileno(fd),
        max_sendmmsg, gso, queue_length, nn_senders, extra_ratio, extra_queue);

    return err;
}

void SrsUdpMuxSender::free_mhdrs(std::vector<mmsghdr>& mhdrs)
{
    int nn_mhdrs = (int)mhdrs.size();
    for (int i = 0; i < nn_mhdrs; i++) {
        // @see https://linux.die.net/man/2/sendmmsg
        // @see https://linux.die.net/man/2/sendmsg
        mmsghdr* hdr = &mhdrs[i];

        // Free control for GSO.
        char* msg_control = (char*)hdr->msg_hdr.msg_control;
        srs_freepa(msg_control);

        // Free iovec.
        for (int j = SRS_PERF_RTC_GSO_MAX - 1; j >= 0 ; j--) {
            iovec* iov = hdr->msg_hdr.msg_iov + j;
            char* data = (char*)iov->iov_base;
            srs_freepa(data);
            srs_freepa(iov);
        }
    }
    mhdrs.clear();
}

srs_error_t SrsUdpMuxSender::fetch(mmsghdr** pphdr)
{
    // TODO: FIXME: Maybe need to shrink?
    if (cache_pos >= (int)cache.size()) {
        // @see https://linux.die.net/man/2/sendmmsg
        // @see https://linux.die.net/man/2/sendmsg
        mmsghdr mhdr;

        mhdr.msg_len = 0;
        mhdr.msg_hdr.msg_flags = 0;
        mhdr.msg_hdr.msg_control = NULL;

        mhdr.msg_hdr.msg_iovlen = SRS_PERF_RTC_GSO_MAX;
        mhdr.msg_hdr.msg_iov = new iovec[mhdr.msg_hdr.msg_iovlen];
        memset((void*)mhdr.msg_hdr.msg_iov, 0, sizeof(iovec) * mhdr.msg_hdr.msg_iovlen);

        for (int i = 0; i < SRS_PERF_RTC_GSO_IOVS; i++) {
            iovec* p = mhdr.msg_hdr.msg_iov + i;
            p->iov_base = new char[kRtpPacketSize];
        }

        cache.push_back(mhdr);
    }

    *pphdr = &cache[cache_pos++];
    return srs_success;
}

bool SrsUdpMuxSender::overflow()
{
    return cache_pos > queue_length + extra_queue;
}

void SrsUdpMuxSender::set_extra_ratio(int r)
{
    // We use the larger extra ratio, because all vhosts shares the senders.
    if (extra_ratio > r) {
        return;
    }

    extra_ratio = r;
    extra_queue = queue_length * r / 100;

    srs_trace("RTC sender #%d extra queue, max_sendmmsg=%d, gso=%d, queue_max=%dx%d, extra_ratio=%d/%d, cache=%d/%d/%d", srs_netfd_fileno(lfd),
        max_sendmmsg, gso, queue_length, nn_senders, extra_ratio, extra_queue, cache_pos, (int)cache.size(), (int)hotspot.size());
}

srs_error_t SrsUdpMuxSender::sendmmsg(mmsghdr* hdr)
{
    if (waiting_msgs) {
        waiting_msgs = false;
        srs_cond_signal(cond);
    }

    return srs_success;
}

srs_error_t SrsUdpMuxSender::cycle()
{
    srs_error_t err = srs_success;

    uint64_t nn_msgs = 0; uint64_t nn_msgs_last = 0; int nn_msgs_max = 0;
    uint64_t nn_bytes = 0; int nn_bytes_max = 0;
    uint64_t nn_gso_msgs = 0; uint64_t nn_gso_iovs = 0; int nn_gso_msgs_max = 0; int nn_gso_iovs_max = 0;
    int nn_loop = 0; int nn_wait = 0;
    srs_utime_t time_last = srs_get_system_time();

    bool stat_enabled = _srs_config->get_rtc_server_perf_stat();
    SrsStatistic* stat = SrsStatistic::instance();

    SrsPithyPrint* pprint = SrsPithyPrint::create_rtc_send(srs_netfd_fileno(lfd));
    SrsAutoFree(SrsPithyPrint, pprint);

    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return err;
        }

        nn_loop++;

        int pos = cache_pos;
        int gso_iovs = 0;
        if (pos <= 0) {
            waiting_msgs = true;
            nn_wait++;
            srs_cond_wait(cond);
            continue;
        }

        // We are working on hotspot now.
        cache.swap(hotspot);
        cache_pos = 0;

        int gso_pos = 0;
        int nn_writen = 0;
        if (pos > 0) {
            // Send out all messages.
            // @see https://linux.die.net/man/2/sendmmsg
            // @see https://linux.die.net/man/2/sendmsg
            mmsghdr* p = &hotspot[0]; mmsghdr* end = p + pos;
            for (p = &hotspot[0]; p < end; p += max_sendmmsg) {
                int vlen = (int)(end - p);
                vlen = srs_min(max_sendmmsg, vlen);

                int r0 = srs_sendmmsg(lfd, p, (unsigned int)vlen, 0, SRS_UTIME_NO_TIMEOUT);
                if (r0 != vlen) {
                    srs_warn("sendmmsg %d msgs, %d done", vlen, r0);
                }

                if (stat_enabled) {
                    stat->perf_on_sendmmsg_packets(vlen);
                }
            }

            // Collect informations for GSO.
            if (stat_enabled) {
                // Stat the messages, iovs and bytes.
                // @see https://linux.die.net/man/2/sendmmsg
                // @see https://linux.die.net/man/2/sendmsg
                for (int i = 0; i < pos; i++) {
                    mmsghdr* mhdr = &hotspot[i];

                    nn_writen += (int)mhdr->msg_len;

                    int real_iovs = mhdr->msg_hdr.msg_iovlen;
                    gso_pos++; nn_gso_msgs++; nn_gso_iovs += real_iovs;
                    gso_iovs += real_iovs;
                }
            }
        }

        if (!stat_enabled) {
            continue;
        }

        // Increase total messages.
        nn_msgs += pos + gso_iovs;
        nn_msgs_max = srs_max(pos, nn_msgs_max);
        nn_bytes += nn_writen;
        nn_bytes_max = srs_max(nn_bytes_max, nn_writen);
        nn_gso_msgs_max = srs_max(gso_pos, nn_gso_msgs_max);
        nn_gso_iovs_max = srs_max(gso_iovs, nn_gso_iovs_max);

        pprint->elapse();
        if (pprint->can_print()) {
            // TODO: FIXME: Extract a PPS calculator.
            int pps_average = 0; int pps_last = 0;
            if (true) {
                if (srs_get_system_time() > srs_get_system_startup_time()) {
                    pps_average = (int)(nn_msgs * SRS_UTIME_SECONDS / (srs_get_system_time() - srs_get_system_startup_time()));
                }
                if (srs_get_system_time() > time_last) {
                    pps_last = (int)((nn_msgs - nn_msgs_last) * SRS_UTIME_SECONDS / (srs_get_system_time() - time_last));
                }
            }

            string pps_unit = "";
            if (pps_last > 10000 || pps_average > 10000) {
                pps_unit = "(w)"; pps_last /= 10000; pps_average /= 10000;
            } else if (pps_last > 1000 || pps_average > 1000) {
                pps_unit = "(k)"; pps_last /= 1000; pps_average /= 1000;
            }

            int nn_cache = 0;
            int nn_hotspot_size = (int)hotspot.size();
            for (int i = 0; i < nn_hotspot_size; i++) {
                mmsghdr* hdr = &hotspot[i];
                nn_cache += hdr->msg_hdr.msg_iovlen;
            }

            srs_trace("-> RTC SEND #%d, sessions %d, udp %d/%d/%" PRId64 ", gso %d/%d/%" PRId64 ", iovs %d/%d/%" PRId64 ", pps %d/%d%s, cache %d/%d, bytes %d/%" PRId64,
                srs_netfd_fileno(lfd), (int)server->nn_sessions(), pos, nn_msgs_max, nn_msgs, gso_pos, nn_gso_msgs_max, nn_gso_msgs, gso_iovs,
                nn_gso_iovs_max, nn_gso_iovs, pps_average, pps_last, pps_unit.c_str(), (int)hotspot.size(), nn_cache, nn_bytes_max, nn_bytes);
            nn_msgs_last = nn_msgs; time_last = srs_get_system_time();
            nn_loop = nn_wait = nn_msgs_max = 0;
            nn_gso_msgs_max = 0; nn_gso_iovs_max = 0;
            nn_bytes_max = 0;
        }
    }

    return err;
}

srs_error_t SrsUdpMuxSender::on_reload_rtc_server()
{
    if (true) {
        int v = _srs_config->get_rtc_server_sendmmsg();
        if (max_sendmmsg != v) {
            srs_trace("Reload max_sendmmsg %d=>%d", max_sendmmsg, v);
            max_sendmmsg = v;
        }
    }

    return srs_success;
}

SrsRtcServer::SrsRtcServer()
{
    timer = new SrsHourGlass(this, 1 * SRS_UTIME_SECONDS);
}

SrsRtcServer::~SrsRtcServer()
{
    srs_freep(timer);

    if (true) {
        vector<SrsUdpMuxListener*>::iterator it;
        for (it = listeners.begin(); it != listeners.end(); ++it) {
            SrsUdpMuxListener* listener = *it;
            srs_freep(listener);
        }
    }

    if (true) {
        vector<SrsUdpMuxSender*>::iterator it;
        for (it = senders.begin(); it != senders.end(); ++it) {
            SrsUdpMuxSender* sender = *it;
            srs_freep(sender);
        }
    }
}

srs_error_t SrsRtcServer::initialize()
{
    srs_error_t err = srs_success;

    if ((err = timer->tick(1 * SRS_UTIME_SECONDS)) != srs_success) {
        return srs_error_wrap(err, "hourglass tick");
    }

    if ((err = timer->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    srs_trace("RTC server init ok");

    return err;
}

srs_error_t SrsRtcServer::listen_udp()
{
    srs_error_t err = srs_success;

    if (!_srs_config->get_rtc_server_enabled()) {
        return err;
    }

    int port = _srs_config->get_rtc_server_listen();
    if (port <= 0) {
        return srs_error_new(ERROR_RTC_PORT, "invalid port=%d", port);
    }

    string ip = srs_any_address_for_listener();
    srs_assert(listeners.empty());

    int nn_listeners = _srs_config->get_rtc_server_reuseport();
    for (int i = 0; i < nn_listeners; i++) {
        SrsUdpMuxSender* sender = new SrsUdpMuxSender(this);
        SrsUdpMuxListener* listener = new SrsUdpMuxListener(this, sender, ip, port);

        if ((err = listener->listen()) != srs_success) {
            srs_freep(listener);
            return srs_error_wrap(err, "listen %s:%d", ip.c_str(), port);
        }

        if ((err = sender->initialize(listener->stfd(), nn_listeners)) != srs_success) {
            return srs_error_wrap(err, "init sender");
        }

        srs_trace("rtc listen at udp://%s:%d, fd=%d", ip.c_str(), port, listener->fd());
        listeners.push_back(listener);
        senders.push_back(sender);
    }

    return err;
}

srs_error_t SrsRtcServer::on_udp_packet(SrsUdpMuxSocket* skt)
{
    if (is_stun(reinterpret_cast<const uint8_t*>(skt->data()), skt->size())) {
        return on_stun(skt);
    } else if (is_dtls(reinterpret_cast<const uint8_t*>(skt->data()), skt->size())) {
        return on_dtls(skt);
    } else if (is_rtp_or_rtcp(reinterpret_cast<const uint8_t*>(skt->data()), skt->size())) {
        return on_rtp_or_rtcp(skt);
    } 

    return srs_error_new(ERROR_RTC_UDP, "unknown udp packet type");
}

srs_error_t SrsRtcServer::listen_api()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Fetch api from hybrid manager.
    SrsHttpServeMux* http_api_mux = _srs_hybrid->srs()->instance()->api_server();
    if ((err = http_api_mux->handle("/rtc/v1/play/", new SrsGoApiRtcPlay(this))) != srs_success) {
        return srs_error_wrap(err, "handle sdp");
    }

    return err;
}

SrsRtcSession* SrsRtcServer::create_rtc_session(const SrsRequest& req, const SrsSdp& remote_sdp, SrsSdp& local_sdp, const string& mock_eip)
{
    std::string local_pwd = gen_random_str(32);
    std::string local_ufrag = "";
    std::string username = "";
    while (true) {
        local_ufrag = gen_random_str(8);

        username = local_ufrag + ":" + remote_sdp.get_ice_ufrag();
        if (! map_username_session.count(username))
            break;
    }

    int cid = _srs_context->get_id();
    SrsRtcSession* session = new SrsRtcSession(this, req, username, cid);
    map_username_session.insert(make_pair(username, session));

    local_sdp.set_ice_ufrag(local_ufrag);
    local_sdp.set_ice_pwd(local_pwd);
    local_sdp.set_fingerprint_algo("sha-256");
    local_sdp.set_fingerprint(SrsDtls::instance()->get_fingerprint());

    // We allows to mock the eip of server.
    if (!mock_eip.empty()) {
        local_sdp.add_candidate(mock_eip, _srs_config->get_rtc_server_listen(), "host");
    } else {
        std::vector<string> candidate_ips = get_candidate_ips();
        for (int i = 0; i < (int)candidate_ips.size(); ++i) {
            local_sdp.add_candidate(candidate_ips[i], _srs_config->get_rtc_server_listen(), "host");
        }
    }

    session->set_remote_sdp(remote_sdp);
    session->set_local_sdp(local_sdp);

    session->set_session_state(WAITING_STUN);

    return session;
}

SrsRtcSession* SrsRtcServer::find_rtc_session_by_peer_id(const string& peer_id)
{
    map<string, SrsRtcSession*>::iterator iter = map_id_session.find(peer_id);
    if (iter == map_id_session.end()) {
        return NULL; 
    }

    return iter->second;
}

srs_error_t SrsRtcServer::on_stun(SrsUdpMuxSocket* skt)
{
    srs_error_t err = srs_success;

    SrsStunPacket stun_req;
    if ((err = stun_req.decode(skt->data(), skt->size())) != srs_success) {
        return srs_error_wrap(err, "decode stun packet failed");
    }

    srs_verbose("recv stun packet from %s, use-candidate=%d, ice-controlled=%d, ice-controlling=%d", 
        skt->get_peer_id().c_str(), stun_req.get_use_candidate(), stun_req.get_ice_controlled(), stun_req.get_ice_controlling());

    std::string username = stun_req.get_username();
    SrsRtcSession* rtc_session = find_rtc_session_by_username(username);
    if (rtc_session == NULL) {
        return srs_error_new(ERROR_RTC_STUN, "can not find rtc_session, stun username=%s", username.c_str());
    }

    // Now, we got the RTC session to handle the packet, switch to its context
    // to make all logs write to the "correct" pid+cid.
    rtc_session->switch_to_context();

    return rtc_session->on_stun(skt, &stun_req);
}

srs_error_t SrsRtcServer::on_dtls(SrsUdpMuxSocket* skt)
{
    SrsRtcSession* rtc_session = find_rtc_session_by_peer_id(skt->get_peer_id());

    if (rtc_session == NULL) {
        return srs_error_new(ERROR_RTC_DTLS, "can not find rtc session by peer_id=%s", skt->get_peer_id().c_str());
    }

    // Now, we got the RTC session to handle the packet, switch to its context
    // to make all logs write to the "correct" pid+cid.
    rtc_session->switch_to_context();

    return rtc_session->on_dtls(skt);
}

srs_error_t SrsRtcServer::on_rtp_or_rtcp(SrsUdpMuxSocket* skt)
{
    srs_error_t err = srs_success;

    SrsRtcSession* rtc_session = find_rtc_session_by_peer_id(skt->get_peer_id());

    if (rtc_session == NULL) {
        return srs_error_new(ERROR_RTC_RTP, "can not find rtc session by peer_id=%s", skt->get_peer_id().c_str());
    }

    // Now, we got the RTC session to handle the packet, switch to its context
    // to make all logs write to the "correct" pid+cid.
    rtc_session->switch_to_context();

    if (is_rtcp(reinterpret_cast<const uint8_t*>(skt->data()), skt->size())) {
        err = rtc_session->on_rtcp(skt);
    } else {
        // We disable it because no RTP for player.
        // see https://github.com/ossrs/srs/blob/018577e685a07d9de7a47354e7a9c5f77f5f4202/trunk/src/app/srs_app_rtc_conn.cpp#L1081
        // err = rtc_session->on_rtp(skt);
    }

    return err;
}

SrsRtcSession* SrsRtcServer::find_rtc_session_by_username(const std::string& username)
{
    map<string, SrsRtcSession*>::iterator iter = map_username_session.find(username);
    if (iter == map_username_session.end()) {
        return NULL; 
    }

    return iter->second;
}

bool SrsRtcServer::insert_into_id_sessions(const string& peer_id, SrsRtcSession* rtc_session)
{
    return map_id_session.insert(make_pair(peer_id, rtc_session)).second;
}

void SrsRtcServer::check_and_clean_timeout_session()
{
    map<string, SrsRtcSession*>::iterator iter = map_username_session.begin();
    while (iter != map_username_session.end()) {
        SrsRtcSession* session = iter->second;
        if (session == NULL) {
            map_username_session.erase(iter++);
            continue;
        }

        if (session->is_stun_timeout()) {
            // Now, we got the RTC session to cleanup, switch to its context
            // to make all logs write to the "correct" pid+cid.
            session->switch_to_context();

            srs_trace("rtc session=%s, stun timeout", session->id().c_str());
            map_username_session.erase(iter++);
            map_id_session.erase(session->get_peer_id());
            delete session;
            continue;
        }

        ++iter;
    }
}

srs_error_t SrsRtcServer::notify(int type, srs_utime_t interval, srs_utime_t tick)
{
    check_and_clean_timeout_session();
    return srs_success;
}

RtcServerAdapter::RtcServerAdapter()
{
    rtc = new SrsRtcServer();
}

RtcServerAdapter::~RtcServerAdapter()
{
    srs_freep(rtc);
}

srs_error_t RtcServerAdapter::initialize()
{
    srs_error_t err = srs_success;

    if ((err = rtc->initialize()) != srs_success) {
        return srs_error_wrap(err, "rtc server initialize");
    }

    return err;
}

srs_error_t RtcServerAdapter::run()
{
    srs_error_t err = srs_success;

    if ((err = rtc->listen_udp()) != srs_success) {
        return srs_error_wrap(err, "listen udp");
    }

    if ((err = rtc->listen_api()) != srs_success) {
        return srs_error_wrap(err, "listen api");
    }

    return err;
}

void RtcServerAdapter::stop()
{
}

