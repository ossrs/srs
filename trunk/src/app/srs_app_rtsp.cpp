/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_app_rtsp.hpp>

#include <algorithm>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_rtsp_stack.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_log.hpp>
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

#ifdef SRS_AUTO_STREAM_CASTER

SrsRtpConn::SrsRtpConn(SrsRtspConn* r, int p, int sid)
{
    rtsp = r;
    _port = p;
    stream_id = sid;
    // TODO: support listen at <[ip:]port>
    listener = new SrsUdpListener(this, "0.0.0.0", p);
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

int SrsRtpConn::listen()
{
    return listener->listen();
}

int SrsRtpConn::on_udp_packet(sockaddr_in* from, char* buf, int nb_buf)
{
    int ret = ERROR_SUCCESS;

    pprint->elapse();

    if (true) {
        SrsBuffer stream;

        if ((ret = stream.initialize(buf, nb_buf)) != ERROR_SUCCESS) {
            return ret;
        }
    
        SrsRtpPacket pkt;
        if ((ret = pkt.decode(&stream)) != ERROR_SUCCESS) {
            srs_error("rtsp: decode rtp packet failed. ret=%d", ret);
            return ret;
        }

        if (pkt.chunked) {
            if (!cache) {
                cache = new SrsRtpPacket();
            }
            cache->copy(&pkt);
            cache->payload->append(pkt.payload->bytes(), pkt.payload->length());
            if (!cache->completed && pprint->can_print()) {
                srs_trace("<- "SRS_CONSTS_LOG_STREAM_CASTER" rtsp: rtp chunked %dB, age=%d, vt=%d/%u, sts=%u/%#x/%#x, paylod=%dB", 
                    nb_buf, pprint->age(), cache->version, cache->payload_type, cache->sequence_number, cache->timestamp, cache->ssrc, 
                    cache->payload->length()
                );
                return ret;
            }
        } else {
            srs_freep(cache);
            cache = new SrsRtpPacket();
            cache->reap(&pkt);
        }
    }

    if (pprint->can_print()) {
        srs_trace("<- "SRS_CONSTS_LOG_STREAM_CASTER" rtsp: rtp #%d %dB, age=%d, vt=%d/%u, sts=%u/%u/%#x, paylod=%dB, chunked=%d", 
            stream_id, nb_buf, pprint->age(), cache->version, cache->payload_type, cache->sequence_number, cache->timestamp, cache->ssrc, 
            cache->payload->length(), cache->chunked
        );
    }

    // always free it.
    SrsAutoFree(SrsRtpPacket, cache);
    
    if ((ret = rtsp->on_rtp_packet(cache, stream_id)) != ERROR_SUCCESS) {
        srs_error("rtsp: process rtp packet failed. ret=%d", ret);
        return ret;
    }

    return ret;
}

SrsRtspAudioCache::SrsRtspAudioCache()
{
    dts = 0;
    audio_samples = NULL;
    payload = NULL;
}

SrsRtspAudioCache::~SrsRtspAudioCache()
{
    srs_freep(audio_samples);
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

int SrsRtspJitter::correct(int64_t& ts)
{
    int ret = ERROR_SUCCESS;

    if (previous_timestamp == 0) {
        previous_timestamp = ts;
    }

    delta = srs_max(0, ts - previous_timestamp);
    if (delta > 90000) {
        delta = 0;
    }

    previous_timestamp = ts;

    ts = pts + delta;
    pts = ts;    

    return ret;
}

SrsRtspConn::SrsRtspConn(SrsRtspCaster* c, st_netfd_t fd, std::string o)
{
    output_template = o;

    session = "";
    video_rtp = NULL;
    audio_rtp = NULL;

    caster = c;
    stfd = fd;
    skt = new SrsStSocket(fd);
    rtsp = new SrsRtspStack(skt);
    trd = new SrsOneCycleThread("rtsp", this);

    req = NULL;
    io = NULL;
    client = NULL;
    stream_id = 0;
    vjitter = new SrsRtspJitter();
    ajitter = new SrsRtspJitter();

    avc = new SrsRawH264Stream();
    aac = new SrsRawAacStream();
    acodec = new SrsRawAacStreamCodec();
    acache = new SrsRtspAudioCache();
}

SrsRtspConn::~SrsRtspConn()
{
    srs_close_stfd(stfd);

    srs_freep(video_rtp);
    srs_freep(audio_rtp);

    srs_freep(trd);
    srs_freep(skt);
    srs_freep(rtsp);
    
    srs_freep(client);
    srs_freep(io);
    srs_freep(req);

    srs_freep(vjitter);
    srs_freep(ajitter);
    srs_freep(acodec);
    srs_freep(acache);
}

int SrsRtspConn::serve()
{
    return trd->start();
}

int SrsRtspConn::do_cycle()
{
    int ret = ERROR_SUCCESS;

    // retrieve ip of client.
    std::string ip = srs_get_peer_ip(st_netfd_fileno(stfd));
    srs_trace("rtsp: serve %s", ip.c_str());

    // consume all rtsp messages.
    for (;;) {
        SrsRtspRequest* req = NULL;
        if ((ret = rtsp->recv_message(&req)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("rtsp: recv request failed. ret=%d", ret);
            }
            return ret;
        }
        SrsAutoFree(SrsRtspRequest, req);
        srs_info("rtsp: got rtsp request");

        if (req->is_options()) {
            SrsRtspOptionsResponse* res = new SrsRtspOptionsResponse(req->seq);
            res->session = session;
            if ((ret = rtsp->send_message(res)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("rtsp: send OPTIONS response failed. ret=%d", ret);
                }
                return ret;
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

            SrsRtspResponse* res = new SrsRtspResponse(req->seq);
            res->session = session;
            if ((ret = rtsp->send_message(res)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("rtsp: send ANNOUNCE response failed. ret=%d", ret);
                }
                return ret;
            }
        } else if (req->is_setup()) {
            srs_assert(req->transport);
            int lpm = 0;
            if ((ret = caster->alloc_port(&lpm)) != ERROR_SUCCESS) {
                srs_error("rtsp: alloc port failed. ret=%d", ret);
                return ret;
            }

            SrsRtpConn* rtp = NULL;
            if (req->stream_id == video_id) {
                srs_freep(video_rtp);
                rtp = video_rtp = new SrsRtpConn(this, lpm, video_id);
            } else {
                srs_freep(audio_rtp);
                rtp = audio_rtp = new SrsRtpConn(this, lpm, audio_id);
            }
            if ((ret = rtp->listen()) != ERROR_SUCCESS) {
                srs_error("rtsp: rtp listen at port=%d failed. ret=%d", lpm, ret);
                return ret;
            }
            srs_trace("rtsp: #%d %s over %s/%s/%s %s client-port=%d-%d, server-port=%d-%d", 
                req->stream_id, (req->stream_id == video_id)? "Video":"Audio", 
                req->transport->transport.c_str(), req->transport->profile.c_str(), req->transport->lower_transport.c_str(), 
                req->transport->cast_type.c_str(), req->transport->client_port_min, req->transport->client_port_max, 
                lpm, lpm + 1
            );

            // create session.
            if (session.empty()) {
                session = "O9EaZ4bf"; // TODO: FIXME: generate session id.
            }

            SrsRtspSetupResponse* res = new SrsRtspSetupResponse(req->seq);
            res->client_port_min = req->transport->client_port_min;
            res->client_port_max = req->transport->client_port_max;
            res->local_port_min = lpm;
            res->local_port_max = lpm + 1;
            res->session = session;
            if ((ret = rtsp->send_message(res)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("rtsp: send SETUP response failed. ret=%d", ret);
                }
                return ret;
            }
        } else if (req->is_record()) {
            SrsRtspResponse* res = new SrsRtspResponse(req->seq);
            res->session = session;
            if ((ret = rtsp->send_message(res)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("rtsp: send SETUP response failed. ret=%d", ret);
                }
                return ret;
            }
        }
    }

    return ret;
}

int SrsRtspConn::on_rtp_packet(SrsRtpPacket* pkt, int stream_id)
{
    int ret = ERROR_SUCCESS;

    // ensure rtmp connected.
    if ((ret = connect()) != ERROR_SUCCESS) {
        return ret;
    }

    if (stream_id == video_id) {
        // rtsp tbn is ts tbn.
        int64_t pts = pkt->timestamp;
        if ((ret = vjitter->correct(pts)) != ERROR_SUCCESS) {
            srs_error("rtsp: correct by jitter failed. ret=%d", ret);
            return ret;
        }

        // TODO: FIXME: set dts to pts, please finger out the right dts.
        int64_t dts = pts;

        return on_rtp_video(pkt, dts, pts);
    } else {
        // rtsp tbn is ts tbn.
        int64_t pts = pkt->timestamp;
        if ((ret = ajitter->correct(pts)) != ERROR_SUCCESS) {
            srs_error("rtsp: correct by jitter failed. ret=%d", ret);
            return ret;
        }

        return on_rtp_audio(pkt, pts);
    }

    return ret;
}

int SrsRtspConn::cycle()
{
    // serve the rtsp client.
    int ret = do_cycle();
    
    // if socket io error, set to closed.
    if (srs_is_client_gracefully_close(ret)) {
        ret = ERROR_SOCKET_CLOSED;
    }
    
    // success.
    if (ret == ERROR_SUCCESS) {
        srs_trace("client finished.");
    }
    
    // client close peer.
    if (ret == ERROR_SOCKET_CLOSED) {
        srs_warn("client disconnect peer. ret=%d", ret);
    }

    return ERROR_SUCCESS;
}

void SrsRtspConn::on_thread_stop()
{
    if (video_rtp) {
        caster->free_port(video_rtp->port(), video_rtp->port() + 1);
    }

    if (audio_rtp) {
        caster->free_port(audio_rtp->port(), audio_rtp->port() + 1);
    }

    caster->remove(this);
}

int SrsRtspConn::on_rtp_video(SrsRtpPacket* pkt, int64_t dts, int64_t pts)
{
    int ret = ERROR_SUCCESS;

    if ((ret = kickoff_audio_cache(pkt, dts)) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = write_h264_ipb_frame(pkt->payload->bytes(), pkt->payload->length(), dts / 90, pts / 90)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsRtspConn::on_rtp_audio(SrsRtpPacket* pkt, int64_t dts)
{
    int ret = ERROR_SUCCESS;

    if ((ret = kickoff_audio_cache(pkt, dts)) != ERROR_SUCCESS) {
        return ret;
    }

    // cache current audio to kickoff.
    acache->dts = dts;
    acache->audio_samples = pkt->audio_samples;
    acache->payload = pkt->payload;

    pkt->audio_samples = NULL;
    pkt->payload = NULL;

    return ret;
}

int SrsRtspConn::kickoff_audio_cache(SrsRtpPacket* pkt, int64_t dts)
{
    int ret = ERROR_SUCCESS;

    // nothing to kick off.
    if (!acache->payload) {
        return ret;
    }

    if (dts - acache->dts > 0 && acache->audio_samples->nb_sample_units > 0) {
        int64_t delta = (dts - acache->dts) / acache->audio_samples->nb_sample_units;
        for (int i = 0; i < acache->audio_samples->nb_sample_units; i++) {
            char* frame = acache->audio_samples->sample_units[i].bytes;
            int nb_frame = acache->audio_samples->sample_units[i].size;
            int64_t timestamp = (acache->dts + delta * i) / 90;
            acodec->aac_packet_type = 1;
            if ((ret = write_audio_raw_frame(frame, nb_frame, acodec, timestamp)) != ERROR_SUCCESS) {
                return ret;
            }
        }
    }

    acache->dts = 0;
    srs_freep(acache->audio_samples);
    srs_freep(acache->payload);

    return ret;
}

int SrsRtspConn::write_sequence_header()
{
    int ret = ERROR_SUCCESS;

    // use the current dts.
    int64_t dts = vjitter->timestamp() / 90;

    // send video sps/pps
    if ((ret = write_h264_sps_pps(dts, dts)) != ERROR_SUCCESS) {
        return ret;
    }

    // generate audio sh by audio specific config.
    if (true) {
        std::string sh = aac_specific_config;

        SrsAvcAacCodec dec;
        if ((ret = dec.audio_aac_sequence_header_demux((char*)sh.c_str(), (int)sh.length())) != ERROR_SUCCESS) {
            return ret;
        }

        acodec->sound_format = SrsCodecAudioAAC;
        acodec->sound_type = (dec.aac_channels == 2)? SrsCodecAudioSoundTypeStereo : SrsCodecAudioSoundTypeMono;
        acodec->sound_size = SrsCodecAudioSampleSize16bit;
        acodec->aac_packet_type = 0;

        static int aac_sample_rates[] = {
            96000, 88200, 64000, 48000,
            44100, 32000, 24000, 22050,
            16000, 12000, 11025,  8000,
            7350,     0,     0,    0
        };
        switch (aac_sample_rates[dec.aac_sample_rate]) {
            case 11025:
                acodec->sound_rate = SrsCodecAudioSampleRate11025;
                break;
            case 22050:
                acodec->sound_rate = SrsCodecAudioSampleRate22050;
                break;
            case 44100:
                acodec->sound_rate = SrsCodecAudioSampleRate44100;
                break;
            default:
                break;
        };

        if ((ret = write_audio_raw_frame((char*)sh.data(), (int)sh.length(), acodec, dts)) != ERROR_SUCCESS) {
            return ret;
        }
    }

    return ret;
}

int SrsRtspConn::write_h264_sps_pps(u_int32_t dts, u_int32_t pts)
{
    int ret = ERROR_SUCCESS;
    
    // h264 raw to h264 packet.
    std::string sh;
    if ((ret = avc->mux_sequence_header(h264_sps, h264_pps, dts, pts, sh)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // h264 packet to flv packet.
    int8_t frame_type = SrsCodecVideoAVCFrameKeyFrame;
    int8_t avc_packet_type = SrsCodecVideoAVCTypeSequenceHeader;
    char* flv = NULL;
    int nb_flv = 0;
    if ((ret = avc->mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // the timestamp in rtmp message header is dts.
    u_int32_t timestamp = dts;
    if ((ret = rtmp_write_packet(SrsCodecFlvTagVideo, timestamp, flv, nb_flv)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsRtspConn::write_h264_ipb_frame(char* frame, int frame_size, u_int32_t dts, u_int32_t pts) 
{
    int ret = ERROR_SUCCESS;
    
    // 5bits, 7.3.1 NAL unit syntax,
    // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
    
    // for IDR frame, the frame is keyframe.
    SrsCodecVideoAVCFrame frame_type = SrsCodecVideoAVCFrameInterFrame;
    if (nal_unit_type == SrsAvcNaluTypeIDR) {
        frame_type = SrsCodecVideoAVCFrameKeyFrame;
    }

    std::string ibp;
    if ((ret = avc->mux_ipb_frame(frame, frame_size, ibp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int8_t avc_packet_type = SrsCodecVideoAVCTypeNALU;
    char* flv = NULL;
    int nb_flv = 0;
    if ((ret = avc->mux_avc2flv(ibp, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // the timestamp in rtmp message header is dts.
    u_int32_t timestamp = dts;
    return rtmp_write_packet(SrsCodecFlvTagVideo, timestamp, flv, nb_flv);
}

int SrsRtspConn::write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, u_int32_t dts)
{
    int ret = ERROR_SUCCESS;

    char* data = NULL;
    int size = 0;
    if ((ret = aac->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return rtmp_write_packet(SrsCodecFlvTagAudio, dts, data, size);
}

int SrsRtspConn::rtmp_write_packet(char type, u_int32_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    SrsSharedPtrMessage* msg = NULL;

    if ((ret = srs_rtmp_create_msg(type, timestamp, data, size, stream_id, &msg)) != ERROR_SUCCESS) {
        srs_error("rtsp: create shared ptr msg failed. ret=%d", ret);
        return ret;
    }
    srs_assert(msg);

    // send out encoded msg.
    if ((ret = client->send_and_free_message(msg, stream_id)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

// TODO: FIXME: merge all client code.
int SrsRtspConn::connect()
{
    int ret = ERROR_SUCCESS;

    // when ok, ignore.
    if (io || client) {
        return ret;
    }
    
    // parse uri
    if (!req) {
        std::string schema, host, vhost, app, port, param;
        srs_discovery_tc_url(rtsp_tcUrl, schema, host, vhost, app, port, param);

        // generate output by template.
        std::string output = output_template;
        output = srs_string_replace(output, "[app]", app);
        output = srs_string_replace(output, "[stream]", rtsp_stream);
        
        req = new SrsRequest();
        srs_parse_rtmp_url(output, req->tcUrl, req->stream);
        srs_discovery_tc_url(req->tcUrl, req->schema, req->host, req->vhost, req->app, req->port, req->param);
    }

    // connect host.
    if ((ret = srs_socket_connect(req->host, ::atoi(req->port.c_str()), ST_UTIME_NO_TIMEOUT, &stfd)) != ERROR_SUCCESS) {
        srs_error("rtsp: connect server %s:%s failed. ret=%d", req->host.c_str(), req->port.c_str(), ret);
        return ret;
    }
    io = new SrsStSocket(stfd);
    client = new SrsRtmpClient(io);

    client->set_recv_timeout(SRS_CONSTS_RTMP_RECV_TIMEOUT_US);
    client->set_send_timeout(SRS_CONSTS_RTMP_SEND_TIMEOUT_US);
    
    // connect to vhost/app
    if ((ret = client->handshake()) != ERROR_SUCCESS) {
        srs_error("rtsp: handshake with server failed. ret=%d", ret);
        return ret;
    }
    if ((ret = connect_app(req->host, req->port)) != ERROR_SUCCESS) {
        srs_error("rtsp: connect with server failed. ret=%d", ret);
        return ret;
    }
    if ((ret = client->create_stream(stream_id)) != ERROR_SUCCESS) {
        srs_error("rtsp: connect with server failed, stream_id=%d. ret=%d", stream_id, ret);
        return ret;
    }
    
    // publish.
    if ((ret = client->publish(req->stream, stream_id)) != ERROR_SUCCESS) {
        srs_error("rtsp: publish failed, stream=%s, stream_id=%d. ret=%d", 
            req->stream.c_str(), stream_id, ret);
        return ret;
    }

    return write_sequence_header();
}

// TODO: FIXME: refine the connect_app.
int SrsRtspConn::connect_app(string ep_server, string ep_port)
{
    int ret = ERROR_SUCCESS;
    
    // args of request takes the srs info.
    if (req->args == NULL) {
        req->args = SrsAmf0Any::object();
    }
    
    // notify server the edge identity,
    // @see https://github.com/simple-rtmp-server/srs/issues/147
    SrsAmf0Object* data = req->args;
    data->set("srs_sig", SrsAmf0Any::str(RTMP_SIG_SRS_KEY));
    data->set("srs_server", SrsAmf0Any::str(RTMP_SIG_SRS_KEY" "RTMP_SIG_SRS_VERSION" ("RTMP_SIG_SRS_URL_SHORT")"));
    data->set("srs_license", SrsAmf0Any::str(RTMP_SIG_SRS_LICENSE));
    data->set("srs_role", SrsAmf0Any::str(RTMP_SIG_SRS_ROLE));
    data->set("srs_url", SrsAmf0Any::str(RTMP_SIG_SRS_URL));
    data->set("srs_version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));
    data->set("srs_site", SrsAmf0Any::str(RTMP_SIG_SRS_WEB));
    data->set("srs_email", SrsAmf0Any::str(RTMP_SIG_SRS_EMAIL));
    data->set("srs_copyright", SrsAmf0Any::str(RTMP_SIG_SRS_COPYRIGHT));
    data->set("srs_primary", SrsAmf0Any::str(RTMP_SIG_SRS_PRIMARY));
    data->set("srs_authors", SrsAmf0Any::str(RTMP_SIG_SRS_AUTHROS));
    // for edge to directly get the id of client.
    data->set("srs_pid", SrsAmf0Any::number(getpid()));
    data->set("srs_id", SrsAmf0Any::number(_srs_context->get_id()));
    
    // local ip of edge
    std::vector<std::string> ips = srs_get_local_ipv4_ips();
    assert(_srs_config->get_stats_network() < (int)ips.size());
    std::string local_ip = ips[_srs_config->get_stats_network()];
    data->set("srs_server_ip", SrsAmf0Any::str(local_ip.c_str()));
    
    // generate the tcUrl
    std::string param = "";
    std::string tc_url = srs_generate_tc_url(ep_server, req->vhost, req->app, ep_port, param);
    
    // upnode server identity will show in the connect_app of client.
    // @see https://github.com/simple-rtmp-server/srs/issues/160
    // the debug_srs_upnode is config in vhost and default to true.
    bool debug_srs_upnode = _srs_config->get_debug_srs_upnode(req->vhost);
    if ((ret = client->connect_app(req->app, tc_url, req, debug_srs_upnode)) != ERROR_SUCCESS) {
        srs_error("rtsp: connect with server failed, tcUrl=%s, dsu=%d. ret=%d", 
            tc_url.c_str(), debug_srs_upnode, ret);
        return ret;
    }
    
    return ret;
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

int SrsRtspCaster::alloc_port(int* pport)
{
    int ret = ERROR_SUCCESS;

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

    return ret;
}

void SrsRtspCaster::free_port(int lpmin, int lpmax)
{
    for (int i = lpmin; i < lpmax; i++) {
        used_ports[i] = false;
    }
    srs_trace("rtsp: free rtp port=%d-%d", lpmin, lpmax);
}

int SrsRtspCaster::on_tcp_client(st_netfd_t stfd)
{
    int ret = ERROR_SUCCESS;

    SrsRtspConn* conn = new SrsRtspConn(this, stfd, output);

    if ((ret = conn->serve()) != ERROR_SUCCESS) {
        srs_error("rtsp: serve client failed. ret=%d", ret);
        srs_freep(conn);
        return ret;
    }

    clients.push_back(conn);
    srs_info("rtsp: start thread to serve client.");

    return ret;
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

#endif

