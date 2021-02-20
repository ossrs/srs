/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Lixin
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

#include <srs_app_rtc_to_rtmp.hpp>
using namespace std;

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <sstream>

#include <srs_protocol_utility.hpp>
#include <srs_raw_avc.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_server.hpp>

SrsRtcRtmpRecv::SrsRtcRtmpRecv(SrsRtcConnection* s, SrsRtcRtmpMuxer* m, const SrsContextId& cid)
{
    cid_ = cid;
    trd = new SrsDummyCoroutine();
    is_started = false;
    sdk = NULL;
    session_ = s;
    muxer_ = m;
    locker_ = srs_mutex_new();
}

SrsRtcRtmpRecv::~SrsRtcRtmpRecv()
{
    stop();
    srs_freep(trd);
    srs_mutex_destroy(locker_);
}

srs_error_t SrsRtcRtmpRecv::on_reload_vhost_play(std::string vhost)
{
    return srs_success;
}

srs_error_t SrsRtcRtmpRecv::on_reload_vhost_realtime(std::string vhost)
{
    return on_reload_vhost_play(vhost);
}

const SrsContextId& SrsRtcRtmpRecv::context_id()
{
    return cid_;
}

srs_error_t SrsRtcRtmpRecv::start()
{
    srs_error_t err = srs_success;

    if (is_started) {
        return err;
    }

    srs_freep(trd);
    trd = new SrsSTCoroutine("rtc_rtmpmuxer_recv", this, cid_);
    srs_trace("start rtc_rtmpmuxer_recv");

    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "rtc_rtmpmuxer_recv");
    }

    is_started = true;

    return err;
}

void SrsRtcRtmpRecv::stop()
{
    trd->stop();
}

void SrsRtcRtmpRecv::set_rtmp_client(SrsSimpleRtmpClient* c)
{
    //locker RTMP Muxer will be released and reconnected 
    SrsLocker(locker_);
    sdk = c;
    srs_trace("RTC: rtmpmuxer(%s) set rtmp client:%d", muxer_->stream_url().c_str(), sdk);
}

srs_error_t SrsRtcRtmpRecv::cycle()
{
    srs_error_t err = srs_success;
    int err_count = 0;

    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "rtc sender thread");
        }

        if (muxer_){
            //locker  RTMP Muxer will be released and reconnected 
            SrsLocker(locker_);
            if (sdk) {
                SrsCommonMessage* msg = NULL;
                err = sdk->recv_message(&msg);
                if (err != srs_success && srs_error_code(err) != ERROR_SOCKET_TIMEOUT) {
                    srs_warn("RTC: rtmp(%s) receive control message err %s", muxer_->stream_url().c_str(), 
                             srs_error_desc(err).c_str());
                    //get muxer rtmp cleint, update rtmp client
                    sdk = muxer_->get_rtmp_client();
                    err_count++;
                }else{
                    err_count = 0;
                }

                //three consecutive errors, need reconnection
                if (err_count >= 3){
                    srs_warn("RTC: rtmp(%s) receive 3 count err, need reconn", muxer_->stream_url().c_str());
                    err_count = 0;

                    //close rtmp, and reconn
                    muxer_->republish();
                    sdk = NULL;
                }

                srs_error_reset(err);
                srs_freep(msg);
            }
        }

        srs_usleep(200 * SRS_UTIME_MILLISECONDS);
    }
}

SrsRtcRtmpMuxer::SrsRtcRtmpMuxer(SrsRtcConnection* s, SrsRtcPublishStream* p, const SrsContextId& cid)
{
    cid_ = cid;
    trd = new SrsDummyCoroutine();
    rtmp_recv_ = new SrsRtcRtmpRecv(s, this, cid);

    req_ = NULL;
    source_ = NULL;

    is_started = false;
    session_ = s;
    publish_ = p;

    mw_msgs = 0;
    realtime = true;

    _srs_config->subscribe(this);
   
   
    sdk = NULL;
    vjitter = new SrsRtpTimeJitter();
    ajitter = new SrsRtpTimeJitter();
    jitter_buffer = NULL;

    frame_data_buflen = 0;
    frame_data_buffer = NULL;
    
    avc = new SrsRawH264Stream();
    aac = new SrsRawAacStream();
    acodec = new SrsRawAacStreamCodec();
    recv_audio_rtp_time = 0;
    recv_video_rtp_time = 0;
    req_keyframe_time = 0;
    send_auido_seqheader = true;
    send_video_seqheader = true;
    republish_video = false;
    republish_video = false;

    rtmp_source = NULL;
    key_frame_count = 0;
    
    totals_frame_count = 0;
    fps_sample_time = 0;
    frame_rate = 0;

    queue = new SrsMessageQueue();

    audio_enabled = _srs_config->get_rtc_server_rtmp_audio_enabled();
    audio_format = _srs_config->get_rtc_server_rtmp_audio_format();
    opus_payload_type = 0;

    source_copy = _srs_config->get_rtc_server_rtmp_source_copy();
    kerframe_interal_print = _srs_config->get_rtc_server_rtmp_keyframe_interval_print();
    req_kerframe = _srs_config->get_rtc_server_rtmp_req_keyframe();


    record_video = _srs_config->get_rtc_server_rtmp_record_video();
    record_auido = _srs_config->get_rtc_server_rtmp_record_audio();
    record_path = _srs_config->get_rtc_server_rtmp_record_path();
    fw_video = NULL;
}

SrsRtcRtmpMuxer::~SrsRtcRtmpMuxer()
{
    _srs_config->unsubscribe(this);
    
    stop();
    srs_freep(rtmp_recv_);
    srs_freep(sdk);
    srs_freep(trd);
    srs_freep(fw_video);
   
    srs_freep(req_);
   
    srs_freep(vjitter);
    srs_freep(ajitter);
    srs_freep(jitter_buffer);
    srs_freep(frame_data_buffer);

    srs_freep(avc);
    srs_freep(aac);
    srs_freep(acodec);

    //srs_freep(rtmp_source);

    srs_freep(queue);
}

srs_error_t SrsRtcRtmpMuxer::initialize(SrsRequest* req)
{
    srs_error_t err = srs_success;

    req_ = req->copy();

    if (!jitter_buffer) {
        jitter_buffer = new SrsRtpJitterBuffer(req_->get_stream_url());
    }

    jitter_buffer->SetDecodeErrorMode(kSelectiveErrors);
    jitter_buffer->SetNackMode(kNack, -1, -1);
    jitter_buffer->SetNackSettings(250, 450, 0);

    if (record_video){
        fw_video = new SrsFileWriter();

        std::string fullname = record_path + req_->get_stream_url() + ".h264";
        std::string record_dir = srs_path_dirname(fullname);
    
        if ((err = srs_create_dir_recursively(record_dir)) != srs_success) {
            return srs_error_wrap(err, "create %s", record_dir.c_str());
        }

        if (!fw_video->is_open()){
            fw_video->open(fullname);
        }
    
        srs_trace("RTC: rtmpmuxer create dir %s ok", record_dir.c_str());
    }

    if ((err = _srs_rtc_sources->fetch_or_create(req_, &source_)) != srs_success) {
        return srs_error_wrap(err, "rtc fetch source failed");
    }

    if (source_copy){
        if ((err = _srs_sources->fetch_or_create(req, (ISrsSourceHandler*)_srs_hybrid->srs()->instance(), &rtmp_source)) != srs_success) {
            return srs_error_wrap(err, "create rtmp source");
        }

        if ((err = rtmp_source->on_publish()) != srs_success) {
            return srs_error_wrap(err, "on publish");
        }
        srs_trace("RTC rtmpmuxer source=%s", rtmp_source->source_id().c_str());
    }else {
        if ((err = _srs_sources->fetch_or_create(req, (ISrsSourceHandler*)_srs_hybrid->srs()->instance(), &rtmp_source)) != srs_success) {
            return srs_error_wrap(err, "create rtmp source");
        }
    }

    opus_payload_type = _srs_config->get_rtc_server_rtmp_opus_payload_type();
    
    srs_utime_t queue_size = _srs_config->get_queue_length(req->vhost);
    queue->set_queue_size(queue_size);

    session_->stat_->nn_subscribers++;

    return err;
}

srs_error_t SrsRtcRtmpMuxer::on_reload_vhost_play(std::string vhost)
{
    if (req_->vhost != vhost) {
        return srs_success;
    }

    realtime = _srs_config->get_realtime_enabled(req_->vhost, true);
    mw_msgs = _srs_config->get_mw_msgs(req_->vhost, realtime, true);

    srs_trace("Reload play realtime=%d, mw_msgs=%d", realtime, mw_msgs);

    return srs_success;
}

srs_error_t SrsRtcRtmpMuxer::on_reload_vhost_realtime(std::string vhost)
{
    return on_reload_vhost_play(vhost);
}

const SrsContextId& SrsRtcRtmpMuxer::context_id()
{
    return cid_;
}

SrsSimpleRtmpClient* SrsRtcRtmpMuxer::get_rtmp_client()
{
    return sdk;
}

srs_error_t SrsRtcRtmpMuxer::start()
{
    srs_error_t err = srs_success;

    if (is_started) {
        return err;
    }

    srs_freep(trd);
    trd = new SrsSTCoroutine("rtc_rtmpmuxer", this, cid_);
    srs_trace("start rtc_rtmpmuxer");

    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "rtc_rtmpmuxer");
    }

    if ((err = rtmp_recv_->start()) != srs_success) {
        return srs_error_wrap(err, "start rtmpmuxer recv");
    }

    is_started = true;

    return err;
}

void SrsRtcRtmpMuxer::stop()
{
    trd->stop();
}

#define SYS_MAX_FORWARD_SEND_RTMP_MSGS 128
#define RECV_RTP_PACKNET_MAX_DUR (1500 * SRS_UTIME_MILLISECONDS)
#define RTMP_RECONN_DUR (5 * SRS_UTIME_SECONDS)
srs_error_t SrsRtcRtmpMuxer::cycle()
{
    srs_error_t err = srs_success;

    SrsRtcStream* source = source_;

    SrsRtcConsumer* consumer = NULL;
    SrsAutoFree(SrsRtcConsumer, consumer);
    if ((err = source->create_consumer(consumer)) != srs_success) {
        return srs_error_wrap(err, "create consumer, source=%s", req_->get_stream_url().c_str());
    }

    // TODO: FIXME: Dumps the SPS/PPS from gop cache, without other frames.
    if ((err = source->consumer_dumps(consumer)) != srs_success) {
        return srs_error_wrap(err, "dumps consumer, url=%s", req_->get_stream_url().c_str());
    }

    realtime = _srs_config->get_realtime_enabled(req_->vhost, true);
    mw_msgs = _srs_config->get_mw_msgs(req_->vhost, realtime, true);

    SrsContextId cid = source->source_id();
    srs_trace("RTC: start rtmpmuxer url=%s, source_id=[%d][%s], realtime=%d, mw_msgs=%d", req_->get_stream_url().c_str(),
        ::getpid(), cid.c_str(), realtime, mw_msgs);

    SrsErrorPithyPrint* epp = new SrsErrorPithyPrint();
    SrsAutoFree(SrsErrorPithyPrint, epp);

    SrsPithyPrint* pprint = SrsPithyPrint::create_rtc_play();
    SrsAutoFree(SrsPithyPrint, pprint);

    bool stat_enabled = _srs_config->get_rtc_server_perf_stat();

    vector<SrsRtpPacket2*> pkts;
    uint64_t total_pkts = 0;

    SrsMessageArray msgs(SYS_MAX_FORWARD_SEND_RTMP_MSGS);

    srs_utime_t last_rtmp_conn_time = 0;
   
    bool reconn = false;

    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "rtc sender thread");
        }
        
        // republish, may be client offline, after online publish
        if (republish_video){
            srs_trace("RTC rtmpmuxer republish(%d) and rtmp close, reconn!!!", republish_video);
            rtmp_close();

            send_video_seqheader = true;
            send_auido_seqheader = true;
            key_frame_count = 0;
            jitter_buffer->ResetJittter();

            reconn = true;
            republish_video = false;
        }

        if (sdk)
        {
            //check rtp recv data is timeout

            srs_utime_t diff_v = srs_get_system_time() - recv_video_rtp_time;
            srs_utime_t diff_a = srs_get_system_time() - recv_audio_rtp_time;
            srs_utime_t diff_req_keyframe = srs_get_system_time() - req_keyframe_time;

            //if there is no data task RTP packet within RECV_RTP_PACKNET_MAX_DUR, the network will be dropped
            if (diff_v > RECV_RTP_PACKNET_MAX_DUR){
                srs_trace("RTC rtmpmuxer %s recv video rtp packet timeout", req_->get_stream_url().c_str());
               
                send_video_seqheader = true;
                key_frame_count = 0;
                jitter_buffer->ResetJittter();
                recv_video_rtp_time = srs_get_system_time();
                totals_frame_count = 0;
            }

            if (diff_a > RECV_RTP_PACKNET_MAX_DUR){
                srs_trace("RTC rtmpmuxer %s recv audio rtp packet timeout", req_->get_stream_url().c_str());
               
                send_auido_seqheader = true;
                recv_audio_rtp_time = srs_get_system_time();
            }

            if (diff_req_keyframe > req_kerframe && req_kerframe != 0 && video_ssrc > 0 && publish_){
                srs_trace("RTC rtmpmuxer %s req keyframe", req_->get_stream_url().c_str());
                publish_->do_request_keyframe(video_ssrc, cid);
                req_keyframe_time = srs_get_system_time();
            }
            
        }else{
            srs_utime_t diff = srs_get_system_time() - last_rtmp_conn_time;
            if (diff >= RTMP_RECONN_DUR || reconn){

                if ((err = rtmp_connect()) != srs_success) {
                    h264_pps = "";
                    h264_sps = "";
                    srs_warn("RTC rtmpmuxer %s connect rtmp server failed!", req_->get_stream_url().c_str());
                    srs_freep(err);
                }
                
                reconn = false;
                last_rtmp_conn_time = srs_get_system_time();
            }
        }

        // Wait for amount of packets.
        consumer->wait(mw_msgs);

        // TODO: FIXME: Handle error.
        consumer->dump_packets(pkts);
      
        int msg_count = (int)pkts.size();
        if (!msg_count) {
            continue;
        }
        
        // Update stats for session.
        session_->stat_->nn_out_rtp += msg_count;
        total_pkts += msg_count;

        // All RTP packets to rtmp and do cleanup
        if (true) {

            if (sdk)
            {
                if ((err = on_rtp_packets(source, pkts)) != srs_success) {
                    uint32_t nn = 0;
                    if (epp->can_print(err, &nn)) {
                        srs_warn("RTC rtmpmuxer send packets=%u, nn=%u/%u, err: %s", pkts.size(), epp->nn_count, nn, srs_error_desc(err).c_str());
                    }
                    srs_freep(err);
                }
            }

            //free rtp pkt
            for (int i = 0; i < msg_count; i++) {
                SrsRtpPacket2* pkt = pkts[i];
                srs_freep(pkt);
            }
            pkts.clear();
        }

        // Stat for performance analysis.
        if (!stat_enabled) {
            continue;
        }

        pprint->elapse();
        if (pprint->can_print()) {
            // TODO: FIXME: Print stat like frame/s, packet/s, loss_packets.
            // srs_trace("-> RTC RTMPMUXER %d msgs, %d/%d packets, %d audios, %d extras, %d videos, %d samples, %d/%d/%d bytes, %d pad, %d/%d cache",
            //     total_pkts, msg_count, info.nn_rtp_pkts, info.nn_audios, info.nn_extras, info.nn_videos, info.nn_samples, info.nn_bytes,
            //     info.nn_rtp_bytes, info.nn_padding_bytes, info.nn_paddings, msg_count, msg_count);
            srs_trace("-> RTC RTMPMUXER %s %d msgs, %d packets", req_->get_stream_url().c_str(), total_pkts, msg_count); 
        }
    }

}

srs_error_t SrsRtcRtmpMuxer::write_h264_file_by_jitbuffer(char *buf, int size, SrsFileWriter *fw)
{
    srs_error_t err = srs_success;
    if (NULL == buf || size <= 0 || !record_video || !fw->is_open()){
        return err;
    }

    fw->write(buf, size, 0);

    return err;
}

srs_error_t SrsRtcRtmpMuxer::write_h264_file(SrsRtpPacket2* pkt, SrsFileWriter *fw)
{
    srs_error_t err = srs_success;
    if (NULL == pkt || NULL == fw || !record_video || !fw->is_open()){
        return err;
    }

    int8_t v = (uint8_t)pkt->nalu_type;
    if (v == kStapA) {
        SrsRtpSTAPPayload *payload = (SrsRtpSTAPPayload*)pkt->payload;
    
        for (int j = 0; j < (int)payload->nalus.size(); j++) {
            char startcode[] = { 0x00, 0x00, 0x00, 0x01};
            fw->write(startcode, 4, NULL);

            SrsSample *stream =  payload->nalus.at(j);
            fw->write(stream->bytes, stream->size, 0);
        }
    } else if (v == kFuA) {
        SrsRtpFUAPayload2 *payload = (SrsRtpFUAPayload2*)pkt->payload;
    
        char *buf = payload->payload;
        int size = payload->size;
    
        int8_t nalu_byte0 = ((int8_t)payload->nri & 0xE0) | ((int8_t)payload->nalu_type & 0x1F);

        if (payload->start){
            char startcode[] = { 0x00, 0x00, 0x00, 0x01};
            fw->write(startcode, 4, NULL);
           
            fw->write(&nalu_byte0, 1, NULL);
            fw->write(buf, size, 0);
        }else {
            fw->write(buf, size, 0);
        }

    } else {
        //char *buf = pkt->shared_msg->payload + 12;
        //int size = pkt->shared_msg->size - 12;

        SrsRtpRawPayload *payload = (SrsRtpRawPayload*)pkt->payload;
     
        char startcode[] = { 0x00, 0x00, 0x00, 0x01};
        fw->write(startcode, 4, NULL);
        fw->write(payload->payload, payload->nb_bytes(), 0);
    }

    return err;
}

srs_error_t SrsRtcRtmpMuxer::on_rtp_packets(SrsRtcStream* source, const vector<SrsRtpPacket2*>& pkts)
{
    srs_error_t err = srs_success;

    vector<SrsRtpPacket2*> send_pkts;

    // Covert kernel messages to RTP packets.
    for (int i = 0; i < (int)pkts.size(); i++) {
        SrsRtpPacket2* pkt = pkts[i];
       
        if (pkt->is_audio()) {

            if (opus_payload_type != 0){
                 pkt->header.set_payload_type(opus_payload_type);
            }

            if ((err = on_rtp_audio(pkt)) != srs_success){
                srs_error("RTC: rtmp muxer process rtp audio error(%s)", srs_error_desc(err).c_str());
                srs_error_reset(err);
                continue;
            }
        } else {
            //write_h264_file(pkt, fw_video);
            video_ssrc = pkt->header.get_ssrc();

            if ((err = on_rtp_video(pkt)) != srs_success){
                srs_error("RTC: rtmp muxer process rtp video error(%s)", srs_error_desc(err).c_str());
                srs_error_reset(err);
                continue;
            }
        }
        
        // Detail log, should disable it in release version.
        srs_info("RTC: rtmp muxer PT=%u, SSRC=%#x, Time=%u, SEQ=%u, %u bytes", pkt->header.get_payload_type(), pkt->header.get_ssrc(),
            pkt->header.get_timestamp(), pkt->header.get_sequence(), pkt->nb_bytes());
    }

    return err;
}

srs_error_t SrsRtcRtmpMuxer::on_rtp_audio(SrsRtpPacket2* pkt)
{
    srs_error_t err = srs_success;

    recv_audio_rtp_time = srs_get_system_time();

    if (!audio_enabled)
        return err;

    // TODO: FIXME: Padding audio to the max payload in RTP packets.
    int64_t pts = pkt->header.get_timestamp();
    if ((err = ajitter->correct(pts)) != srs_success) {
        return srs_error_wrap(err, "a timestmp jitter");
    }
    
    //opus sample rate is 48000;  per ms sample nums:48000/1000=48
    uint32_t fpts = (uint32_t)(pts / 48);

    if (true) {
       
        char* flv = NULL;
        int nb_flv = 0;

        if (audio_format == "rtp") {
            if ((err = mux_opusflv_rtp(pkt, &flv, &nb_flv)) != srs_success) {
                return srs_error_wrap(err, "mux avc rtp opus to flv");
            }
        } else if (audio_format == "opus") {
            if ((err = mux_opusflv(pkt, &flv, &nb_flv)) != srs_success) {
                return srs_error_wrap(err, "mux avc rtp opus to flv");
            }
        }else if (audio_format == "aac"){
            //TODO: fixme opus to aac
            return err;
        }
        
        // the timestamp in rtmp message header is dts.
        uint32_t timestamp = fpts;
        return rtmp_write_packet(SrsFrameTypeAudio, timestamp, flv, nb_flv);
    }

    return err;
}

srs_error_t SrsRtcRtmpMuxer::on_rtp_video(SrsRtpPacket2* pkt)
{
    srs_error_t err = srs_success;
    
    recv_video_rtp_time = srs_get_system_time();

    jitter_buffer->InsertPacket2(*pkt, NULL);

    uint32_t cur_timestamp = 0; 
    int frame_size = 0;
    bool keyframe = false;

    if(jitter_buffer->FoundFrame(cur_timestamp)){
        jitter_buffer->GetFrame(&frame_data_buffer, frame_data_buflen, frame_size, keyframe, cur_timestamp);
        if (frame_data_buflen > 0){
            char* bytes = frame_data_buffer;
            int length = frame_size;

            int64_t pts = cur_timestamp;
                if ((err = vjitter->correct(pts)) != srs_success) {
                return srs_error_wrap(err, "v timestamp jitter");
            }

            uint32_t fdts = (uint32_t)(pts / 90);
            uint32_t fpts = (uint32_t)(pts / 90);

            statistics_fps();
            write_h264_file_by_jitbuffer(bytes, length, fw_video);

            //srs_trace("====%u,%u", cur_timestamp, fpts);

            if ((err = replace_startcode_with_nalulen(bytes, length, fdts, fpts)) != srs_success) {
                return srs_error_wrap(err, "write ibp frame");
            }
        }
    }

    return err;  
}

srs_error_t SrsRtcRtmpMuxer::mux_opusflv_rtp(SrsRtpPacket2* pkt, char** flv, int* nb_flv)
{
    srs_error_t err = srs_success;

    //rtp opus to custom rtmp opus format
    //rtmp opus rtp fromt:
    // |rtmp header | rtmp body |
    // |rtmp header | FF+00+(opus rtp packet)|  
    
    // |rtp header | rtp ext header | rtp payload |
    int head_size = (int)pkt->header.nb_bytes(); //rtp header size
    int head_ext_size = pkt->shared_msg->size - pkt->payload->nb_bytes(); //rtp header + ext head size
    int payload_size = pkt->payload->nb_bytes(); //rtp payload size

    //pkt->shared_msg is raw rtp packet data
    //rtp del extension flag
    pkt->shared_msg->payload[0] = 0x80; 
    
    //update payload type
    if (opus_payload_type != 0){
        uint8_t second = pkt->shared_msg->payload[1];
        uint8_t marker = (second & 0x80);
  
        if (marker) {
            opus_payload_type |= 0x80;
        }

        pkt->shared_msg->payload[1] = opus_payload_type;
    }
    
    int size = head_size + payload_size + 2;  //del rtp ext header
   
    char* data = new char[size];
    char* p = data;
    
    //custom rtmp opus type
    *p++ = 0xFF;
    *p++ = 0x00;

    //rtp header, skip extension head 
    memcpy(p, pkt->shared_msg->payload, head_size);
    p += head_size;

    //skip ext header, copy payload
    memcpy(p, pkt->shared_msg->payload + head_ext_size, payload_size);

    *flv = data;
    *nb_flv = size;
    
    return err;
}

srs_error_t SrsRtcRtmpMuxer::mux_opusflv(SrsRtpPacket2* pkt, char** flv, int* nb_flv)
{
    srs_error_t err = srs_success;
    //rtp opus to custom rtmp opus format
    //rtmp opus fromt:
    // |rtmp header | rtmp body |
    // |rtmp header | SoundFormat|SoundRate|SoundSize|SoundType|+00+(opus payload)|  
    
    // |rtp header | rtp ext header | rtp payload |
    //int head_size = pkt->header.nb_bytes(); //rtp header size
    int head_ext_size = pkt->shared_msg->size - pkt->payload->nb_bytes(); //rtp header + ext head size
    int payload_size = pkt->payload->nb_bytes(); //rtp payload size

    //pkt->shared_msg is raw rtp packet data
    int size = payload_size + 2;  //del rtp header and ext header
   
    char* data = new char[size];
    char* p = data;
    
    //custom rtmp flv opus type
    char sound_format = SrsAudioCodecIdOpus;
    // flv audio header define sound_type
    // 0 = Mono sound
    // 1 = Stereo sound
    char sound_type = 0;  
    // flv audio header define sound_size
    // 0 = 8-bit samples
    // 1 = 16-bit samples
    char sound_size = 1;
    // flv audio header define sound_rate
    // 0 = 5.5 kHz
    // 1 = 11 kHz
    // 2 = 22 kHz
    // 3 = 44 kHz
    char sound_rate = 3;
    // for audio frame, there is 1 or 2 bytes header:
    //   1bytes, SoundFormat|SoundRate|SoundSize|SoundType
    //   1bytes, 00
    uint8_t audio_header = sound_type & 0x01;
    audio_header |= (sound_size << 1) & 0x02;
    audio_header |= (sound_rate << 2) & 0x0c;
    audio_header |= (sound_format << 4) & 0xf0;

    *p++ = audio_header;
    *p++ = 0x00;

    //skip rtp header and ext header, copy payload
    memcpy(p, pkt->shared_msg->payload + head_ext_size, payload_size);

    *flv = data;
    *nb_flv = size;
    
    return err;
}

srs_error_t SrsRtcRtmpMuxer::replace_startcode_with_nalulen(char *video_data, int &size, uint32_t pts, uint32_t dts)
 {
    srs_error_t err = srs_success;

    int index = 0;
    std::list<int> list_index;

    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(video_data[0] & 0x1f);

    if ((video_data[0] & 0x0FF) == 0x00 && (video_data[1] & 0xFF)  == 0x00 && 
        (video_data[2] & 0x0FF) == 0x00 && (video_data[3] & 0xFF)  == 0x01){
        nal_unit_type = (SrsAvcNaluType)(video_data[4] & 0x1f);
    }

    for(; index < size; index++){
        if (index > (size - 4))
            break;

        if (video_data[index] == 0x00 && video_data[index+1] == 0x00 &&
             video_data[index+2] == 0x00 && video_data[index+3] == 0x01){
                 list_index.push_back(index);
        }
    }

    if (list_index.size() == 1){
        int cur_pos = list_index.front();
        list_index.pop_front();

        //0001xxxxxxxxxx
        //xxxx0001xxxxxxx
        uint32_t naluLen = size - cur_pos - 4;
        char *p = (char*)&naluLen;

        video_data[cur_pos] = p[3];
        video_data[cur_pos+1] = p[2];
        video_data[cur_pos+2] = p[1];
        video_data[cur_pos+3] = p[0];

        //err = write_h264_ipb_frame(nal_unit_type, video_data, size, dts, pts);

    }else if (list_index.size() > 1){//mutle slice
        int pre_pos = list_index.front();
        list_index.pop_front();
        int first_pos = pre_pos;

        while(list_index.size() > 0){
            int cur_pos = list_index.front();
            list_index.pop_front();

            //pre=========cur======================
            //0001xxxxxxxx0001xxxxxxxx0001xxxxxxxxx
            //xxxxxxxxxxxx0001xxxxxxxx0001xxxxxxxxx
            uint32_t naluLen = cur_pos - pre_pos - 4;
            char *p = (char*)&naluLen;

            video_data[pre_pos] = p[3];
            video_data[pre_pos+1] = p[2];
            video_data[pre_pos+2] = p[1];
            video_data[pre_pos+3] = p[0];

          
            char *frame = video_data + pre_pos + 4;
            int frame_size = naluLen;

            pre_pos = cur_pos;
            err = decode_h264_sps_pps(frame, frame_size, dts, pts);
        }
        
        //========================pre==========
        //0001xxxxxxxx0001xxxxxxxx0001xxxxxxxxx
        if (first_pos != pre_pos){
            uint32_t naluLen = size - pre_pos - 4;
            char *p = (char*)&naluLen;

            video_data[pre_pos] = p[3];
            video_data[pre_pos+1] = p[2];
            video_data[pre_pos+2] = p[1];
            video_data[pre_pos+3] = p[0];
          
            char *frame = video_data + pre_pos + 4;
            int frame_size = naluLen;
            err = decode_h264_sps_pps(frame, frame_size, dts, pts);
        }

        //err = write_h264_ipb_frame(nal_unit_type, video_data, size, dts, pts);
    }else{
        //xxxxxxxxxxxxxxxxxxx
        //char *frame = video_data;
        //int frame_size = size;
       
    }

    if (send_video_seqheader){
        srs_trace("RTC: send video seq header");
        if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
            return srs_error_wrap(err, "write sps/pps");
        }
        send_video_seqheader = false;
    }

    err = write_h264_ipb_frame(nal_unit_type, video_data, size, dts, pts);

    return err;
}

srs_error_t SrsRtcRtmpMuxer::decode_h264_sps_pps(char *frame, int frame_size, uint32_t pts, uint32_t dts)
{
    srs_error_t err = srs_success;

    if (!frame){
        return srs_error_new(ERROR_GB28181_H264_FRAME_FULL, "h264 frame null");
    }

    if (frame_size <= 0){
        return srs_error_new(ERROR_GB28181_H264_FRAMESIZE, "h264 frame size");
    }

    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
    // ignore the nalu type sei(6) aud(9) 
    switch (nal_unit_type)
    {
    case SrsAvcNaluTypeAccessUnitDelimiter:
    case SrsAvcNaluTypeSEI:
        return err;
    case SrsAvcNaluTypeSPS:
        //ex kerframe_interal_print=10, per 1..11..21.. counts print log
        if (key_frame_count >= kerframe_interal_print){
            key_frame_count = 0; 
        }
        key_frame_count++;
    case SrsAvcNaluTypePPS:
    case SrsAvcNaluTypeIDR:
        if (key_frame_count == 1){
            srs_trace("RTC: rtmpmuxer %s key frame %s FPS:%d", req_->get_stream_url().c_str(), 
                srs_avc_nalu2str(nal_unit_type).c_str(), frame_rate);
        }
        send_video_seqheader = true;
        break;
    default:
        break;
    }

    // for sps
    if (avc->is_sps(frame, frame_size)) {
        std::string sps;
        if ((err = avc->sps_demux(frame, frame_size, sps)) != srs_success) {
            return srs_error_wrap(err, "demux sps");
        }
        
        if (h264_sps == sps && !send_video_seqheader) {
            return err;
        }
        h264_sps = sps;
        
        return err;
    }

    // for pps
    if (avc->is_pps(frame, frame_size)) {
        std::string pps;
        if ((err = avc->pps_demux(frame, frame_size, pps)) != srs_success) {
            return srs_error_wrap(err, "demux pps");
        }
        
        if (h264_pps == pps && !send_video_seqheader) {
            return err;
        }
        h264_pps = pps;
       
        return err;
    }

    return err;
}

srs_error_t SrsRtcRtmpMuxer::write_h264_sps_pps(uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;

    if (h264_sps.empty() || h264_pps.empty()) {
        srs_warn("no sps=%dB or pps=%dB", (int)h264_sps.size(), (int)h264_pps.size());
        return err;
    }
    
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

srs_error_t SrsRtcRtmpMuxer::write_h264_ipb_frame(SrsAvcNaluType nal_unit_type, char* frame, int frame_size, uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;
    
    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    //SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[4] & 0x1f);
    
    // for IDR frame, the frame is keyframe.
    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;
    if (nal_unit_type == SrsAvcNaluTypeIDR || nal_unit_type == SrsAvcNaluTypeSPS) {
        frame_type = SrsVideoAvcFrameTypeKeyFrame;
    }
    
    string ibp = string(frame, frame_size);
    
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

srs_error_t SrsRtcRtmpMuxer::write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts)
{
    srs_error_t err = srs_success;
    
    char* data = NULL;
    int size = 0;
    if ((err = aac->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success) {
        return srs_error_wrap(err, "mux aac to flv");
    }
    
    return rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
}

srs_error_t SrsRtcRtmpMuxer::rtmp_write_packet(char type, uint32_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;

    if (NULL != rtmp_source && source_copy){
        return rtmp_write_packet_by_source(type, timestamp, data, size);
    }

    if (source_copy){
        return err;
    }
    
    if ((err = rtmp_connect()) != srs_success) {
        h264_pps = "";
        h264_sps = "";
        return srs_error_wrap(err, "connect");
    }
    
    SrsSharedPtrMessage* msg = NULL;
    if ((err = srs_rtmp_create_msg(type, timestamp, data, size, sdk->sid(), &msg)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }
    srs_assert(msg);

    if ((err = sdk->send_and_free_message(msg)) != srs_success) {
        rtmp_close();
        return srs_error_wrap(err, "write message");
    }

    return err;
}

srs_error_t SrsRtcRtmpMuxer::rtmp_write_packet_by_source(char type, uint32_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;

    //create a source that will process stream without the need for internal rtmpclient
    if (type == SrsFrameTypeAudio) {
        SrsMessageHeader header;
        header.message_type = RTMP_MSG_AudioMessage;
        // TODO: FIXME: Maybe the tbn is not 90k.
        header.timestamp = timestamp & 0x3fffffff;

        SrsCommonMessage* shared_video = new SrsCommonMessage();
        SrsAutoFree(SrsCommonMessage, shared_video);

        // TODO: FIXME: Check error.
        shared_video->create(&header, data, size);
        rtmp_source->on_audio(shared_video);

        
    }else if(type == SrsFrameTypeVideo) {
        SrsMessageHeader header;
        header.message_type = RTMP_MSG_VideoMessage;
        // TODO: FIXME: Maybe the tbn is not 90k.
        header.timestamp = timestamp & 0x3fffffff;

        SrsCommonMessage* shared_video = new SrsCommonMessage();
        SrsAutoFree(SrsCommonMessage, shared_video);

        // TODO: FIXME: Check error.
        shared_video->create(&header, data, size);
        rtmp_source->on_video(shared_video);
    }
    
    return err;
}

void SrsRtcRtmpMuxer::statistics_fps()
{
    if (totals_frame_count == 0){
        fps_sample_time = srs_get_system_time();
    }
    totals_frame_count++;

    int64_t  diff = (srs_get_system_time() - fps_sample_time) / SRS_UTIME_SECONDS;
    if (diff > 0){
        frame_rate = totals_frame_count / diff;
    }

    if (diff > 60){// per 60s, restatis 
        totals_frame_count = 0;
    }
}


srs_error_t SrsRtcRtmpMuxer::rtmp_connect()
{
    srs_error_t err = srs_success;

    if (source_copy){
        return err;
    }
    
    // Ignore when connected.
    if (sdk) {
        return err;
    }
    
    // generate rtmp url to connect to.
    std::string hostport = "127.0.0.1:1935";
    std::string url = "rtmp://"+ hostport + req_->get_stream_url();

    std::string output =  _srs_config->get_rtc_server_rtmp_output();
   
    if (!output.empty()){
        url = srs_string_replace(output, "[app]", req_->app);
        url = srs_string_replace(url, "[stream]", req_->stream);
    }

    srs_trace("RTC rtmp muxer url:%s", url.c_str());
    
    // connect host.
    srs_utime_t cto = SRS_CONSTS_RTMP_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_PULSE;
    sdk = new SrsSimpleRtmpClient(url, cto, sto);
 
    if ((err = sdk->connect()) != srs_success) {
        rtmp_close();
        return srs_error_wrap(err, "connect %s failed, cto=%dms, sto=%dms.", url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }
    
    // publish.
    if ((err = sdk->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        rtmp_close();
        return srs_error_wrap(err, "publish %s failed", url.c_str());
    }

    sdk->set_recv_timeout(SRS_CONSTS_RTMP_PULSE);
    
    if (NULL != rtmp_recv_){
        rtmp_recv_->set_rtmp_client(sdk);
    }

    jitter_buffer->ResetJittter();
    vjitter->reset();
    ajitter->reset();
    frame_rate = 0;
    totals_frame_count = 0;
   
    return err;
}

void SrsRtcRtmpMuxer::rtmp_close()
{
    h264_pps = "";
    h264_sps = "";
    send_video_seqheader = true;

    //first, clear rtmp recv coroutine rtmp client
    if (NULL != rtmp_recv_){
        rtmp_recv_->set_rtmp_client(NULL);
    }
    srs_freep(sdk);
}

std::string SrsRtcRtmpMuxer::stream_url()
{
    if (req_){
        return req_->get_stream_url();
    }
    return "";
}

void SrsRtcRtmpMuxer::republish()
{
    republish_video = true;
    republish_audio = true;
}