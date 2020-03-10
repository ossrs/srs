/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

#include <srs_app_rtsp.hpp>

#include <algorithm>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_rtsp_stack.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_file.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_raw_avc.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_format.hpp>
//#include <uuid/uuid.h>


SrsRtpConn::SrsRtpConn(SrsRtspConn* r, int p, int sid)
{
    rtsp = r;
    _port = p;
    stream_id = sid;
    // TODO: support listen at <[ip:]port>
    listener = new SrsUdpListener(this, srs_any_address_for_listener(), p);
    cache = new SrsRtpPacket();
    pprint = SrsPithyPrint::create_caster();
}

SrsRtpConn::~SrsRtpConn()
{
    srs_freep(listener);
    srs_freep(cache);
    srs_freep(pprint);
}

int SrsRtpConn::port()
{
    return _port;
}

srs_error_t SrsRtpConn::listen()
{
    return listener->listen();
}

srs_error_t SrsRtpConn::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
{
    srs_error_t err = srs_success;
    
    pprint->elapse();
    
    if (true) {
        SrsBuffer stream(buf, nb_buf);
        
        SrsRtpPacket pkt;
        if ((err = pkt.decode(&stream)) != srs_success) {
            return srs_error_wrap(err, "decode");
        }
        
        if (pkt.chunked) {
            if (!cache) {
                cache = new SrsRtpPacket();
            }
            cache->copy(&pkt);
            cache->payload->append(pkt.payload->bytes(), pkt.payload->length());
            if (pprint->can_print()) {
                srs_trace("<- " SRS_CONSTS_LOG_STREAM_CASTER " rtsp: rtp chunked %dB, age=%d, vt=%d/%u, sts=%u/%#x/%#x, paylod=%dB",
                          nb_buf, pprint->age(), cache->version, cache->payload_type, cache->sequence_number, cache->timestamp, cache->ssrc,
                          cache->payload->length()
                          );
            }

            if (!cache->completed){
                return err;
            }
        } else {
            srs_freep(cache);
            cache = new SrsRtpPacket();
            cache->reap(&pkt);
        }
    }
    
    if (pprint->can_print()) {
        srs_trace("<- " SRS_CONSTS_LOG_STREAM_CASTER " rtsp: rtp #%d %dB, age=%d, vt=%d/%u, sts=%u/%u/%#x, paylod=%dB, chunked=%d",
                  stream_id, nb_buf, pprint->age(), cache->version, cache->payload_type, cache->sequence_number, cache->timestamp, cache->ssrc,
                  cache->payload->length(), cache->chunked
                  );
    }
    
    // always free it.
    SrsAutoFree(SrsRtpPacket, cache);
    
    if ((err = rtsp->on_rtp_packet(cache, stream_id)) != srs_success) {
        return srs_error_wrap(err, "process rtp packet");
    }
    
    return err;
}



SrsRtpOverTcpConn::SrsRtpOverTcpConn(SrsRtspConn* r, int p, int sid)
{
    rtsp = r;
    _port = p;
    stream_id = sid;
    // TODO: support listen at <[ip:]port>
    listener = new SrsUdpListener(this, srs_any_address_for_listener(), p);
    cache = new SrsRtpPacket();
    pprint = SrsPithyPrint::create_caster();
}

SrsRtpOverTcpConn::~SrsRtpOverTcpConn()
{
    srs_freep(listener);
    srs_freep(cache);
    srs_freep(pprint);
}

int SrsRtpOverTcpConn::port()
{
    return _port;
}

srs_error_t SrsRtpOverTcpConn::listen()
{
    return listener->listen();
}

srs_error_t SrsRtpOverTcpConn::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
{
    srs_error_t err = srs_success;
    
    pprint->elapse();
    
    if (true) {
        SrsBuffer stream(buf, nb_buf);
        
        SrsRtpPacket pkt;
        if ((err = pkt.decode(&stream)) != srs_success) {
            return srs_error_wrap(err, "decode");
        }
        
        if (pkt.chunked) {
            if (!cache) {
                cache = new SrsRtpPacket();
            }
            cache->copy(&pkt);
            cache->payload->append(pkt.payload->bytes(), pkt.payload->length());
            if (pprint->can_print()) {
                srs_trace("<- " SRS_CONSTS_LOG_STREAM_CASTER " rtsp: rtp chunked %dB, age=%d, vt=%d/%u, sts=%u/%#x/%#x, paylod=%dB",
                          nb_buf, pprint->age(), cache->version, cache->payload_type, cache->sequence_number, cache->timestamp, cache->ssrc,
                          cache->payload->length()
                          );
            }

            if (!cache->completed){
                return err;
            }
        } else {
            srs_freep(cache);
            cache = new SrsRtpPacket();
            cache->reap(&pkt);
        }
    }
    
    if (pprint->can_print()) {
        srs_trace("<- " SRS_CONSTS_LOG_STREAM_CASTER " rtsp: rtp #%d %dB, age=%d, vt=%d/%u, sts=%u/%u/%#x, paylod=%dB, chunked=%d",
                  stream_id, nb_buf, pprint->age(), cache->version, cache->payload_type, cache->sequence_number, cache->timestamp, cache->ssrc,
                  cache->payload->length(), cache->chunked
                  );
    }
    
    // always free it.
    SrsAutoFree(SrsRtpPacket, cache);
    
    if ((err = rtsp->on_rtp_packet(cache, stream_id)) != srs_success) {
        return srs_error_wrap(err, "process rtp packet");
    }
    
    return err;
}



SrsRtspAudioCache::SrsRtspAudioCache()
{
    dts = 0;
    audio = NULL;
    payload = NULL;
}

SrsRtspAudioCache::~SrsRtspAudioCache()
{
    srs_freep(audio);
    srs_freep(payload);
}

SrsRtspJitter::SrsRtspJitter()
{
    delta = 0;
    previous_timestamp = 0;
    pts = 0;
}

SrsRtspJitter::~SrsRtspJitter()
{
}

int64_t SrsRtspJitter::timestamp()
{
    return pts;
}

srs_error_t SrsRtspJitter::correct(int64_t& ts)
{
    srs_error_t err = srs_success;
    
    if (previous_timestamp == 0) {
        previous_timestamp = ts;
    }
    
    delta = srs_max(0, (int)(ts - previous_timestamp));
    if (delta > 90000) {
        delta = 0;
    }
    
    previous_timestamp = ts;
    
    ts = pts + delta;
    pts = ts;
    
    return err;
}

SrsRtspConn::SrsRtspConn(SrsRtspCaster* c, srs_netfd_t fd, std::string o)
{
    output_template = o;
    
    session = "";
    video_rtp = NULL;
    audio_rtp = NULL;
    
    caster = c;
    stfd = fd;
    skt = new SrsStSocket();
    rtsp = new SrsRtspStack(skt);
    trd = new SrsSTCoroutine("rtsp", this);
    
    req = NULL;
    sdk = NULL;
    vjitter = new SrsRtspJitter();
    ajitter = new SrsRtspJitter();
    
    avc = new SrsRawH264Stream();
    aac = new SrsRawAacStream();
    acodec = new SrsRawAacStreamCodec();
    acache = new SrsRtspAudioCache();
}

SrsRtspConn::~SrsRtspConn()
{
    close();
    
    srs_close_stfd(stfd);
    
    srs_freep(video_rtp);
    srs_freep(audio_rtp);
    
    srs_freep(trd);
    srs_freep(skt);
    srs_freep(rtsp);
    
    srs_freep(sdk);
    srs_freep(req);
    
    srs_freep(vjitter);
    srs_freep(ajitter);
    srs_freep(acodec);
    srs_freep(acache);
}

srs_error_t SrsRtspConn::serve()
{
    srs_error_t err = srs_success;
    
    if ((err = skt->initialize(stfd)) != srs_success) {
        return srs_error_wrap(err, "socket initialize");
    }
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "rtsp connection");
    }
    
    return err;
}

srs_error_t SrsRtspConn::do_cycle()
{
    srs_error_t err = srs_success;
    
    // retrieve ip of client.
    std::string ip = srs_get_peer_ip(srs_netfd_fileno(stfd));
    if (ip.empty() && !_srs_config->empty_ip_ok()) {
        srs_warn("empty ip for fd=%d", srs_netfd_fileno(stfd));
    }
    srs_trace("rtsp: serve %s", ip.c_str());
    
    // consume all rtsp messages.
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "rtsp cycle");
        }
        
        SrsRtspRequest* req = NULL;
        if ((err = rtsp->recv_message(&req)) != srs_success) {
            return srs_error_wrap(err, "recv message");
        }
        SrsAutoFree(SrsRtspRequest, req);
        srs_info("rtsp: got rtsp request");
        
        if (req->is_options()) {
            SrsRtspOptionsResponse* res = new SrsRtspOptionsResponse((int)req->seq);
            res->session = session;
            if ((err = rtsp->send_message(res)) != srs_success) {
                return  srs_error_wrap(err, "response option");
            }
        } else if (req->is_announce()) {
            if (rtsp_tcUrl.empty()) {
                rtsp_tcUrl = req->uri;
            }
            size_t pos = string::npos;
            if ((pos = rtsp_tcUrl.rfind(".sdp")) != string::npos) {
                rtsp_tcUrl = rtsp_tcUrl.substr(0, pos);
            }
            srs_parse_rtmp_url(rtsp_tcUrl, rtsp_tcUrl, rtsp_stream);
            
            srs_assert(req->sdp);
            video_id = ::atoi(req->sdp->video_stream_id.c_str());
            audio_id = ::atoi(req->sdp->audio_stream_id.c_str());
            video_codec = req->sdp->video_codec;
            audio_codec = req->sdp->audio_codec;
            audio_sample_rate = ::atoi(req->sdp->audio_sample_rate.c_str());
            audio_channel = ::atoi(req->sdp->audio_channel.c_str());
            h264_sps = req->sdp->video_sps;
            h264_pps = req->sdp->video_pps;
            aac_specific_config = req->sdp->audio_sh;
            srs_trace("rtsp: video(#%d, %s, %s/%s), audio(#%d, %s, %s/%s, %dHZ %dchannels), %s/%s",
                      video_id, video_codec.c_str(), req->sdp->video_protocol.c_str(), req->sdp->video_transport_format.c_str(),
                      audio_id, audio_codec.c_str(), req->sdp->audio_protocol.c_str(), req->sdp->audio_transport_format.c_str(),
                      audio_sample_rate, audio_channel, rtsp_tcUrl.c_str(), rtsp_stream.c_str()
                      );
            
            SrsRtspResponse* res = new SrsRtspResponse((int)req->seq);
            res->session = session;
            if ((err = rtsp->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response announce");
            }
        } else if (req->is_setup()) {
            srs_assert(req->transport);
            int lpm = 0;
            if ((err = caster->alloc_port(&lpm)) != srs_success) {
                return srs_error_wrap(err, "alloc port");
            }
            
            SrsRtpConn* rtp = NULL;
            if (req->stream_id == video_id) {
                srs_freep(video_rtp);
                rtp = video_rtp = new SrsRtpConn(this, lpm, video_id);
            } else {
                srs_freep(audio_rtp);
                rtp = audio_rtp = new SrsRtpConn(this, lpm, audio_id);
            }
            if ((err = rtp->listen()) != srs_success) {
                return srs_error_wrap(err, "rtp listen");
            }
            srs_trace("rtsp: #%d %s over %s/%s/%s %s client-port=%d-%d, server-port=%d-%d",
                req->stream_id, (req->stream_id == video_id)? "Video":"Audio",
                req->transport->transport.c_str(), req->transport->profile.c_str(), req->transport->lower_transport.c_str(),
                req->transport->cast_type.c_str(), req->transport->client_port_min, req->transport->client_port_max,
                lpm, lpm + 1);
            
            // create session.
            if (session.empty()) {
                session = "O9EaZ4bf"; // TODO: FIXME: generate session id.
            }
            
            SrsRtspSetupResponse* res = new SrsRtspSetupResponse((int)req->seq);
            res->client_port_min = req->transport->client_port_min;
            res->client_port_max = req->transport->client_port_max;
            res->local_port_min = lpm;
            res->local_port_max = lpm + 1;
            res->session = session;
            if ((err = rtsp->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response setup");
            }
        } else if (req->is_record()) {
            SrsRtspResponse* res = new SrsRtspResponse((int)req->seq);
            res->session = session;
            if ((err = rtsp->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response record");
            }
        }
    }
    
    return err;
}

srs_error_t SrsRtspConn::on_rtp_packet(SrsRtpPacket* pkt, int stream_id)
{
    srs_error_t err = srs_success;
    
    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    
    if (stream_id == video_id) {
        // rtsp tbn is ts tbn.
        int64_t pts = pkt->timestamp;
        if ((err = vjitter->correct(pts)) != srs_success) {
            return srs_error_wrap(err, "jitter");
        }
        
        // TODO: FIXME: set dts to pts, please finger out the right dts.
        int64_t dts = pts;
        
        return on_rtp_video(pkt, dts, pts);
    } else {
        // rtsp tbn is ts tbn.
        int64_t pts = pkt->timestamp;
        if ((err = ajitter->correct(pts)) != srs_success) {
            return srs_error_wrap(err, "jitter");
        }
        
        return on_rtp_audio(pkt, pts);
    }
    
    return err;
}

srs_error_t SrsRtspConn::cycle()
{
    // serve the rtsp client.
    srs_error_t err = do_cycle();
    
    caster->remove(this);
    
    if (err == srs_success) {
        srs_trace("client finished.");
    } else if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. code=%d", srs_error_code(err));
        srs_freep(err);
    }
    
    if (video_rtp) {
        caster->free_port(video_rtp->port(), video_rtp->port() + 1);
    }
    
    if (audio_rtp) {
        caster->free_port(audio_rtp->port(), audio_rtp->port() + 1);
    }
    
    return err;
}

srs_error_t SrsRtspConn::on_rtp_video(SrsRtpPacket* pkt, int64_t dts, int64_t pts)
{
    srs_error_t err = srs_success;
    
    if ((err = kickoff_audio_cache(pkt, dts)) != srs_success) {
        return srs_error_wrap(err, "kickoff audio cache");
    }
    
    char* bytes = pkt->payload->bytes();
    int length = pkt->payload->length();
    uint32_t fdts = (uint32_t)(dts / 90);
    uint32_t fpts = (uint32_t)(pts / 90);
    if ((err = write_h264_ipb_frame(bytes, length, fdts, fpts)) != srs_success) {
        return srs_error_wrap(err, "write ibp frame");
    }
    
    return err;
}

srs_error_t SrsRtspConn::on_rtp_audio(SrsRtpPacket* pkt, int64_t dts)
{
    srs_error_t err = srs_success;
    
    if ((err = kickoff_audio_cache(pkt, dts)) != srs_success) {
        return srs_error_wrap(err, "kickoff audio cache");
    }
    
    // cache current audio to kickoff.
    acache->dts = dts;
    acache->audio = pkt->audio;
    acache->payload = pkt->payload;
    
    pkt->audio = NULL;
    pkt->payload = NULL;
    
    return err;
}

srs_error_t SrsRtspConn::kickoff_audio_cache(SrsRtpPacket* pkt, int64_t dts)
{
    srs_error_t err = srs_success;
    
    // nothing to kick off.
    if (!acache->payload) {
        return err;
    }
    
    if (dts - acache->dts > 0 && acache->audio->nb_samples > 0) {
        int64_t delta = (dts - acache->dts) / acache->audio->nb_samples;
        for (int i = 0; i < acache->audio->nb_samples; i++) {
            char* frame = acache->audio->samples[i].bytes;
            int nb_frame = acache->audio->samples[i].size;
            int64_t timestamp = (acache->dts + delta * i) / 90;
            acodec->aac_packet_type = 1;
            if ((err = write_audio_raw_frame(frame, nb_frame, acodec, (uint32_t)timestamp)) != srs_success) {
                return srs_error_wrap(err, "write audio raw frame");
            }
        }
    }
    
    acache->dts = 0;
    srs_freep(acache->audio);
    srs_freep(acache->payload);
    
    return err;
}

srs_error_t SrsRtspConn::write_sequence_header()
{
    srs_error_t err = srs_success;
    
    // use the current dts.
    int64_t dts = vjitter->timestamp() / 90;
    
    // send video sps/pps
    if ((err = write_h264_sps_pps((uint32_t)dts, (uint32_t)dts)) != srs_success) {
        return srs_error_wrap(err, "write sps/pps");
    }
    
    // generate audio sh by audio specific config.
    if (true) {
        std::string sh = aac_specific_config;
        
        SrsFormat* format = new SrsFormat();
        SrsAutoFree(SrsFormat, format);
        
        if ((err = format->on_aac_sequence_header((char*)sh.c_str(), (int)sh.length())) != srs_success) {
            return srs_error_wrap(err, "on aac sequence header");
        }
        
        SrsAudioCodecConfig* dec = format->acodec;
        
        acodec->sound_format = SrsAudioCodecIdAAC;
        acodec->sound_type = (dec->aac_channels == 2)? SrsAudioChannelsStereo : SrsAudioChannelsMono;
        acodec->sound_size = SrsAudioSampleBits16bit;
        acodec->aac_packet_type = 0;
        
        static int srs_aac_srates[] = {
            96000, 88200, 64000, 48000,
            44100, 32000, 24000, 22050,
            16000, 12000, 11025,  8000,
            7350,     0,     0,    0
        };
        switch (srs_aac_srates[dec->aac_sample_rate]) {
            case 11025:
                acodec->sound_rate = SrsAudioSampleRate11025;
                break;
            case 22050:
                acodec->sound_rate = SrsAudioSampleRate22050;
                break;
            case 44100:
                acodec->sound_rate = SrsAudioSampleRate44100;
                break;
            default:
                break;
        };
        
        if ((err = write_audio_raw_frame((char*)sh.data(), (int)sh.length(), acodec, (uint32_t)dts)) != srs_success) {
            return srs_error_wrap(err, "write audio raw frame");
        }
    }
    
    return err;
}

srs_error_t SrsRtspConn::write_h264_sps_pps(uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;
    
    // h264 raw to h264 packet.
    std::string sh;
    if ((err = avc->mux_sequence_header(h264_sps, h264_pps, dts, pts, sh)) != srs_success) {
        return srs_error_wrap(err, "mux sequence header");
    }
    
    // h264 packet to flv packet.
    int8_t frame_type = SrsVideoAvcFrameTypeKeyFrame;
    int8_t avc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux avc to flv");
    }
    
    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    if ((err = rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "write packet");
    }
    
    return err;
}

srs_error_t SrsRtspConn::write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;
    
    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
    
    // for IDR frame, the frame is keyframe.
    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;
    if (nal_unit_type == SrsAvcNaluTypeIDR) {
        frame_type = SrsVideoAvcFrameTypeKeyFrame;
    }
    
    std::string ibp;
    if ((err = avc->mux_ipb_frame(frame, frame_size, ibp)) != srs_success) {
        return srs_error_wrap(err, "mux ibp frame");
    }
    
    int8_t avc_packet_type = SrsVideoAvcFrameTraitNALU;
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(ibp, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux avc to flv");
    }
    
    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    return rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv);
}

srs_error_t SrsRtspConn::write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts)
{
    srs_error_t err = srs_success;
    
    char* data = NULL;
    int size = 0;
    if ((err = aac->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success) {
        return srs_error_wrap(err, "mux aac to flv");
    }
    
    return rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
}

srs_error_t SrsRtspConn::rtmp_write_packet(char type, uint32_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;
    
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    
    SrsSharedPtrMessage* msg = NULL;
    
    if ((err = srs_rtmp_create_msg(type, timestamp, data, size, sdk->sid(), &msg)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }
    srs_assert(msg);
    
    // send out encoded msg.
    if ((err = sdk->send_and_free_message(msg)) != srs_success) {
        close();
        return srs_error_wrap(err, "write message");
    }
    
    return err;
}

srs_error_t SrsRtspConn::connect()
{
    srs_error_t err = srs_success;
    
    // Ignore when connected.
    if (sdk) {
        return err;
    }
    
    // generate rtmp url to connect to.
    std::string url;
    if (!req) {
        std::string schema, host, vhost, app, param;
        int port;
        srs_discovery_tc_url(rtsp_tcUrl, schema, host, vhost, app, rtsp_stream, port, param);

        // generate output by template.
        std::string output = output_template;
        output = srs_string_replace(output, "[app]", app);
        output = srs_string_replace(output, "[stream]", rtsp_stream);
        url = output;
    }
    
    // connect host.
    srs_utime_t cto = SRS_CONSTS_RTMP_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_PULSE;
    sdk = new SrsSimpleRtmpClient(url, cto, sto);
    
    if ((err = sdk->connect()) != srs_success) {
        close();
        return srs_error_wrap(err, "connect %s failed, cto=%dms, sto=%dms.", url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }
    
    // publish.
    if ((err = sdk->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        close();
        return srs_error_wrap(err, "publish %s failed", url.c_str());
    }
    
    return write_sequence_header();
}

void SrsRtspConn::close()
{
    srs_freep(sdk);
}

SrsRtspCaster::SrsRtspCaster(SrsConfDirective* c)
{
    // TODO: FIXME: support reload.
    output = _srs_config->get_stream_caster_output(c);
    local_port_min = _srs_config->get_stream_caster_rtp_port_min(c);
    local_port_max = _srs_config->get_stream_caster_rtp_port_max(c);
}

SrsRtspCaster::~SrsRtspCaster()
{
    std::vector<SrsRtspConn*>::iterator it;
    for (it = clients.begin(); it != clients.end(); ++it) {
        SrsRtspConn* conn = *it;
        srs_freep(conn);
    }
    clients.clear();
    used_ports.clear();
}

srs_error_t SrsRtspCaster::alloc_port(int* pport)
{
    srs_error_t err = srs_success;
    
    // use a pair of port.
    for (int i = local_port_min; i < local_port_max - 1; i += 2) {
        if (!used_ports[i]) {
            used_ports[i] = true;
            used_ports[i + 1] = true;
            *pport = i;
            break;
        }
    }
    srs_info("rtsp: alloc port=%d-%d", *pport, *pport + 1);
    
    return err;
}

void SrsRtspCaster::free_port(int lpmin, int lpmax)
{
    for (int i = lpmin; i < lpmax; i++) {
        used_ports[i] = false;
    }
    srs_trace("rtsp: free rtp port=%d-%d", lpmin, lpmax);
}

srs_error_t SrsRtspCaster::on_tcp_client(srs_netfd_t stfd)
{
    srs_error_t err = srs_success;
    
    SrsRtspConn* conn = new SrsRtspConn(this, stfd, output);
    
    if ((err = conn->serve()) != srs_success) {
        srs_freep(conn);
        return srs_error_wrap(err, "serve conn");
    }
    
    clients.push_back(conn);
    
    return err;
}

void SrsRtspCaster::remove(SrsRtspConn* conn)
{
    std::vector<SrsRtspConn*>::iterator it = find(clients.begin(), clients.end(), conn);
    if (it != clients.end()) {
        clients.erase(it);
    }
    srs_info("rtsp: remove connection from caster.");
    
    srs_freep(conn);
}

Srs28181StreamServer::Srs28181StreamServer()
{
    // default value for testing
    output = "output_rtmp_url";
    local_port_min = 57000;
    local_port_max = 60000;
}


Srs28181StreamServer::Srs28181StreamServer(SrsConfDirective* c)
{
    // TODO: FIXME: support reload.
    output = _srs_config->get_stream_caster_output(c);
    local_port_min = _srs_config->get_stream_caster_rtp_port_min(c);
    local_port_max = _srs_config->get_stream_caster_rtp_port_max(c);
}

Srs28181StreamServer::~Srs28181StreamServer()
{
   std::vector<Srs28181Listener*>::iterator it;
    for (it = listeners.begin(); it != listeners.end(); ++it) {
        Srs28181Listener* ltn = *it;
        srs_freep(ltn);
    }
    listeners.clear();
    used_ports.clear();

    srs_info("28181- server: deconstruction");
}

srs_error_t Srs28181StreamServer::create_listener(SrsListenerType type, int& ltn_port, std::string& suuid )
{
    srs_error_t err = srs_success;

    //uuid_t sid;
    //uuid_generate(sid);
    //suuid.append((char*)sid,128);

    // Fix Me: should use uuid in future
    std::string rd = "";
    srand(time(NULL));
    for(int i=0;i<32;i++)
    {
        rd = rd + char(rand()%10+'0');
    }
    suuid = rd;

    Srs28181Listener * ltn = NULL;
    if( type == SrsListener28181UdpStream ){
        ltn = new Srs28181UdpStreamListener(suuid);
    }
    else if(SrsListener28181UdpStream){
        ltn = new Srs28181TcpStreamListener();
    }
    else{
        return srs_error_new(13026, "28181 listener creation");
    }

    int port = 0;
    alloc_port(&port); 
    ltn_port= port;

    // using default port for testing
    // port = 20090;
    srs_trace("28181-stream-server: start a new listener on %s-%d stream_uuid:%s",
        srs_any_address_for_listener().c_str(),port,suuid.c_str());

    if ((err = ltn->listen(srs_any_address_for_listener(),port)) != srs_success) {
        free_port(port,port+2);
        srs_freep(ltn);
        return srs_error_wrap(err, "28181 listener creation");
    }

    


    listeners.push_back(ltn);

    return err;
}

srs_error_t Srs28181StreamServer::release_listener()
{
    srs_error_t err = srs_success;
    return err;
}

srs_error_t Srs28181StreamServer::alloc_port(int* pport)
{
    srs_error_t err = srs_success;
    
    int i = 0;
    // use a pair of port.
    for (i = local_port_min; i < local_port_max - 1; i += 2) {
        if (!used_ports[i]) {
            used_ports[i] = true;
            used_ports[i + 1] = true;
            *pport = i;
            break;
        }
    }

    if(i>= local_port_max - 1){
        return srs_error_new(10020,"listen port alloc failed!");
    }

    srs_info("28181 tcp stream: alloc port=%d-%d", *pport, *pport + 1);
    
    return err;
}

void Srs28181StreamServer::free_port(int lpmin, int lpmax)
{
    for (int i = lpmin; i < lpmax; i++) {
        used_ports[i] = false;
    }
    srs_trace("28181stream: free rtp port=%d-%d", lpmin, lpmax);
}


void Srs28181StreamServer::remove()
{
    /*
    std::vector<SrsRtspConn*>::iterator it = find(clients.begin(), clients.end(), conn);
    if (it != clients.end()) {
        clients.erase(it);
    }
    srs_info("rtsp: remove connection from caster.");
    
    srs_freep(conn);
    */
}

Srs28181Listener::Srs28181Listener()
{
    port = 0;
    //server = NULL;
    //type = "";
}

Srs28181Listener::~Srs28181Listener()
{
}
/*
SrsListenerType Srs28181Listener::listen_type()
{
    //return type;
}*/

Srs28181TcpStreamListener::Srs28181TcpStreamListener() 
{
    listener = NULL;
}

//(SrsServer* svr, SrsListenerType t, SrsConfDirective* c) : SrsListener(svr, t)
/*
Srs28181TcpStreamListener::Srs28181TcpStreamListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c) 
{
    listener = NULL;
    
    // the caller already ensure the type is ok,
    // we just assert here for unknown stream caster.
    srs_assert(type == SrsListenerGB28181TcpStream);
    if (type == SrsListenerGB28181TcpStream) {
        caster = new Srs28181Caster(c);
    }
}
*/

Srs28181TcpStreamListener::~Srs28181TcpStreamListener()
{
    std::vector<Srs28181TcpStreamConn*>::iterator it;
    for (it = clients.begin(); it != clients.end(); ++it) {
        Srs28181TcpStreamConn* conn = *it;
        srs_freep(conn);
    }
    clients.clear();

    //srs_freep(caster);
    srs_freep(listener);
}

srs_error_t Srs28181TcpStreamListener::listen(string i, int p)
{
    srs_error_t err = srs_success;
    
    // the caller already ensure the type is ok,
    // we just assert here for unknown stream caster.
    //srs_assert(type == SrsListenerRtsp);
    
    std::string ip = i;
    int port = p;
    
    srs_freep(listener);
    listener = new SrsTcpListener(this, ip, port);
    
    if ((err = listener->listen()) != srs_success) {
        return srs_error_wrap(err, "28181 listen %s:%d", ip.c_str(), port);
    }
    
    //string v = srs_listener_type2string(type);
    srs_trace("%s listen at tcp://%s:%d, fd=%d", "v.c_str()", ip.c_str(), port, listener->fd());
    
    return err;
}

srs_error_t Srs28181TcpStreamListener::on_tcp_client(srs_netfd_t stfd)
{
    srs_error_t err = srs_success;

    if(clients.size()>=1){
        return srs_error_wrap(err,"only allow one src!");
    }

    std::string output = "output_temple";
    Srs28181TcpStreamConn * conn = new Srs28181TcpStreamConn(this, stfd, output);
    srs_trace("28181- listener(0x%x): accept a new connection(0x%x)",this,conn);

    if((err = conn->init())!=srs_success){
        srs_freep(conn);
        return srs_error_wrap(err,"28181 stream conn init");
    }
    clients.push_back(conn);
    
    return err;
}

srs_error_t Srs28181TcpStreamListener::remove_conn(Srs28181TcpStreamConn* c)
{
    srs_error_t err = srs_success;
    //srs_error_new(ERROR_THREAD_DISPOSED, "disposed");

    std::vector<Srs28181TcpStreamConn*>::iterator it = find(clients.begin(), clients.end(), c);
    if (it != clients.end()) {
        clients.erase(it);
    }
    srs_info("28181 - listener: remove connection.");

    return err;
}

Srs28181UdpStreamListener::Srs28181UdpStreamListener(std::string suuid)
{
    listener = NULL;
    streamcore = new Srs28181StreamCore(suuid);
}

Srs28181UdpStreamListener::~Srs28181UdpStreamListener()
{
    srs_freep(streamcore);
    srs_freep(listener);
    srs_trace("Srs28181UdpStreamListener deconstruction!");
}

srs_error_t Srs28181UdpStreamListener::listen(string i, int p)
{
    srs_error_t err = srs_success;
    
    ip = i;
    port = p;
    
    srs_freep(listener);
    listener = new SrsUdpListener(this, ip, port);
    
    if ((err = listener->listen()) != srs_success) {
        return srs_error_wrap(err, "listen %s:%d", ip.c_str(), port);
    }
    
    // notify the handler the fd changed.
    if ((err = on_stfd_change(listener->stfd())) != srs_success) {
        return srs_error_wrap(err, "notify fd change failed");
    }
    
    //string v = srs_listener_type2string(type);
    srs_trace("%s listen 28181 stream at udp://%s:%d, fd=%d", "v.c_str()", ip.c_str(), port, listener->fd());
    
    return err;
}

srs_error_t Srs28181UdpStreamListener::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
{
    srs_error_t err = srs_success;

    int ret = streamcore->decode_packet_v2(buf,nb_buf);
    //srs_trace("28181 udp stream: recv size:%d", nb_buf);
     if (ret != 0) {
        return srs_error_new(ret, "process 28181 udp stream");
    }

    return err;
}


Srs28181StreamCore::Srs28181StreamCore(std::string suuid)//(Srs28181TcpStreamListener* l, std::string o)
{
    //output_template = o;

    target_tcUrl = "rtmp://127.0.0.1:7935/live/"+suuid;//"rtmp://127.0.0.1:" + "7935" + "/live/test";
	output_template = "rtmp://127.0.0.1:7935/[app]/[stream]";
    
    session = "";
    video_rtp = NULL;
    audio_rtp = NULL;

    // TODO: set stream_id when connected
    stream_id = 50125;
    video_id = stream_id;

    boundary_type_ = TimestampBoundary;
    
    //listener = l;
    ////caster = c;
    //stfd = fd;

    //skt = new SrsStSocket();
    ////rtsp = new SrsRtspStack(skt);
    //trd = new SrsSTCoroutine("28181tcpstream", this);
    
    //req = NULL;
    sdk = NULL;
    vjitter = new SrsRtspJitter();
    ajitter = new SrsRtspJitter();
    
    avc = new SrsRawH264Stream();
    aac = new SrsRawAacStream();
    acodec = new SrsRawAacStreamCodec();
    acache = new SrsRtspAudioCache();
    pprint = SrsPithyPrint::create_caster();
}

Srs28181StreamCore::~Srs28181StreamCore()
{
    close();
    //srs_close_stfd(stfd);
    
    srs_freep(video_rtp);
    srs_freep(audio_rtp);
    
    //srs_freep(trd);
    //srs_freep(skt);
    ////srs_freep(rtsp);
    
    srs_freep(sdk);
    ////srs_freep(req);
    
    srs_freep(vjitter);
    srs_freep(ajitter);
    srs_freep(acodec);
    srs_freep(acache);
    srs_freep(pprint);
}
/*
srs_error_t Srs28181StreamCore::init()
{
    srs_error_t err = srs_success;
    
    if ((err = skt->initialize(stfd)) != srs_success) {
        return srs_error_wrap(err, "socket initialize");
    }
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "rtsp connection");
    }
    
    return err;
}*/

/*
#define SRS_RECV_BUFFER_SIZE 1024*10
srs_error_t Srs28181TcpStreamConn::do_cycle()
{
    srs_error_t err = srs_success;

    // retrieve ip of client.
    std::string ip = srs_get_peer_ip(srs_netfd_fileno(stfd));
    if (ip.empty() && !_srs_config->empty_ip_ok()) {
        srs_warn("empty ip for fd=%d", srs_netfd_fileno(stfd));
    }
    srs_trace("28181: serve %s", ip.c_str());
    
    char buffer[SRS_RECV_BUFFER_SIZE];
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "28181 conn do_cycle");
        }

        ssize_t nb_read = 0;
        if ((err = skt->read(buffer, SRS_RECV_BUFFER_SIZE, &nb_read)) != srs_success) {
            return srs_error_wrap(err, "recv data");
        }

        decode_packet(buffer,nb_read);
    }

    // make it happy
    return err;
}*/

#define GB28181_STREAM
srs_error_t Srs28181StreamCore::on_stream_packet(SrsRtpPacket* pkt, int stream_id)
{
    srs_error_t err = srs_success;
    
    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    
    if (stream_id == video_id) {
        // rtsp tbn is ts tbn.
        int64_t pts = pkt->timestamp;
        if ((err = vjitter->correct(pts)) != srs_success) {
            return srs_error_wrap(err, "jitter");
        }
        
        // TODO: FIXME: set dts to pts, please finger out the right dts.
        int64_t dts = pts;
    #ifdef GB28181_STREAM
        return on_stream_video(pkt,dts,pts);
    #else
        return on_rtp_video(pkt, dts, pts);
    #endif

    } else {
        // rtsp tbn is ts tbn.
        int64_t pts = pkt->timestamp;
        if ((err = ajitter->correct(pts)) != srs_success) {
            return srs_error_wrap(err, "jitter");
        }
        
        return on_rtp_audio(pkt, pts);
    }
    
    return err;
}


srs_error_t Srs28181StreamCore::on_stream_video(SrsRtpPacket* pkt, int64_t dts, int64_t pts)
{

	//int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;

	if (pkt->tgtstream->length() <= 0) {
        srs_trace("28181streamcore - empty stream, will continue");
		return err;
	}

    SrsBuffer stream(pkt->tgtstream->bytes(), pkt->tgtstream->length());

	// send each frame.
	// TODO: bks: find i frame then return directory. dont need compare every bytes
	while (!stream.empty()) {
		char* frame = NULL;
		int frame_size = 0;
		
		if ((err = avc->annexb_demux(&stream, &frame, &frame_size)) != srs_success) {
            // i do not care
            srs_warn("28181streamcore - waring: no nalu in buffer.[%d]",srs_error_code(err));
            return srs_success;
			//return srs_error_wrap(err,"annexb demux");
		}

		// for highly reliable. Only give notification but exit
		if (frame_size <= 0) {
			srs_warn("h264 stream: frame_size <=0, and continue for next loop!");
			continue;
		}

		//if ((ret = avc->annexb_demux(stream, &frame, &frame_size)) != ERROR_SUCCESS) {
		//	return ret;
		//}

		// ignore others.
		// 5bits, 7.3.1 NAL unit syntax,
		// H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
		//  7: SPS, 8: PPS, 5: I Frame, 1: P Frame, 9: AUD
		SrsAvcNaluType nut = (SrsAvcNaluType)(frame[0] & 0x1f);
		if (nut != SrsAvcNaluTypeSPS && nut != SrsAvcNaluTypePPS //&& nut != SrsAvcNaluTypeSEI 
			&& nut != SrsAvcNaluTypeIDR && nut != SrsAvcNaluTypeNonIDR
			&& nut != SrsAvcNaluTypeAccessUnitDelimiter
			) {
			//srs_trace("h264-ps stream: Ignore this frame size=%d, dts=%d", frame_size, dts);
			continue;
		}


		// for sps
		if (avc->is_sps(frame, frame_size)) {
			std::string sps = "";
			if ((err = avc->sps_demux(frame, frame_size, sps)) != srs_success) {
				srs_error("h264-ps: invalied sps in dts=%d",dts);
				continue;
				//return ret;
			}

			if (h264_sps != sps) {
				h264_sps = sps;
				h264_sps_changed = true;
				h264_sps_pps_sent = false;
				srs_trace("h264-ps stream: set SPS frame size=%d, dts=%d", frame_size, dts);
			}
		}

		// for pps
		if (avc->is_pps(frame, frame_size)) {
			std::string pps = "";
			if ((err = avc->pps_demux(frame, frame_size, pps)) != srs_success) {
				srs_error("h264-ps: invalied sps in dts=%d", dts);
				continue;
				//return ret;
			}

			if (h264_pps != pps) {
				h264_pps = pps;
				h264_pps_changed = true;
				h264_sps_pps_sent = false;
				srs_trace("h264-ps stream: set PPS frame size=%d, dts=%d", frame_size, dts);
			}
		}

		// attention: now, we set sps/pps
		if (h264_sps_changed && h264_pps_changed) {

			h264_sps_changed = false;
			h264_pps_changed = false;
			h264_sps_pps_sent = true;

			if ((err = write_h264_sps_pps(dts / 90, pts / 90)) != srs_success) {
				srs_error("h264-ps stream: Re-write SPS-PPS Wrong! frame size=%d, dts=%d", frame_size, dts);
				return srs_error_wrap(err,"re-write sps-pps failed");
			}
			srs_warn("h264-ps stream: Re-write SPS-PPS Successful! frame size=%d, dts=%d", frame_size, dts);
		}

		//besson: make sure you control flows in one important function
		//dont spread controlers everythere. mpegts_upd is not a good example

		// attention: should ship sps/pps frame in every tsb rtp group
		// otherwise sps/pps will be written as ipb frame!
		if (h264_sps_pps_sent && nut != SrsAvcNaluTypeSPS && nut != SrsAvcNaluTypePPS) {
			if ((err = kickoff_audio_cache(pkt, dts)) != srs_success) {
				srs_warn("h264-ps stream: kickoff audio cache dts=%d", dts);
				return srs_error_wrap(err,"killoff audio cache failed");
			}

			// ibp frame.
			// TODO: FIXME: we should group all frames to a rtmp/flv message from one ts message.
			srs_info("h264-ps stream: demux avc ibp frame size=%d, dts=%d", frame_size, dts);
			if ((err = write_h264_ipb_frame(frame, frame_size, dts / 90, pts / 90)) != srs_success) {
				return srs_error_wrap(err,"write ibp failed");
			}
		}
	}//while send frame

	return err;
}

/*
srs_error_t Srs28181StreamCore::cycle()
{
    // serve the rtsp client.
    srs_error_t err = do_cycle();
    
    //caster->remove(this);
    if (err == srs_success) {
        srs_trace("client finished.");
    } else if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. code=%d", srs_error_code(err));
        srs_freep(err);
    }

    listener->remove_conn(this);
    
    //  do not need caster anymore
    // if (video_rtp) {
    //     caster->free_port(video_rtp->port(), video_rtp->port() + 1);
    // }
    
    // if (audio_rtp) {
    //     caster->free_port(audio_rtp->port(), audio_rtp->port() + 1);
    // }
    
    return err;
}
*/

srs_error_t Srs28181StreamCore::on_rtp_video(SrsRtpPacket* pkt, int64_t dts, int64_t pts)
{
    srs_error_t err = srs_success;
    
    if ((err = kickoff_audio_cache(pkt, dts)) != srs_success) {
        return srs_error_wrap(err, "kickoff audio cache");
    }
    
    char* bytes = pkt->payload->bytes();
    int length = pkt->payload->length();
    uint32_t fdts = (uint32_t)(dts / 90);
    uint32_t fpts = (uint32_t)(pts / 90);
    if ((err = write_h264_ipb_frame(bytes, length, fdts, fpts)) != srs_success) {
        return srs_error_wrap(err, "write ibp frame");
    }
    
    return err;
}

srs_error_t Srs28181StreamCore::on_rtp_audio(SrsRtpPacket* pkt, int64_t dts)
{
    srs_error_t err = srs_success;
    
    if ((err = kickoff_audio_cache(pkt, dts)) != srs_success) {
        return srs_error_wrap(err, "kickoff audio cache");
    }
    
    // cache current audio to kickoff.
    acache->dts = dts;
    acache->audio = pkt->audio;
    acache->payload = pkt->payload;
    
    pkt->audio = NULL;
    pkt->payload = NULL;
    
    return err;
}

srs_error_t Srs28181StreamCore::kickoff_audio_cache(SrsRtpPacket* pkt, int64_t dts)
{
    srs_error_t err = srs_success;
    
    // nothing to kick off.
    if (!acache->payload) {
        return err;
    }
    
    if (dts - acache->dts > 0 && acache->audio->nb_samples > 0) {
        int64_t delta = (dts - acache->dts) / acache->audio->nb_samples;
        for (int i = 0; i < acache->audio->nb_samples; i++) {
            char* frame = acache->audio->samples[i].bytes;
            int nb_frame = acache->audio->samples[i].size;
            int64_t timestamp = (acache->dts + delta * i) / 90;
            acodec->aac_packet_type = 1;
            if ((err = write_audio_raw_frame(frame, nb_frame, acodec, (uint32_t)timestamp)) != srs_success) {
                return srs_error_wrap(err, "write audio raw frame");
            }
        }
    }
    
    acache->dts = 0;
    srs_freep(acache->audio);
    srs_freep(acache->payload);
    
    return err;
}

// TODO: modify return type
// can decode raw rtp+h264 or rtp+ps+h264
#define PS_IN_RTP
int Srs28181StreamCore::decode_packet(char* buf, int nb_buf)
{
	int ret = 0;
	int status;

	pprint->elapse();

	if (true) {
		SrsBuffer stream(buf,nb_buf);

		//if ((ret = stream.initialize(buf, nb_buf)) != ERROR_SUCCESS) {
		//	return ret;
		//}

		SrsRtpPacket pkt;
		if ((ret = pkt.decode_v2(&stream)) != ERROR_SUCCESS) {
			srs_error("28181: decode rtp packet failed. ret=%d", ret);
			return ret;
		}

		if (pkt.chunked) {
			if (!cache_) {
				cache_ = new SrsRtpPacket();
			}
			cache_->copy(&pkt);
			cache_->payload->append(pkt.payload->bytes(), pkt.payload->length());

			/*
			if (!cache->completed && pprint->can_print()) {
			srs_trace("<- "SRS_CONSTS_LOG_STREAM_CASTER" rtsp: rtp chunked %dB, age=%d, vt=%d/%u, sts=%u/%#x/%#x, paylod=%dB",
			nb_buf, pprint->age(), cache->version, cache->payload_type, cache->sequence_number, cache->timestamp, cache->ssrc,
			cache->payload->length()
			);
			return ret;
			}*/

			//besson: correct rtp decode bug
			if (!cache_->completed) {
				return ret;
			}

		}
		else {
			// : NOTE:if u receive from middle or stream loss starting rtp, will also deal this uncompleted packet, 
			// the following progress will skip this ncompleted packet
			srs_freep(cache_);
			cache_ = new SrsRtpPacket();
			cache_->reap(&pkt);

		}
	}

	if (pprint->can_print()) {
		srs_trace("<- "SRS_CONSTS_LOG_STREAM_CASTER"  rtp #%d %dB, age=%d, vt=%d/%u, sts=%u/%u/%#x, paylod=%dB, chunked=%d",
			stream_id, nb_buf, pprint->age(), cache_->version, cache_->payload_type, cache_->sequence_number, cache_->timestamp, cache_->ssrc,
			cache_->payload->length(), cache_->chunked
		);
	}

	// always free it.
	SrsAutoFree(SrsRtpPacket, cache_);

#ifdef PS_IN_RTP
	// ps stream
	if ((status = cache_->decode_stream()) != ERROR_SUCCESS) {
		if (status == ERROR_RTP_PS_HK_PRIVATE_PROTO) {
			//private_proto = true;
			//only mention once
			srs_error(" rtp type 96 ps. stream_id:%d", stream_id);
		}
	}
#else
    // only rtp no ps
    cache_->tgtstream->append(cache_->payload->bytes(),cache_->payload->length());
#endif

    srs_error_t err = srs_success;
	if ((err = on_stream_packet(cache_, stream_id)) != srs_success) {
		srs_error("28181: process rtp packet failed. ret=%d",err->error_code(err) );
		return -1;
	}

	return ret;
}

// TODO: modify return type
int Srs28181StreamCore::decode_packet_v2(char* buf, int nb_buf)
{
	int ret = 0;
	int status;

	pprint->elapse();

	if (true) {
		SrsBuffer stream(buf,nb_buf);

		/*if ((ret = stream.initialize(buf, nb_buf)) != ERROR_SUCCESS) {
			return ret;
		}*/

		SrsRtpPacket pkt;
		if ((ret = pkt.decode_v2(&stream, boundary_type_)) != ERROR_SUCCESS) {
			srs_error("rtp auto decoder: decode rtp packet failed. ret=%d", ret);
			return ret;
		}

		if (pkt.chunked) {
			if (!cache_) {
				cache_ = new SrsRtpPacket();
			}

			if (boundary_type_ == MarkerBoundary) {
				cache_->copy(&pkt);
				cache_->payload->append(pkt.payload->bytes(), pkt.payload->length());
			}
			else if (boundary_type_ == TimestampBoundary) {

				// there is two conditions:
				// 1.ts changing every rtp packet
				// 2.ts changing every x rtp packets
				// in any case, we should first copy the cached rtp packet from last loop
				// cause we use ts boundary to decode rtp group, we determinte a group end after a new group beginng 
				if (first_rtp_tsb_enabled_) {
					first_rtp_tsb_enabled_ = false;

					if (!first_rtp_tsb_) {
						srs_error("rtp auto decoder: first_rtp_tsb_ is NULL!");
						ret = ERROR_RTP_PS_FIRST_TSB_LOSS;
						return ret;
						//srs_assert(first_rtp_tsb_==NULL);
					}

					cache_->copy(first_rtp_tsb_);
					cache_->payload->append(first_rtp_tsb_->payload->bytes(), first_rtp_tsb_->payload->length());
					srs_freep(first_rtp_tsb_);
				}

				if (pkt.timestamp != cache_->timestamp) {

					// if timestamp change, enable flag and cache the first new rtp packet in group
					first_rtp_tsb_enabled_ = true;

					srs_freep(first_rtp_tsb_);
					first_rtp_tsb_ = new SrsRtpPacket();
					first_rtp_tsb_->copy(&pkt);
					first_rtp_tsb_->payload->append(pkt.payload->bytes(), pkt.payload->length());

					cache_->completed = true;
				}
				else {
					cache_->copy(&pkt);
					cache_->payload->append(pkt.payload->bytes(), pkt.payload->length());
					cache_->completed = false;
				}
			}
			else {
				srs_error("Unkonown rtp boundary type!");
			}

			/*
			if (!cache->completed && pprint->can_print()) {
			srs_trace("<- "SRS_CONSTS_LOG_STREAM_CASTER" rtsp: rtp chunked %dB, age=%d, vt=%d/%u, sts=%u/%#x/%#x, paylod=%dB",
			nb_buf, pprint->age(), cache->version, cache->payload_type, cache->sequence_number, cache->timestamp, cache->ssrc,
			cache->payload->length()
			);
			return ret;
			}*/

			//  correct rtp decode bug
			if (!cache_->completed) {
				return ret;
			}

		}
		else {
			// besson: NOTE:if u receive from middle or stream loss starting rtp, will also deal this uncompleted packet, 
			// the following progress will skip this ncompleted packet
			srs_freep(cache_);
			cache_ = new SrsRtpPacket();
			cache_->reap(&pkt);

		}
	}

	if (pprint->can_print()) {
		srs_trace("<- "SRS_CONSTS_LOG_STREAM_CASTER"  rtp #%d %dB, age=%d, vt=%d/%u, sts=%u/%u/%#x, paylod=%dB, chunked=%d, boundary type=%s",
			stream_id, nb_buf, pprint->age(), cache_->version, cache_->payload_type, cache_->sequence_number, cache_->timestamp, cache_->ssrc,
			cache_->payload->length(), cache_->chunked, boundary_type_==MarkerBoundary?"MKR":"TSB"
		);
	}

	// always free it.
	SrsAutoFree(SrsRtpPacket, cache_);

	// ps stream
	if ((status = cache_->decode_stream()) != 0) {
		if (status == ERROR_RTP_PS_HK_PRIVATE_PROTO) {
			//private_proto = true;
			//only mention once
			srs_error(" rtp type 96 ps. private proto port:%d, stream_id:%d", 0, stream_id);
		}
	}

    srs_error_t err = srs_success;
	if ((err = on_stream_packet(cache_, stream_id)) != srs_success) {
		srs_error("rtp auto decoder: process rtp packet failed. ret=%d",srs_error_code(err) );
		//invalid_rtp_num_++;
		return -1;
	}

	return ret;
}

srs_error_t Srs28181StreamCore::write_sequence_header()
{
    srs_error_t err = srs_success;
    
    // use the current dts.
    int64_t dts = vjitter->timestamp() / 90;
    
    // send video sps/pps
    if ((err = write_h264_sps_pps((uint32_t)dts, (uint32_t)dts)) != srs_success) {
        return srs_error_wrap(err, "write sps/pps");
    }
    
    // generate audio sh by audio specific config.
    if (true) {
        std::string sh = aac_specific_config;
        
        SrsFormat* format = new SrsFormat();
        SrsAutoFree(SrsFormat, format);
        
        if ((err = format->on_aac_sequence_header((char*)sh.c_str(), (int)sh.length())) != srs_success) {
            return srs_error_wrap(err, "on aac sequence header");
        }
        
        SrsAudioCodecConfig* dec = format->acodec;
        
        acodec->sound_format = SrsAudioCodecIdAAC;
        acodec->sound_type = (dec->aac_channels == 2)? SrsAudioChannelsStereo : SrsAudioChannelsMono;
        acodec->sound_size = SrsAudioSampleBits16bit;
        acodec->aac_packet_type = 0;
        
        static int srs_aac_srates[] = {
            96000, 88200, 64000, 48000,
            44100, 32000, 24000, 22050,
            16000, 12000, 11025,  8000,
            7350,     0,     0,    0
        };
        switch (srs_aac_srates[dec->aac_sample_rate]) {
            case 11025:
                acodec->sound_rate = SrsAudioSampleRate11025;
                break;
            case 22050:
                acodec->sound_rate = SrsAudioSampleRate22050;
                break;
            case 44100:
                acodec->sound_rate = SrsAudioSampleRate44100;
                break;
            default:
                break;
        };
        
        if ((err = write_audio_raw_frame((char*)sh.data(), (int)sh.length(), acodec, (uint32_t)dts)) != srs_success) {
            return srs_error_wrap(err, "write audio raw frame");
        }
    }
    
    return err;
}

srs_error_t Srs28181StreamCore::write_h264_sps_pps(uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;
    
    // h264 raw to h264 packet.
    std::string sh;
    if ((err = avc->mux_sequence_header(h264_sps, h264_pps, dts, pts, sh)) != srs_success) {
        return srs_error_wrap(err, "mux sequence header");
    }
    
    // h264 packet to flv packet.
    int8_t frame_type = SrsVideoAvcFrameTypeKeyFrame;
    int8_t avc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux avc to flv");
    }
    
    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    if ((err = rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "write packet");
    }
    
    return err;
}

srs_error_t Srs28181StreamCore::write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;
    
    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
    
    // for IDR frame, the frame is keyframe.
    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;
    if (nal_unit_type == SrsAvcNaluTypeIDR) {
        frame_type = SrsVideoAvcFrameTypeKeyFrame;
    }
    
    std::string ibp;
    if ((err = avc->mux_ipb_frame(frame, frame_size, ibp)) != srs_success) {
        return srs_error_wrap(err, "mux ibp frame");
    }
    
    int8_t avc_packet_type = SrsVideoAvcFrameTraitNALU;
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(ibp, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux avc to flv");
    }
    
    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    return rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv);
}

srs_error_t Srs28181StreamCore::write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts)
{
    srs_error_t err = srs_success;
    
    char* data = NULL;
    int size = 0;
    if ((err = aac->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success) {
        return srs_error_wrap(err, "mux aac to flv");
    }
    
    return rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
}

srs_error_t Srs28181StreamCore::rtmp_write_packet(char type, uint32_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;
    
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    
    SrsSharedPtrMessage* msg = NULL;
    
    if ((err = srs_rtmp_create_msg(type, timestamp, data, size, sdk->sid(), &msg)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }
    srs_assert(msg);
    
    // send out encoded msg.
    if ((err = sdk->send_and_free_message(msg)) != srs_success) {
        close();
        return srs_error_wrap(err, "write message");
    }
    
    return err;
}

#define H264PS_STREAM_TEST
srs_error_t Srs28181StreamCore::connect()
{
    srs_error_t err = srs_success;
    
    // Ignore when connected.
    if (sdk) {
        return err;
    }
    
    // generate rtmp url to connect to.
    std::string url;
    //if (!req) {
    if(target_tcUrl != ""){
        std::string schema, host, vhost, app, param;
        int port;
        srs_discovery_tc_url(target_tcUrl, schema, host, vhost, app, stream_name, port, param);

        // generate output by template.
        std::string output = output_template;
        output = srs_string_replace(output, "[app]", app);
        output = srs_string_replace(output, "[stream]", stream_name);
        url = output;
    }

    // Fix Me: MUST use identified url in future
    url = target_tcUrl;
    
    srs_trace("28181 stream - target_tcurl:%s,stream_name:%s, url:%s",
        target_tcUrl.c_str(),stream_name.c_str(),url.c_str());

    // connect host.
    srs_utime_t cto = SRS_CONSTS_RTMP_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_PULSE;
    sdk = new SrsSimpleRtmpClient(url, cto, sto);
    
    if ((err = sdk->connect()) != srs_success) {
        close();
        return srs_error_wrap(err, "connect %s failed, cto=%dms, sto=%dms.", url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }
    
    // publish.
    if ((err = sdk->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        close();
        return srs_error_wrap(err, "publish %s failed", url.c_str());
    }
    
#ifdef H264PS_STREAM_TEST
	return err;
#else
    return write_sequence_header();
#endif
}

void Srs28181StreamCore::close()
{
    srs_freep(sdk);
}







Srs28181TcpStreamConn::Srs28181TcpStreamConn(Srs28181TcpStreamListener* l, srs_netfd_t fd, std::string o)
{
    output_template = o;

    target_tcUrl = "rtmp://127.0.0.1:7935/live/test";//"rtmp://127.0.0.1:" + "7935" + "/live/test";
	output_template = "rtmp://127.0.0.1:7935/[app]/[stream]";
    
    session = "";
    video_rtp = NULL;
    audio_rtp = NULL;

    // TODO: set stream_id when connected
    stream_id = 50125;
    video_id = stream_id;
    
    listener = l;
    //caster = c;
    stfd = fd;
    skt = new SrsStSocket();
    //rtsp = new SrsRtspStack(skt);
    trd = new SrsSTCoroutine("28181tcpstream", this);
    
    //req = NULL;
    sdk = NULL;
    vjitter = new SrsRtspJitter();
    ajitter = new SrsRtspJitter();
    
    avc = new SrsRawH264Stream();
    aac = new SrsRawAacStream();
    acodec = new SrsRawAacStreamCodec();
    acache = new SrsRtspAudioCache();
    pprint = SrsPithyPrint::create_caster();
}

Srs28181TcpStreamConn::~Srs28181TcpStreamConn()
{
    close();
    
    srs_close_stfd(stfd);
    
    srs_freep(video_rtp);
    srs_freep(audio_rtp);
    
    srs_freep(trd);
    srs_freep(skt);
    //srs_freep(rtsp);
    
    srs_freep(sdk);
    //srs_freep(req);
    
    srs_freep(vjitter);
    srs_freep(ajitter);
    srs_freep(acodec);
    srs_freep(acache);
    srs_freep(pprint);
}

srs_error_t Srs28181TcpStreamConn::init()
{
    srs_error_t err = srs_success;
    
    if ((err = skt->initialize(stfd)) != srs_success) {
        return srs_error_wrap(err, "socket initialize");
    }
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "rtsp connection");
    }
    
    return err;
}

#define SRS_RECV_BUFFER_SIZE 1024*10
srs_error_t Srs28181TcpStreamConn::do_cycle()
{
    srs_error_t err = srs_success;

    // retrieve ip of client.
    std::string ip = srs_get_peer_ip(srs_netfd_fileno(stfd));
    if (ip.empty() && !_srs_config->empty_ip_ok()) {
        srs_warn("empty ip for fd=%d", srs_netfd_fileno(stfd));
    }
    srs_trace("28181: serve %s", ip.c_str());
    
    char buffer[SRS_RECV_BUFFER_SIZE];
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "28181 conn do_cycle");
        }

        ssize_t nb_read = 0;
        if ((err = skt->read(buffer, SRS_RECV_BUFFER_SIZE, &nb_read)) != srs_success) {
            return srs_error_wrap(err, "recv data");
        }

        decode_packet(buffer,nb_read);
    }

    // make it happy
    return err;
}

//#define GB28181_STREAM
srs_error_t Srs28181TcpStreamConn::on_rtp_packet(SrsRtpPacket* pkt, int stream_id)
{
    srs_error_t err = srs_success;
    
    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    
    if (stream_id == video_id) {
        // rtsp tbn is ts tbn.
        int64_t pts = pkt->timestamp;
        if ((err = vjitter->correct(pts)) != srs_success) {
            return srs_error_wrap(err, "jitter");
        }
        
        // TODO: FIXME: set dts to pts, please finger out the right dts.
        int64_t dts = pts;
    #ifdef GB28181_STREAM
        return on_rtp_video_adv(pkt,dts,pts);
    #else
        return on_rtp_video(pkt, dts, pts);
    #endif

    } else {
        // rtsp tbn is ts tbn.
        int64_t pts = pkt->timestamp;
        if ((err = ajitter->correct(pts)) != srs_success) {
            return srs_error_wrap(err, "jitter");
        }
        
        return on_rtp_audio(pkt, pts);
    }
    
    return err;
}


srs_error_t Srs28181TcpStreamConn::on_rtp_video_adv(SrsRtpPacket* pkt, int64_t dts, int64_t pts)
{

	//int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;

	if (pkt->tgtstream->length() <= 0) {
		return srs_error_new(13002,"tetstream not enough");
	}

    SrsBuffer stream(pkt->tgtstream->bytes(), pkt->tgtstream->length());
	/*SrsBuffer stream;
	if ((ret = stream.initialize(pkt->tgtstream->bytes(), pkt->tgtstream->length()))!=ERROR_SUCCESS) {
		srs_trace("h264-ps stream: inValid Frame Size, frame size=%d, dts=%d", pkt->tgtstream->length(), dts);
		return ret;
	}*/

	// send each frame.
	// TODO: bks: find i frame then return directory. dont need compare every bytes
	while (!stream.empty()) {
		char* frame = NULL;
		int frame_size = 0;
		
		if ((err = avc->annexb_demux(&stream, &frame, &frame_size)) != srs_success) {
			return srs_error_wrap(err,"annexb demux");
		}

		// for highly reliable. Only give notification but exit
		if (frame_size <= 0) {
			srs_warn("h264 stream: frame_size <=0, and continue for next loop!");
			continue;
		}

		//if ((ret = avc->annexb_demux(stream, &frame, &frame_size)) != ERROR_SUCCESS) {
		//	return ret;
		//}

		// ignore others.
		// 5bits, 7.3.1 NAL unit syntax,
		// H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
		//  7: SPS, 8: PPS, 5: I Frame, 1: P Frame, 9: AUD
		SrsAvcNaluType nut = (SrsAvcNaluType)(frame[0] & 0x1f);
		if (nut != SrsAvcNaluTypeSPS && nut != SrsAvcNaluTypePPS //&& nut != SrsAvcNaluTypeSEI 
			&& nut != SrsAvcNaluTypeIDR && nut != SrsAvcNaluTypeNonIDR
			&& nut != SrsAvcNaluTypeAccessUnitDelimiter
			) {
			//srs_trace("h264-ps stream: Ignore this frame size=%d, dts=%d", frame_size, dts);
			continue;
		}


		// for sps
		if (avc->is_sps(frame, frame_size)) {
			std::string sps = "";
			if ((err = avc->sps_demux(frame, frame_size, sps)) != srs_success) {
				srs_error("h264-ps: invalied sps in dts=%d",dts);
				continue;
				//return ret;
			}

			if (h264_sps != sps) {
				h264_sps = sps;
				h264_sps_changed = true;
				h264_sps_pps_sent = false;
				srs_trace("h264-ps stream: set SPS frame size=%d, dts=%d", frame_size, dts);
			}
		}

		// for pps
		if (avc->is_pps(frame, frame_size)) {
			std::string pps = "";
			if ((err = avc->pps_demux(frame, frame_size, pps)) != srs_success) {
				srs_error("h264-ps: invalied sps in dts=%d", dts);
				continue;
				//return ret;
			}

			if (h264_pps != pps) {
				h264_pps = pps;
				h264_pps_changed = true;
				h264_sps_pps_sent = false;
				srs_trace("h264-ps stream: set PPS frame size=%d, dts=%d", frame_size, dts);
			}
		}

		// attention: now, we set sps/pps
		if (h264_sps_changed && h264_pps_changed) {

			h264_sps_changed = false;
			h264_pps_changed = false;
			h264_sps_pps_sent = true;

			if ((err = write_h264_sps_pps(dts / 90, pts / 90)) != srs_success) {
				srs_error("h264-ps stream: Re-write SPS-PPS Wrong! frame size=%d, dts=%d", frame_size, dts);
				return srs_error_wrap(err,"re-write sps-pps failed");
			}
			srs_warn("h264-ps stream: Re-write SPS-PPS Successful! frame size=%d, dts=%d", frame_size, dts);
		}

		//besson: make sure you control flows in one important function
		//dont spread controlers everythere. mpegts_upd is not a good example

		// attention: should ship sps/pps frame in every tsb rtp group
		// otherwise sps/pps will be written as ipb frame!
		if (h264_sps_pps_sent && nut != SrsAvcNaluTypeSPS && nut != SrsAvcNaluTypePPS) {
			if ((err = kickoff_audio_cache(pkt, dts)) != srs_success) {
				srs_warn("h264-ps stream: kickoff audio cache dts=%d", dts);
				return srs_error_wrap(err,"killoff audio cache failed");
			}

			// ibp frame.
			// TODO: FIXME: we should group all frames to a rtmp/flv message from one ts message.
			srs_info("h264-ps stream: demux avc ibp frame size=%d, dts=%d", frame_size, dts);
			if ((err = write_h264_ipb_frame(frame, frame_size, dts / 90, pts / 90)) != srs_success) {
				return srs_error_wrap(err,"write ibp failed");
			}
		}
	}//while send frame

	return err;
}

srs_error_t Srs28181TcpStreamConn::cycle()
{
    // serve the rtsp client.
    srs_error_t err = do_cycle();
    
    //caster->remove(this);
    if (err == srs_success) {
        srs_trace("client finished.");
    } else if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. code=%d", srs_error_code(err));
        srs_freep(err);
    }

    listener->remove_conn(this);
    
    /* do not need caster anymore
    if (video_rtp) {
        caster->free_port(video_rtp->port(), video_rtp->port() + 1);
    }
    
    if (audio_rtp) {
        caster->free_port(audio_rtp->port(), audio_rtp->port() + 1);
    }*/
    
    return err;
}

srs_error_t Srs28181TcpStreamConn::on_rtp_video(SrsRtpPacket* pkt, int64_t dts, int64_t pts)
{
    srs_error_t err = srs_success;
    
    if ((err = kickoff_audio_cache(pkt, dts)) != srs_success) {
        return srs_error_wrap(err, "kickoff audio cache");
    }
    
    char* bytes = pkt->payload->bytes();
    int length = pkt->payload->length();
    uint32_t fdts = (uint32_t)(dts / 90);
    uint32_t fpts = (uint32_t)(pts / 90);
    if ((err = write_h264_ipb_frame(bytes, length, fdts, fpts)) != srs_success) {
        return srs_error_wrap(err, "write ibp frame");
    }
    
    return err;
}

srs_error_t Srs28181TcpStreamConn::on_rtp_audio(SrsRtpPacket* pkt, int64_t dts)
{
    srs_error_t err = srs_success;
    
    if ((err = kickoff_audio_cache(pkt, dts)) != srs_success) {
        return srs_error_wrap(err, "kickoff audio cache");
    }
    
    // cache current audio to kickoff.
    acache->dts = dts;
    acache->audio = pkt->audio;
    acache->payload = pkt->payload;
    
    pkt->audio = NULL;
    pkt->payload = NULL;
    
    return err;
}

srs_error_t Srs28181TcpStreamConn::kickoff_audio_cache(SrsRtpPacket* pkt, int64_t dts)
{
    srs_error_t err = srs_success;
    
    // nothing to kick off.
    if (!acache->payload) {
        return err;
    }
    
    if (dts - acache->dts > 0 && acache->audio->nb_samples > 0) {
        int64_t delta = (dts - acache->dts) / acache->audio->nb_samples;
        for (int i = 0; i < acache->audio->nb_samples; i++) {
            char* frame = acache->audio->samples[i].bytes;
            int nb_frame = acache->audio->samples[i].size;
            int64_t timestamp = (acache->dts + delta * i) / 90;
            acodec->aac_packet_type = 1;
            if ((err = write_audio_raw_frame(frame, nb_frame, acodec, (uint32_t)timestamp)) != srs_success) {
                return srs_error_wrap(err, "write audio raw frame");
            }
        }
    }
    
    acache->dts = 0;
    srs_freep(acache->audio);
    srs_freep(acache->payload);
    
    return err;
}

// TODO: modify return type
// can decode raw rtp+h264 or rtp+ps+h264
int Srs28181TcpStreamConn::decode_packet(char* buf, int nb_buf)
{
	int ret = 0;
	int status;

	pprint->elapse();

	if (true) {
		SrsBuffer stream(buf,nb_buf);

		//if ((ret = stream.initialize(buf, nb_buf)) != ERROR_SUCCESS) {
		//	return ret;
		//}

		SrsRtpPacket pkt;
		if ((ret = pkt.decode_v2(&stream)) != ERROR_SUCCESS) {
			srs_error("28181: decode rtp packet failed. ret=%d", ret);
			return ret;
		}

		if (pkt.chunked) {
			if (!cache_) {
				cache_ = new SrsRtpPacket();
			}
			cache_->copy(&pkt);
			cache_->payload->append(pkt.payload->bytes(), pkt.payload->length());

			/*
			if (!cache->completed && pprint->can_print()) {
			srs_trace("<- "SRS_CONSTS_LOG_STREAM_CASTER" rtsp: rtp chunked %dB, age=%d, vt=%d/%u, sts=%u/%#x/%#x, paylod=%dB",
			nb_buf, pprint->age(), cache->version, cache->payload_type, cache->sequence_number, cache->timestamp, cache->ssrc,
			cache->payload->length()
			);
			return ret;
			}*/

			//besson: correct rtp decode bug
			if (!cache_->completed) {
				return ret;
			}

		}
		else {
			// : NOTE:if u receive from middle or stream loss starting rtp, will also deal this uncompleted packet, 
			// the following progress will skip this ncompleted packet
			srs_freep(cache_);
			cache_ = new SrsRtpPacket();
			cache_->reap(&pkt);

		}
	}

    // TODO: set stream_id when connected
    stream_id = 50125;

	if (pprint->can_print()) {
		srs_trace("<- "SRS_CONSTS_LOG_STREAM_CASTER"  rtp #%d %dB, age=%d, vt=%d/%u, sts=%u/%u/%#x, paylod=%dB, chunked=%d",
			stream_id, nb_buf, pprint->age(), cache_->version, cache_->payload_type, cache_->sequence_number, cache_->timestamp, cache_->ssrc,
			cache_->payload->length(), cache_->chunked
		);
	}

	// always free it.
	SrsAutoFree(SrsRtpPacket, cache_);

#ifdef PS_IN_RTP
	// ps stream
	if ((status = cache_->decode_stream()) != ERROR_SUCCESS) {
		if (status == ERROR_RTP_PS_HK_PRIVATE_PROTO) {
			//private_proto = true;
			//only mention once
			srs_error(" rtp type 96 ps. stream_id:%d", stream_id);
		}
	}
#endif


    srs_error_t err = srs_success;
	if ((err = on_rtp_packet(cache_, stream_id)) != srs_success) {
		srs_error("28181: process rtp packet failed. ret=%d", ret);
		return -1;
	}

	return ret;
}

// TODO: modify return type
int Srs28181TcpStreamConn::decode_packet_v2(char* buf, int nb_buf)
{
	int ret = 0;
	int status;

	pprint->elapse();

	if (true) {
		SrsBuffer stream(buf,nb_buf);

		/*if ((ret = stream.initialize(buf, nb_buf)) != ERROR_SUCCESS) {
			return ret;
		}*/

		SrsRtpPacket pkt;
		if ((ret = pkt.decode_v2(&stream, boundary_type_)) != ERROR_SUCCESS) {
			srs_error("rtp auto decoder: decode rtp packet failed. ret=%d", ret);
			return ret;
		}

		if (pkt.chunked) {
			if (!cache_) {
				cache_ = new SrsRtpPacket();
			}

			if (boundary_type_ == MarkerBoundary) {
				cache_->copy(&pkt);
				cache_->payload->append(pkt.payload->bytes(), pkt.payload->length());
			}
			else if (boundary_type_ == TimestampBoundary) {

				// there is two conditions:
				// 1.ts changing every rtp packet
				// 2.ts changing every x rtp packets
				// in any case, we should first copy the cached rtp packet from last loop
				// cause we use ts boundary to decode rtp group, we determinte a group end after a new group beginng 
				if (first_rtp_tsb_enabled_) {
					first_rtp_tsb_enabled_ = false;

					if (!first_rtp_tsb_) {
						srs_error("rtp auto decoder: first_rtp_tsb_ is NULL!");
						ret = ERROR_RTP_PS_FIRST_TSB_LOSS;
						return ret;
						//srs_assert(first_rtp_tsb_==NULL);
					}

					cache_->copy(first_rtp_tsb_);
					cache_->payload->append(first_rtp_tsb_->payload->bytes(), first_rtp_tsb_->payload->length());
					srs_freep(first_rtp_tsb_);
				}

				if (pkt.timestamp != cache_->timestamp) {

					// if timestamp change, enable flag and cache the first new rtp packet in group
					first_rtp_tsb_enabled_ = true;

					srs_freep(first_rtp_tsb_);
					first_rtp_tsb_ = new SrsRtpPacket();
					first_rtp_tsb_->copy(&pkt);
					first_rtp_tsb_->payload->append(pkt.payload->bytes(), pkt.payload->length());

					cache_->completed = true;
				}
				else {
					cache_->copy(&pkt);
					cache_->payload->append(pkt.payload->bytes(), pkt.payload->length());
					cache_->completed = false;
				}
			}
			else {
				srs_error("Unkonown rtp boundary type!");
			}

			/*
			if (!cache->completed && pprint->can_print()) {
			srs_trace("<- "SRS_CONSTS_LOG_STREAM_CASTER" rtsp: rtp chunked %dB, age=%d, vt=%d/%u, sts=%u/%#x/%#x, paylod=%dB",
			nb_buf, pprint->age(), cache->version, cache->payload_type, cache->sequence_number, cache->timestamp, cache->ssrc,
			cache->payload->length()
			);
			return ret;
			}*/

			//besson: correct rtp decode bug
			if (!cache_->completed) {
				return ret;
			}

		}
		else {
			// besson: NOTE:if u receive from middle or stream loss starting rtp, will also deal this uncompleted packet, 
			// the following progress will skip this ncompleted packet
			srs_freep(cache_);
			cache_ = new SrsRtpPacket();
			cache_->reap(&pkt);

		}
	}

	if (pprint->can_print()) {
		srs_trace("<- "SRS_CONSTS_LOG_STREAM_CASTER"  rtp #%d %dB, age=%d, vt=%d/%u, sts=%u/%u/%#x, paylod=%dB, chunked=%d, boundary type=%s",
			stream_id, nb_buf, pprint->age(), cache_->version, cache_->payload_type, cache_->sequence_number, cache_->timestamp, cache_->ssrc,
			cache_->payload->length(), cache_->chunked, boundary_type_==MarkerBoundary?"MKR":"TSB"
		);
	}

	// always free it.
	SrsAutoFree(SrsRtpPacket, cache_);

	// ps stream
	if ((status = cache_->decode_stream()) != 0) {
		if (status == ERROR_RTP_PS_HK_PRIVATE_PROTO) {
			//private_proto = true;
			//only mention once
			srs_error(" rtp type 96 ps. private proto port:%d, stream_id:%d", 0, stream_id);
		}
	}

    srs_error_t err = srs_success;
	if ((err = on_rtp_packet(cache_, stream_id)) != srs_success) {
		srs_error("rtp auto decoder: process rtp packet failed. ret=%d", srs_error_code(err));
		//invalid_rtp_num_++;
		return -1;
	}

	return ret;
}

srs_error_t Srs28181TcpStreamConn::write_sequence_header()
{
    srs_error_t err = srs_success;
    
    // use the current dts.
    int64_t dts = vjitter->timestamp() / 90;
    
    // send video sps/pps
    if ((err = write_h264_sps_pps((uint32_t)dts, (uint32_t)dts)) != srs_success) {
        return srs_error_wrap(err, "write sps/pps");
    }
    
    // generate audio sh by audio specific config.
    if (true) {
        std::string sh = aac_specific_config;
        
        SrsFormat* format = new SrsFormat();
        SrsAutoFree(SrsFormat, format);
        
        if ((err = format->on_aac_sequence_header((char*)sh.c_str(), (int)sh.length())) != srs_success) {
            return srs_error_wrap(err, "on aac sequence header");
        }
        
        SrsAudioCodecConfig* dec = format->acodec;
        
        acodec->sound_format = SrsAudioCodecIdAAC;
        acodec->sound_type = (dec->aac_channels == 2)? SrsAudioChannelsStereo : SrsAudioChannelsMono;
        acodec->sound_size = SrsAudioSampleBits16bit;
        acodec->aac_packet_type = 0;
        
        static int srs_aac_srates[] = {
            96000, 88200, 64000, 48000,
            44100, 32000, 24000, 22050,
            16000, 12000, 11025,  8000,
            7350,     0,     0,    0
        };
        switch (srs_aac_srates[dec->aac_sample_rate]) {
            case 11025:
                acodec->sound_rate = SrsAudioSampleRate11025;
                break;
            case 22050:
                acodec->sound_rate = SrsAudioSampleRate22050;
                break;
            case 44100:
                acodec->sound_rate = SrsAudioSampleRate44100;
                break;
            default:
                break;
        };
        
        if ((err = write_audio_raw_frame((char*)sh.data(), (int)sh.length(), acodec, (uint32_t)dts)) != srs_success) {
            return srs_error_wrap(err, "write audio raw frame");
        }
    }
    
    return err;
}

srs_error_t Srs28181TcpStreamConn::write_h264_sps_pps(uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;
    
    // h264 raw to h264 packet.
    std::string sh;
    if ((err = avc->mux_sequence_header(h264_sps, h264_pps, dts, pts, sh)) != srs_success) {
        return srs_error_wrap(err, "mux sequence header");
    }
    
    // h264 packet to flv packet.
    int8_t frame_type = SrsVideoAvcFrameTypeKeyFrame;
    int8_t avc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux avc to flv");
    }
    
    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    if ((err = rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "write packet");
    }
    
    return err;
}

srs_error_t Srs28181TcpStreamConn::write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;
    
    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
    
    // for IDR frame, the frame is keyframe.
    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;
    if (nal_unit_type == SrsAvcNaluTypeIDR) {
        frame_type = SrsVideoAvcFrameTypeKeyFrame;
    }
    
    std::string ibp;
    if ((err = avc->mux_ipb_frame(frame, frame_size, ibp)) != srs_success) {
        return srs_error_wrap(err, "mux ibp frame");
    }
    
    int8_t avc_packet_type = SrsVideoAvcFrameTraitNALU;
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(ibp, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux avc to flv");
    }
    
    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    return rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv);
}

srs_error_t Srs28181TcpStreamConn::write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts)
{
    srs_error_t err = srs_success;
    
    char* data = NULL;
    int size = 0;
    if ((err = aac->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success) {
        return srs_error_wrap(err, "mux aac to flv");
    }
    
    return rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
}

srs_error_t Srs28181TcpStreamConn::rtmp_write_packet(char type, uint32_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;
    
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    
    SrsSharedPtrMessage* msg = NULL;
    
    if ((err = srs_rtmp_create_msg(type, timestamp, data, size, sdk->sid(), &msg)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }
    srs_assert(msg);
    
    // send out encoded msg.
    if ((err = sdk->send_and_free_message(msg)) != srs_success) {
        close();
        return srs_error_wrap(err, "write message");
    }
    
    return err;
}

srs_error_t Srs28181TcpStreamConn::connect()
{
    srs_error_t err = srs_success;
    
    // Ignore when connected.
    if (sdk) {
        return err;
    }
    
    // generate rtmp url to connect to.
    std::string url;
    //if (!req) {
    if(target_tcUrl != ""){
        std::string schema, host, vhost, app, param;
        int port;
        srs_discovery_tc_url(target_tcUrl, schema, host, vhost, app, stream_name, port, param);

        // generate output by template.
        std::string output = output_template;
        output = srs_string_replace(output, "[app]", app);
        output = srs_string_replace(output, "[stream]", stream_name);
        url = output;
    }
    
    srs_trace("28181 stream - target_tcurl:%s,stream_name:%s, url:%s",
        target_tcUrl.c_str(),stream_name.c_str(),url.c_str());

    // connect host.
    srs_utime_t cto = SRS_CONSTS_RTMP_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_PULSE;
    sdk = new SrsSimpleRtmpClient(url, cto, sto);
    
    if ((err = sdk->connect()) != srs_success) {
        close();
        return srs_error_wrap(err, "connect %s failed, cto=%dms, sto=%dms.", url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }
    
    // publish.
    if ((err = sdk->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        close();
        return srs_error_wrap(err, "publish %s failed", url.c_str());
    }
    
    return write_sequence_header();
}

void Srs28181TcpStreamConn::close()
{
    srs_freep(sdk);
}