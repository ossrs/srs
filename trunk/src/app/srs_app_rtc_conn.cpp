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

#include <sstream>

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_rtc_stun_stack.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_config.hpp>
#include <srs_app_rtc_queue.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_service_utility.hpp>
#include <srs_http_stack.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_service_st.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>

// TODO: FIXME: Move to utility.
string gen_random_str(int len)
{
    static string random_table = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

    string ret;
    ret.reserve(len);
    for (int i = 0; i < len; ++i) {
        ret.append(1, random_table[random() % random_table.size()]);
    }

    return ret;
}

uint64_t SrsNtp::kMagicNtpFractionalUnit = 1ULL << 32;

SrsNtp::SrsNtp()
{
    system_ms_ = 0;
    ntp_ = 0;
    ntp_second_ = 0;
    ntp_fractions_ = 0;
}

SrsNtp::~SrsNtp()
{
}

SrsNtp SrsNtp::from_time_ms(uint64_t ms)
{
    SrsNtp srs_ntp;
    srs_ntp.system_ms_ = ms;
    srs_ntp.ntp_second_ = ms / 1000;
    srs_ntp.ntp_fractions_ = (static_cast<double>(ms % 1000 / 1000.0)) * kMagicNtpFractionalUnit;
    srs_ntp.ntp_ = (static_cast<uint64_t>(srs_ntp.ntp_second_) << 32) | srs_ntp.ntp_fractions_;
    return srs_ntp;
}

SrsNtp SrsNtp::to_time_ms(uint64_t ntp)
{
    SrsNtp srs_ntp;
    srs_ntp.ntp_ = ntp;
    srs_ntp.ntp_second_ = (ntp & 0xFFFFFFFF00000000ULL) >> 32;
    srs_ntp.ntp_fractions_ = (ntp & 0x00000000FFFFFFFFULL);
    srs_ntp.system_ms_ = (static_cast<uint64_t>(srs_ntp.ntp_second_) * 1000) + 
        (static_cast<double>(static_cast<uint64_t>(srs_ntp.ntp_fractions_) * 1000.0) / kMagicNtpFractionalUnit);
    return srs_ntp;
}


SrsSecurityTransport::SrsSecurityTransport(SrsRtcSession* s)
{
    session_ = s;

    dtls_ = new SrsDtls((ISrsDtlsCallback*)this);
    srtp_ = new SrsSRTP();

    handshake_done = false;
}

SrsSecurityTransport::~SrsSecurityTransport()
{
    if (dtls_) {
        srs_freep(dtls_);
        dtls_ = NULL;
    }

    if (srtp_) {
        srs_freep(srtp_);
        srtp_ = NULL;
    }
}

srs_error_t SrsSecurityTransport::initialize(SrsSessionConfig* cfg)
{
    return dtls_->initialize(cfg->dtls_role, cfg->dtls_version);
}

srs_error_t SrsSecurityTransport::start_active_handshake()
{
    return dtls_->start_active_handshake();
}

srs_error_t SrsSecurityTransport::write_dtls_data(void* data, int size) 
{
    srs_error_t err = srs_success;
    if (size) {
        if ((err = session_->sendonly_skt->sendto(data, size, 0)) != srs_success) {
            return srs_error_wrap(err, "send dtls packet");
        }
    }

    if (session_->blackhole && session_->blackhole_addr && session_->blackhole_stfd) {
        // Ignore any error for black-hole.
        void* p = data; int len = size; SrsRtcSession* s = session_;
        srs_sendto(s->blackhole_stfd, p, len, (sockaddr*)s->blackhole_addr, sizeof(sockaddr_in), SRS_UTIME_NO_TIMEOUT);
    }

    return err;
}

srs_error_t SrsSecurityTransport::on_dtls(char* data, int nb_data)
{
    return dtls_->on_dtls(data, nb_data);
}

srs_error_t SrsSecurityTransport::on_dtls_handshake_done()
{
    srs_error_t err = srs_success;
    srs_trace("rtc session=%s, DTLS handshake done.", session_->id().c_str());

    handshake_done = true;
    if ((err = srtp_initialize()) != srs_success) {
        return srs_error_wrap(err, "srtp init failed");
    }

    return session_->on_connection_established();
}

srs_error_t SrsSecurityTransport::on_dtls_application_data(const char* buf, const int nb_buf)
{
    srs_error_t err = srs_success;

    // TODO: process SCTP protocol(WebRTC datachannel support)

    return err;
}

srs_error_t SrsSecurityTransport::srtp_initialize()
{
    srs_error_t err = srs_success;

    std::string send_key;
    std::string recv_key;

    if ((err = dtls_->get_srtp_key(recv_key, send_key)) != srs_success) {
        return err;
    }
    
    if ((err = srtp_->initialize(recv_key, send_key)) != srs_success) {
        return srs_error_wrap(err, "srtp init failed");
    }

    return err;
}

srs_error_t SrsSecurityTransport::protect_rtp(const char* plaintext, char* cipher, int& nb_cipher)
{
    if (!srtp_) {
        return srs_error_new(ERROR_RTC_SRTP_PROTECT, "rtp protect failed");
    }

    return srtp_->protect_rtp(plaintext, cipher, nb_cipher);
}

srs_error_t SrsSecurityTransport::protect_rtcp(const char* plaintext, char* cipher, int& nb_cipher)
{
    if (!srtp_) {
        return srs_error_new(ERROR_RTC_SRTP_PROTECT, "rtcp protect failed");
    }

    return srtp_->protect_rtcp(plaintext, cipher, nb_cipher);
}

// TODO: FIXME: Merge with protect_rtp.
srs_error_t SrsSecurityTransport::protect_rtp2(void* rtp_hdr, int* len_ptr)
{
    if (!srtp_) {
        return srs_error_new(ERROR_RTC_SRTP_PROTECT, "rtp protect");
    }

    return srtp_->protect_rtp2(rtp_hdr, len_ptr);
}

srs_error_t SrsSecurityTransport::unprotect_rtp(const char* cipher, char* plaintext, int& nb_plaintext)
{
    if (!srtp_) {
        return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "rtp unprotect failed");
    }
    
    return srtp_->unprotect_rtp(cipher, plaintext, nb_plaintext);
}

srs_error_t SrsSecurityTransport::unprotect_rtcp(const char* cipher, char* plaintext, int& nb_plaintext)
{
    if (!srtp_) {
        return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "rtcp unprotect failed");
    }

    return srtp_->unprotect_rtcp(cipher, plaintext, nb_plaintext);
}

SrsRtcOutgoingInfo::SrsRtcOutgoingInfo()
{
#if defined(SRS_DEBUG)
    debug_id = 0;
#endif

    nn_rtp_pkts = 0;
    nn_audios = nn_extras = 0;
    nn_videos = nn_samples = 0;
    nn_bytes = nn_rtp_bytes = 0;
    nn_padding_bytes = nn_paddings = 0;
}

SrsRtcOutgoingInfo::~SrsRtcOutgoingInfo()
{
}

SrsRtcPlayer::SrsRtcPlayer(SrsRtcSession* s, string parent_cid)
{
    _parent_cid = parent_cid;
    trd = new SrsDummyCoroutine();

    session_ = s;

    mw_msgs = 0;
    realtime = true;

    // TODO: FIXME: Config the capacity?
    audio_queue_ = new SrsRtpRingBuffer(100);
    video_queue_ = new SrsRtpRingBuffer(1000);

    nn_simulate_nack_drop = 0;
    nack_enabled_ = false;

    _srs_config->subscribe(this);
}

SrsRtcPlayer::~SrsRtcPlayer()
{
    _srs_config->unsubscribe(this);

    srs_freep(trd);
    srs_freep(audio_queue_);
    srs_freep(video_queue_);
}

srs_error_t SrsRtcPlayer::initialize(const uint32_t& vssrc, const uint32_t& assrc, const uint16_t& v_pt, const uint16_t& a_pt)
{
    srs_error_t err = srs_success;

    video_ssrc = vssrc;
    audio_ssrc = assrc;

    video_payload_type = v_pt;
    audio_payload_type = a_pt;

    // TODO: FIXME: Support reload.
    nack_enabled_ = _srs_config->get_rtc_nack_enabled(session_->req->vhost);
    srs_trace("RTC player video(ssrc=%d, pt=%d), audio(ssrc=%d, pt=%d), nack=%d",
        video_ssrc, video_payload_type, audio_ssrc, audio_payload_type, nack_enabled_);

    if (_srs_rtc_hijacker) {
        if ((err = _srs_rtc_hijacker->on_start_play(session_, this, session_->req)) != srs_success) {
            return srs_error_wrap(err, "on start play");
        }
    }

    return err;
}

srs_error_t SrsRtcPlayer::on_reload_vhost_play(string vhost)
{
    SrsRequest* req = session_->req;

    if (req->vhost != vhost) {
        return srs_success;
    }

    realtime = _srs_config->get_realtime_enabled(req->vhost, true);
    mw_msgs = _srs_config->get_mw_msgs(req->vhost, realtime, true);

    srs_trace("Reload play realtime=%d, mw_msgs=%d", realtime, mw_msgs);

    return srs_success;
}

srs_error_t SrsRtcPlayer::on_reload_vhost_realtime(string vhost)
{
    return on_reload_vhost_play(vhost);
}

std::string SrsRtcPlayer::cid()
{
    return trd->cid();
}

srs_error_t SrsRtcPlayer::start()
{
    srs_error_t err = srs_success;

    srs_freep(trd);
    trd = new SrsSTCoroutine("rtc_sender", this, _parent_cid);

    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "rtc_sender");
    }

    return err;
}

void SrsRtcPlayer::stop()
{
    trd->stop();
}

void SrsRtcPlayer::stop_loop()
{
    trd->interrupt();
}

srs_error_t SrsRtcPlayer::cycle()
{
    srs_error_t err = srs_success;

    SrsRtcSource* source = NULL;
    SrsRequest* req = session_->req;

    if ((err = _srs_rtc_sources->fetch_or_create(req, &source)) != srs_success) {
        return srs_error_wrap(err, "rtc fetch source failed");
    }

    SrsRtcConsumer* consumer = NULL;
    SrsAutoFree(SrsRtcConsumer, consumer);
    if ((err = source->create_consumer(consumer)) != srs_success) {
        return srs_error_wrap(err, "rtc create consumer, source url=%s", req->get_stream_url().c_str());
    }

    // TODO: FIXME: Dumps the SPS/PPS from gop cache, without other frames.
    if ((err = source->consumer_dumps(consumer)) != srs_success) {
        return srs_error_wrap(err, "dumps consumer, source url=%s", req->get_stream_url().c_str());
    }

    realtime = _srs_config->get_realtime_enabled(req->vhost, true);
    mw_msgs = _srs_config->get_mw_msgs(req->vhost, realtime, true);

    srs_trace("RTC source url=%s, source_id=[%d][%s], encrypt=%d, realtime=%d, mw_msgs=%d", req->get_stream_url().c_str(),
        ::getpid(), source->source_id().c_str(), session_->encrypt, realtime, mw_msgs);

    SrsPithyPrint* pprint = SrsPithyPrint::create_rtc_play();
    SrsAutoFree(SrsPithyPrint, pprint);

    srs_trace("rtc session=%s, start play", session_->id().c_str());
    bool stat_enabled = _srs_config->get_rtc_server_perf_stat();
    SrsStatistic* stat = SrsStatistic::instance();

    // TODO: FIXME: Use cache for performance?
    vector<SrsRtpPacket2*> pkts;
    SrsRtcOutgoingInfo info;

    if (_srs_rtc_hijacker) {
        if ((err = _srs_rtc_hijacker->on_start_consume(session_, this, session_->req, consumer)) != srs_success) {
            return srs_error_wrap(err, "on start consuming");
        }
    }

    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "rtc sender thread");
        }

        // Wait for amount of packets.
        consumer->wait(mw_msgs);

        // TODO: FIXME: Handle error.
        consumer->dump_packets(pkts);

        int msg_count = (int)pkts.size();
        if (!msg_count) {
            continue;
        }

        // Send-out all RTP packets and do cleanup.
        // TODO: FIXME: Handle error.
        send_packets(source, pkts, info);

        for (int i = 0; i < msg_count; i++) {
            SrsRtpPacket2* pkt = pkts[i];
            srs_freep(pkt);
        }
        pkts.clear();

        // Stat for performance analysis.
        if (!stat_enabled) {
            continue;
        }

        // Stat the original RAW AV frame, maybe h264+aac.
        stat->perf_on_msgs(msg_count);
        // Stat the RTC packets, RAW AV frame, maybe h.264+opus.
        int nn_rtc_packets = srs_max(info.nn_audios, info.nn_extras) + info.nn_videos;
        stat->perf_on_rtc_packets(nn_rtc_packets);
        // Stat the RAW RTP packets, which maybe group by GSO.
        stat->perf_on_rtp_packets(msg_count);
        // Stat the bytes and paddings.
        stat->perf_on_rtc_bytes(info.nn_bytes, info.nn_rtp_bytes, info.nn_padding_bytes);

        pprint->elapse();
        if (pprint->can_print()) {
            // TODO: FIXME: Print stat like frame/s, packet/s, loss_packets.
            srs_trace("-> RTC PLAY %d msgs, %d/%d packets, %d audios, %d extras, %d videos, %d samples, %d/%d/%d bytes, %d pad, %d/%d cache",
                msg_count, msg_count, info.nn_rtp_pkts, info.nn_audios, info.nn_extras, info.nn_videos, info.nn_samples, info.nn_bytes,
                info.nn_rtp_bytes, info.nn_padding_bytes, info.nn_paddings, msg_count, msg_count);
        }
    }
}

srs_error_t SrsRtcPlayer::send_packets(SrsRtcSource* source, const vector<SrsRtpPacket2*>& pkts, SrsRtcOutgoingInfo& info)
{
    srs_error_t err = srs_success;

    // If DTLS is not OK, drop all messages.
    if (!session_->transport_) {
        return err;
    }

    // Covert kernel messages to RTP packets.
    for (int i = 0; i < (int)pkts.size(); i++) {
        SrsRtpPacket2* pkt = pkts[i];

        // Update stats.
        info.nn_bytes += pkt->nb_bytes();

        if (pkt->is_audio()) {
            info.nn_audios++;
            pkt->header.set_ssrc(audio_ssrc);
            pkt->header.set_payload_type(audio_payload_type);

            // TODO: FIXME: Padding audio to the max payload in RTP packets.
        } else {
            info.nn_videos++;
            pkt->header.set_ssrc(video_ssrc);
            pkt->header.set_payload_type(video_payload_type);
        }

        // Detail log, should disable it in release version.
        srs_info("RTC: Update PT=%u, SSRC=%#x, Time=%u, %u bytes", pkt->header.get_payload_type(), pkt->header.get_ssrc(),
            pkt->header.get_timestamp(), pkt->nb_bytes());
    }

    // By default, we send packets by sendmmsg.
    if ((err = do_send_packets(pkts, info)) != srs_success) {
        return srs_error_wrap(err, "raw send");
    }

    return err;
}

srs_error_t SrsRtcPlayer::do_send_packets(const std::vector<SrsRtpPacket2*>& pkts, SrsRtcOutgoingInfo& info)
{
    srs_error_t err = srs_success;

    // Cache the encrypt flag and sender.
    bool encrypt = session_->encrypt;

    for (int i = 0; i < (int)pkts.size(); i++) {
        SrsRtpPacket2* pkt = pkts.at(i);

        // For this message, select the first iovec.
        iovec* iov = new iovec();
        SrsAutoFree(iovec, iov);

        char* iov_base = new char[kRtpPacketSize];
        SrsAutoFreeA(char, iov_base);

        iov->iov_base = iov_base;
        iov->iov_len = kRtpPacketSize;

        // Marshal packet to bytes in iovec.
        if (true) {
            SrsBuffer stream((char*)iov->iov_base, iov->iov_len);
            if ((err = pkt->encode(&stream)) != srs_success) {
                return srs_error_wrap(err, "encode packet");
            }
            iov->iov_len = stream.pos();
        }

        // Whether encrypt the RTP bytes.
        if (encrypt) {
            int nn_encrypt = (int)iov->iov_len;
            if ((err = session_->transport_->protect_rtp2(iov->iov_base, &nn_encrypt)) != srs_success) {
                return srs_error_wrap(err, "srtp protect");
            }
            iov->iov_len = (size_t)nn_encrypt;
        }

        // Put final RTP packet to NACK/ARQ queue.
        if (nack_enabled_) {
            SrsRtpPacket2* nack = new SrsRtpPacket2();
            nack->header = pkt->header;

            // TODO: FIXME: Should avoid memory copying.
            SrsRtpRawPayload* payload = new SrsRtpRawPayload();
            nack->payload = payload;

            payload->nn_payload = (int)iov->iov_len;
            payload->payload = new char[payload->nn_payload];
            memcpy((void*)payload->payload, iov->iov_base, iov->iov_len);

            nack->shared_msg = new SrsSharedPtrMessage();
            nack->shared_msg->wrap(payload->payload, payload->nn_payload);

            if (nack->header.get_ssrc() == video_ssrc) {
                video_queue_->set(nack->header.get_sequence(), nack);
            } else {
                audio_queue_->set(nack->header.get_sequence(), nack);
            }
        }

        info.nn_rtp_bytes += (int)iov->iov_len;

        // When we send out a packet, increase the stat counter.
        info.nn_rtp_pkts++;

        // For NACK simulator, drop packet.
        if (nn_simulate_nack_drop) {
            simulate_drop_packet(&pkt->header, (int)iov->iov_len);
            iov->iov_len = 0;
            continue;
        }

        // TODO: FIXME: Handle error.
        session_->sendonly_skt->sendto(iov->iov_base, iov->iov_len, 0);

        // Detail log, should disable it in release version.
        srs_info("RTC: SEND PT=%u, SSRC=%#x, SEQ=%u, Time=%u, %u/%u bytes", pkt->header.get_payload_type(), pkt->header.get_ssrc(),
            pkt->header.get_sequence(), pkt->header.get_timestamp(), pkt->nb_bytes(), iov->iov_len);
    }

    return err;
}

void SrsRtcPlayer::nack_fetch(vector<SrsRtpPacket2*>& pkts, uint32_t ssrc, uint16_t seq)
{
    SrsRtpPacket2* pkt = NULL;

    if (ssrc == video_ssrc) {
        pkt = video_queue_->at(seq);
    } else if (ssrc == audio_ssrc) {
        pkt = audio_queue_->at(seq);
    }

    if (pkt) {
        pkts.push_back(pkt);
    }
}

void SrsRtcPlayer::simulate_nack_drop(int nn)
{
    nn_simulate_nack_drop = nn;
}

void SrsRtcPlayer::simulate_drop_packet(SrsRtpHeader* h, int nn_bytes)
{
    srs_warn("RTC NACK simulator #%d drop seq=%u, ssrc=%u/%s, ts=%u, %d bytes", nn_simulate_nack_drop,
        h->get_sequence(), h->get_ssrc(), (h->get_ssrc()==video_ssrc? "Video":"Audio"), h->get_timestamp(),
        nn_bytes);

    nn_simulate_nack_drop--;
}

srs_error_t SrsRtcPlayer::on_rtcp(char* data, int nb_data)
{
    srs_error_t err = srs_success;

    char* ph = data;
    int nb_left = nb_data;
    while (nb_left) {
        uint8_t payload_type = ph[1];
        uint16_t length_4bytes = (((uint16_t)ph[2]) << 8) | ph[3];

        int length = (length_4bytes + 1) * 4;

        if (length > nb_data) {
            return srs_error_new(ERROR_RTC_RTCP, "invalid rtcp packet, length=%u", length);
        }

        srs_verbose("on rtcp, payload_type=%u", payload_type);

        switch (payload_type) {
            case kSR: {
                err = on_rtcp_sr(ph, length);
                break;
            }
            case kRR: {
                err = on_rtcp_rr(ph, length);
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
                err = on_rtcp_feedback(ph, length);
                break;
            }
            case kPsFb: {
                err = on_rtcp_ps_feedback(ph, length);
                break;
            }
            case kXR: {
                err = on_rtcp_xr(ph, length);
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

srs_error_t SrsRtcPlayer::on_rtcp_sr(char* buf, int nb_buf)
{
    srs_error_t err = srs_success;
    // TODO: FIXME: Implements it.
    return err;
}

srs_error_t SrsRtcPlayer::on_rtcp_xr(char* buf, int nb_buf)
{
    srs_error_t err = srs_success;
    // TODO: FIXME: Implements it.
    return err;
}

srs_error_t SrsRtcPlayer::on_rtcp_feedback(char* buf, int nb_buf)
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
    uint32_t ssrc_of_media_source = stream->read_4bytes();

    /*
         0                   1                   2                   3
         0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |            PID                |             BLP               |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */

    uint16_t pid = stream->read_2bytes();
    int blp = stream->read_2bytes();

    // TODO: FIXME: Support ARQ.
    vector<SrsRtpPacket2*> resend_pkts;
    nack_fetch(resend_pkts, ssrc_of_media_source, pid);

    // If NACK disabled, print a log.
    if (!nack_enabled_) {
        srs_trace("RTC NACK seq=%u, ignored", pid);
        return err;
    }

    uint16_t mask = 0x01;
    for (int i = 1; i < 16 && blp; ++i, mask <<= 1) {
        if (!(blp & mask)) {
            continue;
        }

        uint32_t loss_seq = pid + i;
        nack_fetch(resend_pkts, ssrc_of_media_source, loss_seq);
    }

    for (int i = 0; i < (int)resend_pkts.size(); ++i) {
        SrsRtpPacket2* pkt = resend_pkts[i];

        char* data = new char[pkt->nb_bytes()];
        SrsAutoFreeA(char, data);

        SrsBuffer buf(data, pkt->nb_bytes());

        // TODO: FIXME: Check error.
        pkt->encode(&buf);
        session_->sendonly_skt->sendto(data, pkt->nb_bytes(), 0);

        SrsRtpHeader* h = &pkt->header;
        srs_trace("RTC NACK ARQ seq=%u, ssrc=%u, ts=%u, %d bytes", h->get_sequence(),
            h->get_ssrc(), h->get_timestamp(), pkt->nb_bytes());
    }

    return err;
}

srs_error_t SrsRtcPlayer::on_rtcp_ps_feedback(char* buf, int nb_buf)
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

    /*uint8_t payload_type = */stream->read_1bytes();
    /*uint16_t length = */stream->read_2bytes();
    /*uint32_t ssrc_of_sender = */stream->read_4bytes();
    /*uint32_t ssrc_of_media_source = */stream->read_4bytes();

    switch (fmt) {
        case kPLI: {
            ISrsRtcPublisher* publisher = session_->source_->rtc_publisher();
            if (publisher) {
                publisher->request_keyframe();
                srs_trace("RTC request PLI");
            }
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

srs_error_t SrsRtcPlayer::on_rtcp_rr(char* data, int nb_data)
{
    srs_error_t err = srs_success;
    // TODO: FIXME: Implements it.
    return err;
}

SrsRtcPublisher::SrsRtcPublisher(SrsRtcSession* session)
{
    report_timer = new SrsHourGlass(this, 200 * SRS_UTIME_MILLISECONDS);

    session_ = session;
    request_keyframe_ = false;
    video_queue_ = new SrsRtpRingBuffer(1000);
    video_nack_ = new SrsRtpNackForReceiver(video_queue_, 1000 * 2 / 3);
    audio_queue_ = new SrsRtpRingBuffer(100);
    audio_nack_ = new SrsRtpNackForReceiver(audio_queue_, 100 * 2 / 3);

    source = NULL;
    nn_simulate_nack_drop = 0;
    nack_enabled_ = false;

    nn_audio_frames = 0;
    twcc_ext_id_ = 0;
    last_twcc_feedback_time_ = 0;
    twcc_fb_count_ = 0;
}

SrsRtcPublisher::~SrsRtcPublisher()
{
    // TODO: FIXME: Do unpublish when session timeout.
    if (source) {
        source->set_rtc_publisher(NULL);
        source->on_unpublish();
    }

    srs_freep(report_timer);
    srs_freep(video_nack_);
    srs_freep(video_queue_);
    srs_freep(audio_nack_);
    srs_freep(audio_queue_);
}

srs_error_t SrsRtcPublisher::initialize(uint32_t vssrc, uint32_t assrc, uint8_t twcc_ext_id, SrsRequest* r)
{
    srs_error_t err = srs_success;

    video_ssrc = vssrc;
    audio_ssrc = assrc;
    twcc_ext_id_ = twcc_ext_id;
    rtcp_twcc_.set_media_ssrc(video_ssrc);
    req = r;

    if (twcc_ext_id_ != 0) {
        extension_types_.register_by_uri(twcc_ext_id_, kTWCCExt);
    }
    // TODO: FIXME: Support reload.
    nack_enabled_ = _srs_config->get_rtc_nack_enabled(session_->req->vhost);

    srs_trace("RTC publisher video(ssrc=%u), audio(ssrc=%u), nack=%d",
        video_ssrc, audio_ssrc, nack_enabled_);

    if ((err = report_timer->tick(0 * SRS_UTIME_MILLISECONDS)) != srs_success) {
        return srs_error_wrap(err, "hourglass tick");
    }

    if ((err = report_timer->start()) != srs_success) {
        return srs_error_wrap(err, "start report_timer");
    }

    if ((err = _srs_rtc_sources->fetch_or_create(req, &source)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    if ((err = source->on_publish()) != srs_success) {
        return srs_error_wrap(err, "on publish");
    }

    source->set_rtc_publisher(this);

    if (_srs_rtc_hijacker) {
        if ((err = _srs_rtc_hijacker->on_start_publish(session_, this, req)) != srs_success) {
            return srs_error_wrap(err, "on start publish");
        }
    }

    return err;
}

void SrsRtcPublisher::check_send_nacks(SrsRtpNackForReceiver* nack, uint32_t ssrc)
{
    // If DTLS is not OK, drop all messages.
    if (!session_->transport_) {
        return;
    }

    // @see: https://tools.ietf.org/html/rfc4585#section-6.1
    vector<uint16_t> nack_seqs;
    nack->get_nack_seqs(nack_seqs);
    
    vector<uint16_t>::iterator iter = nack_seqs.begin();
    while (iter != nack_seqs.end()) {
        char buf[kRtpPacketSize];
        SrsBuffer stream(buf, sizeof(buf));
        // FIXME: Replace magic number.
        stream.write_1bytes(0x81);
        stream.write_1bytes(kRtpFb);
        stream.write_2bytes(3);
        stream.write_4bytes(ssrc); // TODO: FIXME: Should be 1?
        stream.write_4bytes(ssrc); // TODO: FIXME: Should be 0?
        uint16_t pid = *iter;
        uint16_t blp = 0;
        while (iter + 1 != nack_seqs.end() && (*(iter + 1) - pid <= 15)) {
            blp |= (1 << (*(iter + 1) - pid - 1));
            ++iter;
        }

        stream.write_2bytes(pid);
        stream.write_2bytes(blp);

        if (session_->blackhole && session_->blackhole_addr && session_->blackhole_stfd) {
            // Ignore any error for black-hole.
            void* p = stream.data(); int len = stream.pos(); SrsRtcSession* s = session_;
            srs_sendto(s->blackhole_stfd, p, len, (sockaddr*)s->blackhole_addr, sizeof(sockaddr_in), SRS_UTIME_NO_TIMEOUT);
        }

        char protected_buf[kRtpPacketSize];
        int nb_protected_buf = stream.pos();

        // FIXME: Merge nack rtcp into one packets.
        if (session_->transport_->protect_rtcp(protected_buf, stream.data(), nb_protected_buf) == srs_success) {
            // TODO: FIXME: Check error.
            session_->sendonly_skt->sendto(protected_buf, nb_protected_buf, 0);
        }

        ++iter;
    }
}

srs_error_t SrsRtcPublisher::send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer* rtp_queue)
{
    srs_error_t err = srs_success;

    // If DTLS is not OK, drop all messages.
    if (!session_->transport_) {
        return err;
    }

    // @see https://tools.ietf.org/html/rfc3550#section-6.4.2
    char buf[kRtpPacketSize];
    SrsBuffer stream(buf, sizeof(buf));
    stream.write_1bytes(0x81);
    stream.write_1bytes(kRR);
    stream.write_2bytes(7);
    stream.write_4bytes(ssrc); // TODO: FIXME: Should be 1?

    // TODO: FIXME: Implements it.
    // TODO: FIXME: See https://github.com/ossrs/srs/blob/f81d35d20f04ebec01915cb78a882e45b7ee8800/trunk/src/app/srs_app_rtc_queue.cpp
    uint8_t fraction_lost = 0;
    uint32_t cumulative_number_of_packets_lost = 0 & 0x7FFFFF;
    uint32_t extended_highest_sequence = rtp_queue->get_extended_highest_sequence();
    uint32_t interarrival_jitter = 0;

    uint32_t rr_lsr = 0;
    uint32_t rr_dlsr = 0;

    const uint64_t& lsr_systime = last_sender_report_sys_time[ssrc];
    const SrsNtp& lsr_ntp = last_sender_report_ntp[ssrc];

    if (lsr_systime > 0) {
        rr_lsr = (lsr_ntp.ntp_second_ << 16) | (lsr_ntp.ntp_fractions_ >> 16);
        uint32_t dlsr = (srs_update_system_time() - lsr_systime) / 1000;
        rr_dlsr = ((dlsr / 1000) << 16) | ((dlsr % 1000) * 65536 / 1000);
    }

    stream.write_4bytes(ssrc);
    stream.write_1bytes(fraction_lost);
    stream.write_3bytes(cumulative_number_of_packets_lost);
    stream.write_4bytes(extended_highest_sequence);
    stream.write_4bytes(interarrival_jitter);
    stream.write_4bytes(rr_lsr);
    stream.write_4bytes(rr_dlsr);

    srs_verbose("RR ssrc=%u, fraction_lost=%u, cumulative_number_of_packets_lost=%u, extended_highest_sequence=%u, interarrival_jitter=%u",
        ssrc, fraction_lost, cumulative_number_of_packets_lost, extended_highest_sequence, interarrival_jitter);

    char protected_buf[kRtpPacketSize];
    int nb_protected_buf = stream.pos();
    if ((err = session_->transport_->protect_rtcp(stream.data(), protected_buf, nb_protected_buf)) != srs_success) {
        return srs_error_wrap(err, "protect rtcp rr");
    }

    // TDOO: FIXME: Check error.
    session_->sendonly_skt->sendto(protected_buf, nb_protected_buf, 0);
    return err;
}

srs_error_t SrsRtcPublisher::send_rtcp_xr_rrtr(uint32_t ssrc)
{
    srs_error_t err = srs_success;

    // If DTLS is not OK, drop all messages.
    if (!session_->transport_) {
        return err;
    }

    /*
     @see: http://www.rfc-editor.org/rfc/rfc3611.html#section-2

      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |V=2|P|reserved |   PT=XR=207   |             length            |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                              SSRC                             |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     :                         report blocks                         :
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

     @see: http://www.rfc-editor.org/rfc/rfc3611.html#section-4.4

      0                   1                   2                   3
         0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |     BT=4      |   reserved    |       block length = 2        |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |              NTP timestamp, most significant word             |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |             NTP timestamp, least significant word             |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    srs_utime_t now = srs_update_system_time();
    SrsNtp cur_ntp = SrsNtp::from_time_ms(now / 1000);

    char buf[kRtpPacketSize];
    SrsBuffer stream(buf, sizeof(buf));
    stream.write_1bytes(0x80);
    stream.write_1bytes(kXR);
    stream.write_2bytes(4);
    stream.write_4bytes(ssrc);
    stream.write_1bytes(4);
    stream.write_1bytes(0);
    stream.write_2bytes(2);
    stream.write_4bytes(cur_ntp.ntp_second_);
    stream.write_4bytes(cur_ntp.ntp_fractions_);

    char protected_buf[kRtpPacketSize];
    int nb_protected_buf = stream.pos();
    if ((err = session_->transport_->protect_rtcp(stream.data(), protected_buf, nb_protected_buf)) != srs_success) {
        return srs_error_wrap(err, "protect rtcp xr");
    }

    // TDOO: FIXME: Check error.
    session_->sendonly_skt->sendto(protected_buf, nb_protected_buf, 0);

    return err;
}

srs_error_t SrsRtcPublisher::send_rtcp_fb_pli(uint32_t ssrc)
{
    srs_error_t err = srs_success;

    // If DTLS is not OK, drop all messages.
    if (!session_->transport_) {
        return err;
    }

    char buf[kRtpPacketSize];
    SrsBuffer stream(buf, sizeof(buf));
    stream.write_1bytes(0x81);
    stream.write_1bytes(kPsFb);
    stream.write_2bytes(2);
    stream.write_4bytes(ssrc);
    stream.write_4bytes(ssrc);

    srs_trace("RTC PLI ssrc=%u", ssrc);

    if (session_->blackhole && session_->blackhole_addr && session_->blackhole_stfd) {
        // Ignore any error for black-hole.
        void* p = stream.data(); int len = stream.pos(); SrsRtcSession* s = session_;
        srs_sendto(s->blackhole_stfd, p, len, (sockaddr*)s->blackhole_addr, sizeof(sockaddr_in), SRS_UTIME_NO_TIMEOUT);
    }

    char protected_buf[kRtpPacketSize];
    int nb_protected_buf = stream.pos();
    if ((err = session_->transport_->protect_rtcp(stream.data(), protected_buf, nb_protected_buf)) != srs_success) {
        return srs_error_wrap(err, "protect rtcp psfb pli");
    }

    // TDOO: FIXME: Check error.
    session_->sendonly_skt->sendto(protected_buf, nb_protected_buf, 0);

    return err;
}

srs_error_t SrsRtcPublisher::on_twcc(uint16_t sn) {
    srs_utime_t now = srs_get_system_time();
    return rtcp_twcc_.recv_packet(sn, now);
}
srs_error_t SrsRtcPublisher::on_rtp(char* data, int nb_data)
{
    srs_error_t err = srs_success;

    // For NACK simulator, drop packet.
    if (nn_simulate_nack_drop) {
        SrsBuffer b(data, nb_data); SrsRtpHeader h; h.decode(&b);
        simulate_drop_packet(&h, nb_data);
        return err;
    }

    // We must parse the TWCC from RTP header before SRTP unprotect, because:
    //      1. Client may send some padding packets with invalid SequenceNumber, which causes the SRTP fail.
    //      2. Server may send multiple duplicated NACK to client, and got more than one ARQ packet, which also fail SRTP.
    // so, we must parse the header before SRTP unprotect(which may fail and drop packet).
    if (0 != twcc_ext_id_) {
        SrsBuffer b(data, nb_data); SrsRtpHeader h;
        h.ignore_padding(true);
        h.set_extensions(&extension_types_);
        if ((err = h.decode(&b)) != srs_success) {
            return srs_error_wrap(err, "twcc decode header");
        }

        uint16_t twcc_sn = 0;
        if ((err = h.get_twcc_sequence_number(twcc_sn)) == srs_success) {
            if((err = on_twcc(twcc_sn)) != srs_success) {
                return srs_error_wrap(err, "on twcc");
            }
        } else {
            srs_error_reset(err);
        }
    }

    // Decrypt the cipher to plaintext RTP data.
    int nb_unprotected_buf = nb_data;
    char* unprotected_buf = new char[kRtpPacketSize];
    if ((err = session_->transport_->unprotect_rtp(data, unprotected_buf, nb_unprotected_buf)) != srs_success) {
        // We try to decode the RTP header for more detail error informations.
        SrsBuffer b(data, nb_data); SrsRtpHeader h; h.decode(&b);
        err = srs_error_wrap(err, "marker=%u, pt=%u, seq=%u, ts=%u, ssrc=%u, pad=%u, payload=%uB", h.get_marker(), h.get_payload_type(),
            h.get_sequence(), h.get_timestamp(), h.get_ssrc(), h.get_padding(), nb_data - b.pos());

        srs_freepa(unprotected_buf);
        return err;
    }

    if (session_->blackhole && session_->blackhole_addr && session_->blackhole_stfd) {
        // Ignore any error for black-hole.
        void* p = unprotected_buf; int len = nb_unprotected_buf; SrsRtcSession* s = session_;
        srs_sendto(s->blackhole_stfd, p, len, (sockaddr*)s->blackhole_addr, sizeof(sockaddr_in), SRS_UTIME_NO_TIMEOUT);
    }

    char* buf = unprotected_buf;
    int nb_buf = nb_unprotected_buf;

    // Decode the RTP packet from buffer.
    SrsRtpPacket2* pkt = new SrsRtpPacket2();
    SrsAutoFree(SrsRtpPacket2, pkt);

    if (true) {
        pkt->set_decode_handler(this);
        pkt->set_extension_types(&extension_types_);
        pkt->shared_msg = new SrsSharedPtrMessage();
        pkt->shared_msg->wrap(buf, nb_buf);

        SrsBuffer b(buf, nb_buf);
        if ((err = pkt->decode(&b)) != srs_success) {
            return srs_error_wrap(err, "decode rtp packet");
        }
    }

    // For source to consume packet.
    uint32_t ssrc = pkt->header.get_ssrc();
    if (ssrc == audio_ssrc) {
        pkt->frame_type = SrsFrameTypeAudio;
        if ((err = on_audio(pkt)) != srs_success) {
            return srs_error_wrap(err, "on audio");
        }
    } else if (ssrc == video_ssrc) {
        pkt->frame_type = SrsFrameTypeVideo;
        if ((err = on_video(pkt)) != srs_success) {
            return srs_error_wrap(err, "on video");
        }
    } else {
        return srs_error_new(ERROR_RTC_RTP, "unknown ssrc=%u", ssrc);
    }

    // For NACK to handle packet.
    if (nack_enabled_ && (err = on_nack(pkt)) != srs_success) {
        return srs_error_wrap(err, "on nack");
    }

    if (_srs_rtc_hijacker) {
        if ((err = _srs_rtc_hijacker->on_rtp_packet(session_, this, req, pkt->copy())) != srs_success) {
            return srs_error_wrap(err, "on rtp packet");
        }
    }

    return err;
}

void SrsRtcPublisher::on_before_decode_payload(SrsRtpPacket2* pkt, SrsBuffer* buf, ISrsRtpPayloader** ppayload)
{
    // No payload, ignore.
    if (buf->empty()) {
        return;
    }

    uint32_t ssrc = pkt->header.get_ssrc();
    if (ssrc == audio_ssrc) {
        *ppayload = new SrsRtpRawPayload();
    } else if (ssrc == video_ssrc) {
        uint8_t v = (uint8_t)pkt->nalu_type;
        if (v == kStapA) {
            *ppayload = new SrsRtpSTAPPayload();
        } else if (v == kFuA) {
            *ppayload = new SrsRtpFUAPayload2();
        } else {
            *ppayload = new SrsRtpRawPayload();
        }
    }
}

srs_error_t SrsRtcPublisher::on_audio(SrsRtpPacket2* pkt)
{
    srs_error_t err = srs_success;

    pkt->frame_type = SrsFrameTypeAudio;

    // TODO: FIXME: Error check.
    source->on_rtp(pkt);

    return err;
}

srs_error_t SrsRtcPublisher::on_video(SrsRtpPacket2* pkt)
{
    srs_error_t err = srs_success;

    pkt->frame_type = SrsFrameTypeVideo;

    // TODO: FIXME: Error check.
    source->on_rtp(pkt);

    if (request_keyframe_) {
        request_keyframe_ = false;

        // TODO: FIXME: Check error.
        send_rtcp_fb_pli(video_ssrc);
    }

    return err;
}

srs_error_t SrsRtcPublisher::on_nack(SrsRtpPacket2* pkt)
{
    srs_error_t err = srs_success;
    
    SrsRtpNackForReceiver* nack_receiver = audio_nack_;
    SrsRtpRingBuffer* ring_queue = audio_queue_;
    
    // TODO: FIXME: use is_audio() to jugdement
    uint32_t ssrc = pkt->header.get_ssrc();
    uint16_t seq = pkt->header.get_sequence();
    bool video = (ssrc == video_ssrc) ? true : false;
    if (video) {
        nack_receiver = video_nack_; 
        ring_queue = video_queue_;
    }

    // TODO: check whether is necessary?
    nack_receiver->remove_timeout_packets();

    SrsRtpNackInfo* nack_info = nack_receiver->find(seq);
    if (nack_info) {
        // seq had been received.
        nack_receiver->remove(seq);
        return err;
    }

    // insert check nack list
    uint16_t nack_first = 0, nack_last = 0;
    if (!ring_queue->update(seq, nack_first, nack_last)) {
        srs_warn("too old seq %u, range [%u, %u]", seq, ring_queue->begin, ring_queue->end);
    }
    if (srs_rtp_seq_distance(nack_first, nack_last) > 0) {
        srs_trace("update seq=%u, nack range [%u, %u]", seq, nack_first, nack_last);
        nack_receiver->insert(nack_first, nack_last);
        nack_receiver->check_queue_size();
    }
    
    // insert into video_queue and audio_queue
    ring_queue->set(seq, pkt->copy());
    // send_nack
    check_send_nacks(nack_receiver, ssrc);

    return err;
}

srs_error_t SrsRtcPublisher::send_periodic_twcc()
{
    srs_error_t err = srs_success;
    srs_utime_t now = srs_get_system_time();
    if(0 == last_twcc_feedback_time_) {
        last_twcc_feedback_time_ = now;
        return err;
    }
    srs_utime_t diff = now - last_twcc_feedback_time_;
    if( diff >= 50 * SRS_UTIME_MILLISECONDS) {
        last_twcc_feedback_time_ = now;
        char pkt[kRtcpPacketSize];
        SrsBuffer *buffer = new SrsBuffer(pkt, sizeof(pkt));
        SrsAutoFree(SrsBuffer, buffer);
        rtcp_twcc_.set_feedback_count(twcc_fb_count_);
        twcc_fb_count_++;
        if((err = rtcp_twcc_.encode(buffer)) != srs_success) {
            return srs_error_wrap(err, "fail to generate twcc feedback packet");
        }
        int nb_protected_buf = buffer->pos();
        char protected_buf[kRtpPacketSize];
        if (session_->transport_->protect_rtcp(pkt, protected_buf, nb_protected_buf) == srs_success) {
            session_->sendonly_skt->sendto(protected_buf, nb_protected_buf, 0);
        }
    }

    return err;
}

srs_error_t SrsRtcPublisher::on_rtcp(char* data, int nb_data)
{
    srs_error_t err = srs_success;

    char* ph = data;
    int nb_left = nb_data;
    while (nb_left) {
        uint8_t payload_type = ph[1];
        uint16_t length_4bytes = (((uint16_t)ph[2]) << 8) | ph[3];

        int length = (length_4bytes + 1) * 4;

        if (length > nb_data) {
            return srs_error_new(ERROR_RTC_RTCP, "invalid rtcp packet, length=%u", length);
        }

        srs_verbose("on rtcp, payload_type=%u", payload_type);

        switch (payload_type) {
            case kSR: {
                err = on_rtcp_sr(ph, length);
                break;
            }
            case kRR: {
                err = on_rtcp_rr(ph, length);
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
                err = on_rtcp_feedback(ph, length);
                break;
            }
            case kPsFb: {
                err = on_rtcp_ps_feedback(ph, length);
                break;
            }
            case kXR: {
                err = on_rtcp_xr(ph, length);
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

srs_error_t SrsRtcPublisher::on_rtcp_sr(char* buf, int nb_buf)
{
    srs_error_t err = srs_success;

    if (nb_buf < 28) {
        return srs_error_new(ERROR_RTC_RTCP_CHECK, "invalid rtp sender report packet, nb_buf=%d", nb_buf);
    }

    SrsBuffer* stream = new SrsBuffer(buf, nb_buf);
    SrsAutoFree(SrsBuffer, stream);

    // @see: https://tools.ietf.org/html/rfc3550#section-6.4.1
    /*
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
header |V=2|P|    RC   |   PT=SR=200   |             length            |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         SSRC of sender                        |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
sender |              NTP timestamp, most significant word             |
info   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |             NTP timestamp, least significant word             |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         RTP timestamp                         |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                     sender's packet count                     |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                      sender's octet count                     |
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
    uint8_t rc = first & 0x1F;

    uint8_t payload_type = stream->read_1bytes();
    srs_assert(payload_type == kSR);
    uint16_t length = stream->read_2bytes();

    if (((length + 1) * 4) != (rc * 24 + 28)) {
        return srs_error_new(ERROR_RTC_RTCP_CHECK, "invalid rtcp sender report packet, length=%u, rc=%u", length, rc);
    }

    uint32_t ssrc_of_sender = stream->read_4bytes();
    uint64_t ntp = stream->read_8bytes();
    SrsNtp srs_ntp = SrsNtp::to_time_ms(ntp);
    uint32_t rtp_time = stream->read_4bytes();
    uint32_t sender_packet_count = stream->read_4bytes();
    uint32_t sender_octec_count = stream->read_4bytes();

    (void)sender_packet_count; (void)sender_octec_count; (void)rtp_time;
    srs_verbose("sender report, ssrc_of_sender=%u, rtp_time=%u, sender_packet_count=%u, sender_octec_count=%u",
        ssrc_of_sender, rtp_time, sender_packet_count, sender_octec_count);

    for (int i = 0; i < rc; ++i) {
        uint32_t ssrc = stream->read_4bytes();
        uint8_t fraction_lost = stream->read_1bytes();
        uint32_t cumulative_number_of_packets_lost = stream->read_3bytes();
        uint32_t highest_seq = stream->read_4bytes();
        uint32_t jitter = stream->read_4bytes();
        uint32_t lst = stream->read_4bytes();
        uint32_t dlsr = stream->read_4bytes();

        (void)ssrc; (void)fraction_lost; (void)cumulative_number_of_packets_lost; (void)highest_seq; (void)jitter; (void)lst; (void)dlsr;
        srs_verbose("sender report, ssrc=%u, fraction_lost=%u, cumulative_number_of_packets_lost=%u, highest_seq=%u, jitter=%u, lst=%u, dlst=%u",
            ssrc, fraction_lost, cumulative_number_of_packets_lost, highest_seq, jitter, lst, dlsr);
    }

    last_sender_report_ntp[ssrc_of_sender] = srs_ntp;
    last_sender_report_sys_time[ssrc_of_sender] = srs_update_system_time();

    return err;
}

srs_error_t SrsRtcPublisher::on_rtcp_xr(char* buf, int nb_buf)
{
    srs_error_t err = srs_success;

    /*
     @see: http://www.rfc-editor.org/rfc/rfc3611.html#section-2

      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |V=2|P|reserved |   PT=XR=207   |             length            |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                              SSRC                             |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     :                         report blocks                         :
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     */

    SrsBuffer stream(buf, nb_buf);
    /*uint8_t first = */stream.read_1bytes();
    uint8_t pt = stream.read_1bytes();
    srs_assert(pt == kXR);
    uint16_t length = (stream.read_2bytes() + 1) * 4;
    /*uint32_t ssrc = */stream.read_4bytes();

    if (length != nb_buf) {
        return srs_error_new(ERROR_RTC_RTCP_CHECK, "invalid XR packet, length=%u, nb_buf=%d", length, nb_buf);
    }

    while (stream.pos() + 4 < length) {
        uint8_t bt = stream.read_1bytes();
        stream.skip(1);
        uint16_t block_length = (stream.read_2bytes() + 1) * 4;

        if (stream.pos() + block_length - 4 > nb_buf) {
            return srs_error_new(ERROR_RTC_RTCP_CHECK, "invalid XR packet block, block_length=%u, nb_buf=%d", block_length, nb_buf);
        }

        if (bt == 5) {
            for (int i = 4; i < block_length; i += 12) {
                uint32_t ssrc = stream.read_4bytes();
                uint32_t lrr = stream.read_4bytes();
                uint32_t dlrr = stream.read_4bytes();

                SrsNtp cur_ntp = SrsNtp::from_time_ms(srs_update_system_time() / 1000);
                uint32_t compact_ntp = (cur_ntp.ntp_second_ << 16) | (cur_ntp.ntp_fractions_ >> 16);

                int rtt_ntp = compact_ntp - lrr - dlrr;
                int rtt = ((rtt_ntp * 1000) >> 16) + ((rtt_ntp >> 16) * 1000);
                srs_verbose("ssrc=%u, compact_ntp=%u, lrr=%u, dlrr=%u, rtt=%d",
                    ssrc, compact_ntp, lrr, dlrr, rtt);

                if (ssrc == video_ssrc) {
                    video_nack_->update_rtt(rtt);
                } else if (ssrc == audio_ssrc) {
                    audio_nack_->update_rtt(rtt);
                }
            }
        }
    }

    return err;
}

srs_error_t SrsRtcPublisher::on_rtcp_feedback(char* buf, int nb_buf)
{
    srs_error_t err = srs_success;
    // TODO: FIXME: Implements it.
    return err;
}

srs_error_t SrsRtcPublisher::on_rtcp_ps_feedback(char* buf, int nb_buf)
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

srs_error_t SrsRtcPublisher::on_rtcp_rr(char* buf, int nb_buf)
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

void SrsRtcPublisher::request_keyframe()
{
    std::string scid = _srs_context->get_id();
    std::string pcid = session_->context_id();
    srs_trace("RTC play=[%d][%s] request keyframe from publish=[%d][%s]", ::getpid(), scid.c_str(), ::getpid(), pcid.c_str());

    request_keyframe_ = true;
}

srs_error_t SrsRtcPublisher::notify(int type, srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;
    // TODO: FIXME: Check error.
    send_rtcp_rr(video_ssrc, video_queue_);
    send_rtcp_rr(audio_ssrc, audio_queue_);
    send_rtcp_xr_rrtr(video_ssrc);
    send_rtcp_xr_rrtr(audio_ssrc);
    send_periodic_twcc();
    
    return err;
}

void SrsRtcPublisher::simulate_nack_drop(int nn)
{
    nn_simulate_nack_drop = nn;
}

void SrsRtcPublisher::simulate_drop_packet(SrsRtpHeader* h, int nn_bytes)
{
    srs_warn("RTC NACK simulator #%d drop seq=%u, ssrc=%u/%s, ts=%u, %d bytes", nn_simulate_nack_drop,
        h->get_sequence(), h->get_ssrc(), (h->get_ssrc()==video_ssrc? "Video":"Audio"), h->get_timestamp(),
        nn_bytes);

    nn_simulate_nack_drop--;
}

SrsRtcSession::SrsRtcSession(SrsRtcServer* s)
{
    req = NULL;
    cid = "";
    is_publisher_ = false;
    encrypt = true;

    source_ = NULL;
    publisher_ = NULL;
    player_ = NULL;
    sendonly_skt = NULL;
    server_ = s;
    transport_ = new SrsSecurityTransport(this);

    state_ = INIT;
    last_stun_time = 0;
    sessionStunTimeout = 0;
    disposing_ = false;

    blackhole = false;
    blackhole_addr = NULL;
    blackhole_stfd = NULL;
}

SrsRtcSession::~SrsRtcSession()
{
    srs_freep(player_);
    srs_freep(publisher_);
    srs_freep(transport_);
    srs_freep(req);
    srs_close_stfd(blackhole_stfd);
    srs_freep(blackhole_addr);
    srs_freep(sendonly_skt);
}

SrsSdp* SrsRtcSession::get_local_sdp()
{
    return &local_sdp;
}

void SrsRtcSession::set_local_sdp(const SrsSdp& sdp)
{
    local_sdp = sdp;
}

SrsSdp* SrsRtcSession::get_remote_sdp()
{
    return &remote_sdp;
}

void SrsRtcSession::set_remote_sdp(const SrsSdp& sdp)
{
    remote_sdp = sdp;
}

SrsRtcSessionStateType SrsRtcSession::state()
{
    return state_;
}

void SrsRtcSession::set_state(SrsRtcSessionStateType state)
{
    state_ = state;
}

string SrsRtcSession::id()
{
    return peer_id_ + "/" + username_;
}


string SrsRtcSession::peer_id()
{
    return peer_id_;
}

void SrsRtcSession::set_peer_id(string v)
{
    peer_id_ = v;
}

string SrsRtcSession::username()
{
    return username_;
}

void SrsRtcSession::set_encrypt(bool v)
{
    encrypt = v;
}

void SrsRtcSession::switch_to_context()
{
    _srs_context->set_id(cid);
}

std::string SrsRtcSession::context_id()
{
    return cid;
}

srs_error_t SrsRtcSession::initialize(SrsRtcSource* source, SrsRequest* r, bool is_publisher, string username, std::string context_id)
{
    srs_error_t err = srs_success;

    username_ = username;
    req = r->copy();
    cid = context_id;
    is_publisher_ = is_publisher;
    source_ = source;

    SrsSessionConfig* cfg = &local_sdp.session_config_;
    if ((err = transport_->initialize(cfg)) != srs_success) {
        return srs_error_wrap(err, "init");
    }

    // TODO: FIXME: Support reload.
    sessionStunTimeout = _srs_config->get_rtc_stun_timeout(req->vhost);
    last_stun_time = srs_get_system_time();

    blackhole = _srs_config->get_rtc_server_black_hole();

    srs_trace("RTC init session, DTLS(role=%s, version=%s), timeout=%dms, blackhole=%d",
        cfg->dtls_role.c_str(), cfg->dtls_version.c_str(), srsu2msi(sessionStunTimeout), blackhole);

    if (blackhole) {
        string blackhole_ep = _srs_config->get_rtc_server_black_hole_addr();
        if (!blackhole_ep.empty()) {
            string host; int port;
            srs_parse_hostport(blackhole_ep, host, port);

            srs_freep(blackhole_addr);
            blackhole_addr = new sockaddr_in();
            blackhole_addr->sin_family = AF_INET;
            blackhole_addr->sin_addr.s_addr = inet_addr(host.c_str());
            blackhole_addr->sin_port = htons(port);

            int fd = socket(AF_INET, SOCK_DGRAM, 0);
            blackhole_stfd = srs_netfd_open_socket(fd);
            srs_assert(blackhole_stfd);

            srs_trace("RTC blackhole %s:%d, fd=%d", host.c_str(), port, fd);
        }
    }

    return err;
}

srs_error_t SrsRtcSession::on_stun(SrsUdpMuxSocket* skt, SrsStunPacket* r)
{
    srs_error_t err = srs_success;

    if (!r->is_binding_request()) {
        return err;
    }

    last_stun_time = srs_get_system_time();

    // We are running in the ice-lite(server) mode. If client have multi network interface,
    // we only choose one candidate pair which is determined by client.
    if (!sendonly_skt || sendonly_skt->peer_id() != skt->peer_id()) {
        update_sendonly_socket(skt);
    }

    // Write STUN messages to blackhole.
    if (blackhole && blackhole_addr && blackhole_stfd) {
        // Ignore any error for black-hole.
        void* p = skt->data(); int len = skt->size();
        srs_sendto(blackhole_stfd, p, len, (sockaddr*)blackhole_addr, sizeof(sockaddr_in), SRS_UTIME_NO_TIMEOUT);
    }

    if ((err = on_binding_request(r)) != srs_success) {
        return srs_error_wrap(err, "stun binding request failed");
    }

    return err;
}

srs_error_t SrsRtcSession::on_dtls(char* data, int nb_data)
{
    return transport_->on_dtls(data, nb_data);
}

srs_error_t SrsRtcSession::on_rtcp(char* data, int nb_data)
{
    srs_error_t err = srs_success;

    if (transport_ == NULL) {
        return srs_error_new(ERROR_RTC_RTCP, "recv unexpect rtp packet before dtls done");
    }

    char unprotected_buf[kRtpPacketSize];
    int nb_unprotected_buf = nb_data;
    if ((err = transport_->unprotect_rtcp(data, unprotected_buf, nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "rtcp unprotect failed");
    }

    if (blackhole && blackhole_addr && blackhole_stfd) {
        // Ignore any error for black-hole.
        void* p = unprotected_buf; int len = nb_unprotected_buf;
        srs_sendto(blackhole_stfd, p, len, (sockaddr*)blackhole_addr, sizeof(sockaddr_in), SRS_UTIME_NO_TIMEOUT);
    }

    if (player_) {
        return player_->on_rtcp(unprotected_buf, nb_unprotected_buf);
    }

    if (publisher_) {
        return publisher_->on_rtcp(unprotected_buf, nb_unprotected_buf);
    }

    return err;
}

srs_error_t SrsRtcSession::on_rtp(char* data, int nb_data)
{
    if (publisher_ == NULL) {
        return srs_error_new(ERROR_RTC_RTCP, "rtc publisher null");
    }

    if (transport_ == NULL) {
        return srs_error_new(ERROR_RTC_RTCP, "recv unexpect rtp packet before dtls done");
    }

    return publisher_->on_rtp(data, nb_data);
}

srs_error_t SrsRtcSession::on_connection_established()
{
    srs_error_t err = srs_success;

    srs_trace("RTC %s session=%s, to=%dms connection established", (is_publisher_? "Publisher":"Subscriber"),
        id().c_str(), srsu2msi(sessionStunTimeout));

    if (is_publisher_) {
        if ((err = start_publish()) != srs_success) {
            return srs_error_wrap(err, "start publish");
        }
    } else {
        if ((err = start_play()) != srs_success) {
            return srs_error_wrap(err, "start play");
        }
    }

    return err;
}

srs_error_t SrsRtcSession::start_play()
{
    srs_error_t err = srs_success;

    srs_freep(player_);
    player_ = new SrsRtcPlayer(this, _srs_context->get_id());

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

    if ((err = player_->initialize(video_ssrc, audio_ssrc, video_payload_type, audio_payload_type)) != srs_success) {
        return srs_error_wrap(err, "SrsRtcPlayer init");
    }

    if ((err = player_->start()) != srs_success) {
        return srs_error_wrap(err, "start SrsRtcPlayer");
    }

    return err;
}

srs_error_t SrsRtcSession::start_publish()
{
    srs_error_t err = srs_success;

    srs_freep(publisher_);
    publisher_ = new SrsRtcPublisher(this);
    // Request PLI for exists players?
    //publisher_->request_keyframe();

    uint32_t video_ssrc = 0;
    uint32_t audio_ssrc = 0;
    for (size_t i = 0; i < remote_sdp.media_descs_.size(); ++i) {
        const SrsMediaDesc& media_desc = remote_sdp.media_descs_[i];
        if (media_desc.is_audio()) {
            if (!media_desc.ssrc_infos_.empty()) {
                audio_ssrc = media_desc.ssrc_infos_[0].ssrc_;
            }
        } else if (media_desc.is_video()) {
            if (!media_desc.ssrc_infos_.empty()) {
                video_ssrc = media_desc.ssrc_infos_[0].ssrc_;
            }
        }
    }

    uint32_t twcc_ext_id = 0;
    for (size_t i = 0; i < local_sdp.media_descs_.size(); ++i) {
        const SrsMediaDesc& media_desc = remote_sdp.media_descs_[i];
        map<int, string> extmaps = media_desc.get_extmaps();
        for(map<int, string>::iterator it_ext = extmaps.begin(); it_ext != extmaps.end(); ++it_ext) {
            if(kTWCCExt == it_ext->second) {
                twcc_ext_id = it_ext->first;
                break;
            }
        }
        if (twcc_ext_id != 0){
            break;
        }
    }

    // FIXME: err process.
    if ((err = publisher_->initialize(video_ssrc, audio_ssrc, twcc_ext_id, req)) != srs_success) {
        return srs_error_wrap(err, "rtc publisher init");
    }

    return err;
}

bool SrsRtcSession::is_stun_timeout()
{
    return last_stun_time + sessionStunTimeout < srs_get_system_time();
}

void SrsRtcSession::update_sendonly_socket(SrsUdpMuxSocket* skt)
{
    if (sendonly_skt) {
        srs_trace("session %s address changed, update %s -> %s",
            id().c_str(), sendonly_skt->peer_id().c_str(), skt->peer_id().c_str());
    }

    srs_freep(sendonly_skt);
    sendonly_skt = skt->copy_sendonly();
}

void SrsRtcSession::simulate_nack_drop(int nn)
{
    if (player_) {
        player_->simulate_nack_drop(nn);
    }

    if (publisher_) {
        publisher_->simulate_nack_drop(nn);
    }
}

#ifdef SRS_OSX
// These functions are similar to the older byteorder(3) family of functions.
// For example, be32toh() is identical to ntohl().
// @see https://linux.die.net/man/3/be32toh
#define be32toh ntohl
#endif

srs_error_t SrsRtcSession::on_binding_request(SrsStunPacket* r)
{
    srs_error_t err = srs_success;

    bool strict_check = _srs_config->get_rtc_stun_strict_check(req->vhost);
    if (strict_check && r->get_ice_controlled()) {
        // @see: https://tools.ietf.org/html/draft-ietf-ice-rfc5245bis-00#section-6.1.3.1
        // TODO: Send 487 (Role Conflict) error response.
        return srs_error_new(ERROR_RTC_STUN, "Peer must not in ice-controlled role in ice-lite mode.");
    }

    SrsStunPacket stun_binding_response;
    char buf[kRtpPacketSize];
    SrsBuffer* stream = new SrsBuffer(buf, sizeof(buf));
    SrsAutoFree(SrsBuffer, stream);

    stun_binding_response.set_message_type(BindingResponse);
    stun_binding_response.set_local_ufrag(r->get_remote_ufrag());
    stun_binding_response.set_remote_ufrag(r->get_local_ufrag());
    stun_binding_response.set_transcation_id(r->get_transcation_id());
    // FIXME: inet_addr is deprecated, IPV6 support
    stun_binding_response.set_mapped_address(be32toh(inet_addr(sendonly_skt->get_peer_ip().c_str())));
    stun_binding_response.set_mapped_port(sendonly_skt->get_peer_port());

    if ((err = stun_binding_response.encode(get_local_sdp()->get_ice_pwd(), stream)) != srs_success) {
        return srs_error_wrap(err, "stun binding response encode failed");
    }

    if ((err = sendonly_skt->sendto(stream->data(), stream->pos(), 0)) != srs_success) {
        return srs_error_wrap(err, "stun binding response send failed");
    }

    if (state_ == WAITING_STUN) {
        state_ = DOING_DTLS_HANDSHAKE;

        peer_id_ = sendonly_skt->peer_id();
        server_->insert_into_id_sessions(peer_id_, this);

        state_ = DOING_DTLS_HANDSHAKE;
        srs_trace("rtc session=%s, STUN done, waitting DTLS handshake.", id().c_str());

        if((err = transport_->start_active_handshake()) != srs_success) {
            return srs_error_wrap(err, "fail to dtls handshake");
        }
    }

    if (blackhole && blackhole_addr && blackhole_stfd) {
        // Ignore any error for black-hole.
        void* p = stream->data(); int len = stream->pos();
        srs_sendto(blackhole_stfd, p, len, (sockaddr*)blackhole_addr, sizeof(sockaddr_in), SRS_UTIME_NO_TIMEOUT);
    }

    return err;
}

ISrsRtcHijacker::ISrsRtcHijacker()
{
}

ISrsRtcHijacker::~ISrsRtcHijacker()
{
}

ISrsRtcHijacker* _srs_rtc_hijacker = NULL;

