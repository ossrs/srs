/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Runner365
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

#include "srt_to_rtmp.hpp"
#include "stringex.hpp"
#include "time_help.h"
#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_stream.hpp>
#include <list>

std::shared_ptr<srt2rtmp> srt2rtmp::s_srt2rtmp_ptr;

std::shared_ptr<srt2rtmp> srt2rtmp::get_instance() {
    if (!s_srt2rtmp_ptr) {
        s_srt2rtmp_ptr = std::make_shared<srt2rtmp>();
    }
    return s_srt2rtmp_ptr;
}

srt2rtmp::srt2rtmp():_lastcheck_ts(0) {

}

srt2rtmp::~srt2rtmp() {
    release();
}

srs_error_t srt2rtmp::init() {
    srs_error_t err = srs_success;

    if (_trd_ptr.get() != nullptr) {
        return srs_error_wrap(err, "don't start thread again");
    }

    _trd_ptr = std::make_shared<SrsSTCoroutine>("srt2rtmp", this);

    if ((err = _trd_ptr->start()) != srs_success) {
        return srs_error_wrap(err, "start thread");
    }

    srs_trace("srt2rtmp start coroutine...");

    return err;
}

void srt2rtmp::release() {
    if (!_trd_ptr) {
        return;
    }
    _trd_ptr->stop();
    _trd_ptr = nullptr;
}

void srt2rtmp::insert_data_message(unsigned char* data_p, unsigned int len, const std::string& key_path) {
    std::unique_lock<std::mutex> locker(_mutex);

    SRT_DATA_MSG_PTR msg_ptr = std::make_shared<SRT_DATA_MSG>(data_p, len, key_path);
    _msg_queue.push(msg_ptr);
    //_notify_cond.notify_one();
    return;
}

void srt2rtmp::insert_ctrl_message(unsigned int msg_type, const std::string& key_path) {
    std::unique_lock<std::mutex> locker(_mutex);

    SRT_DATA_MSG_PTR msg_ptr = std::make_shared<SRT_DATA_MSG>(key_path, msg_type);
    _msg_queue.push(msg_ptr);
    //_notify_cond.notify_one();
    return;
}
SRT_DATA_MSG_PTR srt2rtmp::get_data_message() {
    std::unique_lock<std::mutex> locker(_mutex);
    SRT_DATA_MSG_PTR msg_ptr;

    if (_msg_queue.empty())
    {
        return msg_ptr;
    }
    //while (_msg_queue.empty()) {
    //    _notify_cond.wait(locker);
    //}

    msg_ptr = _msg_queue.front();
    _msg_queue.pop();
    return msg_ptr;
}

void srt2rtmp::check_rtmp_alive() {
    const int64_t CHECK_INTERVAL    = 5*1000;
    const int64_t ALIVE_TIMEOUT_MAX = 5*1000;

    if (_lastcheck_ts == 0) {
        _lastcheck_ts = now_ms();
        return;
    }
    int64_t timenow_ms = now_ms();

    if ((timenow_ms - _lastcheck_ts) > CHECK_INTERVAL) {
        _lastcheck_ts = timenow_ms;

        for (auto iter = _rtmp_client_map.begin();
            iter != _rtmp_client_map.end();) {
            RTMP_CLIENT_PTR rtmp_ptr = iter->second;

            if ((timenow_ms - rtmp_ptr->get_last_live_ts()) >= ALIVE_TIMEOUT_MAX) {
                srs_warn("srt2rtmp client is timeout, url:%s", 
                    rtmp_ptr->get_url().c_str());
                _rtmp_client_map.erase(iter++);
                rtmp_ptr->close();
            } else {
                iter++;
            }
        }
    }
    return;
}

void srt2rtmp::handle_close_rtmpsession(const std::string& key_path) {
    RTMP_CLIENT_PTR rtmp_ptr;
    auto iter = _rtmp_client_map.find(key_path);
    if (iter == _rtmp_client_map.end()) {
        srs_error("fail to close rtmp session fail, can't find session by key_path:%s", 
            key_path.c_str());
        return;
    }
    rtmp_ptr = iter->second;
    _rtmp_client_map.erase(iter);
    srs_trace("close rtmp session which key_path is %s", key_path.c_str());
    rtmp_ptr->close();
    
    return;
}

//the cycle is running in srs coroutine
srs_error_t srt2rtmp::cycle() {
    srs_error_t err = srs_success;
    _lastcheck_ts = 0;

    while(true) {
        SRT_DATA_MSG_PTR msg_ptr = get_data_message();

        if (!msg_ptr) {
            srs_usleep((30 * SRS_UTIME_MILLISECONDS));
        } else {
            switch (msg_ptr->msg_type()) {
                case SRT_MSG_DATA_TYPE:
                {
                    handle_ts_data(msg_ptr);
                    break;
                }
                case SRT_MSG_CLOSE_TYPE:
                {
                    handle_close_rtmpsession(msg_ptr->get_path());
                    break;
                }
                default:
                {
                    srs_error("srt to rtmp get wrong message type(%u), path:%s",
                        msg_ptr->msg_type(), msg_ptr->get_path().c_str());
                    assert(0);
                }
            }
            
        }
        check_rtmp_alive();
        if ((err = _trd_ptr->pull()) != srs_success) {
            return srs_error_wrap(err, "forwarder");
        }
    }
}

void srt2rtmp::handle_ts_data(SRT_DATA_MSG_PTR data_ptr) {
    RTMP_CLIENT_PTR rtmp_ptr;
    auto iter = _rtmp_client_map.find(data_ptr->get_path());
    if (iter == _rtmp_client_map.end()) {
        srs_trace("new rtmp client for srt upstream, key_path:%s", data_ptr->get_path().c_str());
        rtmp_ptr = std::make_shared<rtmp_client>(data_ptr->get_path());
        _rtmp_client_map.insert(std::make_pair(data_ptr->get_path(), rtmp_ptr));
    } else {
        rtmp_ptr = iter->second;
    }

    rtmp_ptr->receive_ts_data(data_ptr);

    return;
}

rtmp_client::rtmp_client(std::string key_path):_key_path(key_path)
    , _connect_flag(false) {
    const std::string DEF_VHOST = "DEFAULT_VHOST";
    _ts_demux_ptr = std::make_shared<ts_demux>();
    _avc_ptr    = std::make_shared<SrsRawH264Stream>();
    _aac_ptr    = std::make_shared<SrsRawAacStream>();
    std::vector<std::string> ret_vec;

    string_split(key_path, "/", ret_vec);

    if (ret_vec.size() >= 3) {
        _vhost = ret_vec[0];
        _appname = ret_vec[1];
        _streamname = ret_vec[2]; 
    } else {
        _vhost = DEF_VHOST;
        _appname = ret_vec[0];
        _streamname = ret_vec[1]; 
    }
    char url_sz[128];
    
    std::vector<std::string> ip_ports = _srs_config->get_listens();
    int port = 0;
    std::string ip;

    for (auto item : ip_ports) {
        srs_parse_endpoint(item, ip, port);
        if (port != 0) {
            break;
        }
    }
    port = (port == 0) ? 1935 : port;
    if (_vhost == DEF_VHOST) {
        sprintf(url_sz, "rtmp://127.0.0.1:%d/%s/%s", port,
            _appname.c_str(), _streamname.c_str());
    } else {
        sprintf(url_sz, "rtmp://127.0.0.1:%d/%s?vhost=%s/%s", port,
            _appname.c_str(), _vhost.c_str(), _streamname.c_str());
    }
    
    _url = url_sz;

    _h264_sps_changed = false;
    _h264_pps_changed = false;
    _h264_sps_pps_sent = false;

    _last_live_ts = now_ms();
    srs_trace("rtmp client construct url:%s", url_sz);
}

rtmp_client::~rtmp_client() {

}

void rtmp_client::close() {
    _connect_flag = false;
    if (!_rtmp_conn_ptr) {
        return;
    }
    srs_trace("rtmp client close url:%s", _url.c_str());
    _rtmp_conn_ptr->close();
    _rtmp_conn_ptr = nullptr;
    
}

int64_t rtmp_client::get_last_live_ts() {
    return _last_live_ts;
}

std::string rtmp_client::get_url() {
    return _url;
}

srs_error_t rtmp_client::connect() {
    srs_error_t err = srs_success;
    srs_utime_t cto = SRS_CONSTS_RTMP_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_PULSE;

    _last_live_ts = now_ms();
    if (_connect_flag) {
        return srs_success;
    }

    if (_rtmp_conn_ptr.get() != nullptr) {
        return srs_error_wrap(err, "repeated connect %s failed, cto=%dms, sto=%dms.", 
                            _url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }

    _rtmp_conn_ptr = std::make_shared<SrsSimpleRtmpClient>(_url, cto, sto);

    if ((err = _rtmp_conn_ptr->connect()) != srs_success) {
        close();
        return srs_error_wrap(err, "connect %s failed, cto=%dms, sto=%dms.", 
                            _url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }
    
    if ((err = _rtmp_conn_ptr->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        close();
        return srs_error_wrap(err, "publish error, url:%s", _url.c_str());
    }
    _connect_flag = true;
    return err;
}

void rtmp_client::receive_ts_data(SRT_DATA_MSG_PTR data_ptr) {
    _ts_demux_ptr->decode(data_ptr, shared_from_this());//on_data_callback is the decode callback
    return;
}

srs_error_t rtmp_client::write_h264_sps_pps(uint32_t dts, uint32_t pts) {
    srs_error_t err = srs_success;
    
    // TODO: FIMXE: there exists bug, see following comments.
    // when sps or pps changed, update the sequence header,
    // for the pps maybe not changed while sps changed.
    // so, we must check when each video ts message frame parsed.
    if (!_h264_sps_changed || !_h264_pps_changed) {
        return err;
    }
    
    // h264 raw to h264 packet.
    std::string sh;
    if ((err = _avc_ptr->mux_sequence_header(_h264_sps, _h264_pps, dts, pts, sh)) != srs_success) {
        return srs_error_wrap(err, "mux sequence header");
    }
    
    // h264 packet to flv packet.
    int8_t frame_type = SrsVideoAvcFrameTypeKeyFrame;
    int8_t avc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = _avc_ptr->mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "avc to flv");
    }
    
    if (_srs_config->get_srt_mix_correct()) {
        _rtmp_queue.insert_rtmp_data((unsigned char*)flv, nb_flv, (int64_t)dts, SrsFrameTypeVideo);
        rtmp_write_work();
    } else {
        rtmp_write_packet(SrsFrameTypeVideo, dts, flv, nb_flv);
    }

    // reset sps and pps.
    _h264_sps_changed = false;
    _h264_pps_changed = false;
    _h264_sps_pps_sent = true;
    
    return err;
}

srs_error_t rtmp_client::write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts) {
    srs_error_t err = srs_success;
    
    // when sps or pps not sent, ignore the packet.
    // @see https://github.com/ossrs/srs/issues/203
    if (!_h264_sps_pps_sent) {
        return srs_error_new(ERROR_H264_DROP_BEFORE_SPS_PPS, "drop sps/pps");
    }
    
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
    if ((err = _avc_ptr->mux_ipb_frame(frame, frame_size, ibp)) != srs_success) {
        return srs_error_wrap(err, "mux frame");
    }
    
    int8_t avc_packet_type = SrsVideoAvcFrameTraitNALU;
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = _avc_ptr->mux_avc2flv(ibp, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux avc to flv");
    }
    if (_srs_config->get_srt_mix_correct()) {
        _rtmp_queue.insert_rtmp_data((unsigned char*)flv, nb_flv, (int64_t)dts, SrsFrameTypeVideo);
        rtmp_write_work();
    } else {
        rtmp_write_packet(SrsFrameTypeVideo, dts, flv, nb_flv);
    }

    return err;
}

srs_error_t rtmp_client::write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts) {
    srs_error_t err = srs_success;
    
    char* data = NULL;
    int size = 0;
    if ((err = _aac_ptr->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success) {
        return srs_error_wrap(err, "mux aac to flv");
    }
    if (_srs_config->get_srt_mix_correct()) {
        _rtmp_queue.insert_rtmp_data((unsigned char*)data, size, (int64_t)dts, SrsFrameTypeAudio);
        rtmp_write_work();
    } else {
        rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
    }

    return err;
}

srs_error_t rtmp_client::rtmp_write_packet(char type, uint32_t timestamp, char* data, int size) {
    srs_error_t err = srs_success;
    SrsSharedPtrMessage* msg = NULL;

    if ((err = srs_rtmp_create_msg(type, timestamp, data, size, _rtmp_conn_ptr->sid(), &msg)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }
    srs_assert(msg);
    
    // send out encoded msg.
    if ((err = _rtmp_conn_ptr->send_and_free_message(msg)) != srs_success) {
        close();
        return srs_error_wrap(err, "send messages");
    }
    
    return err;
}

void rtmp_client::rtmp_write_work() {
    rtmp_packet_info_s packet_info;
    bool ret = false;
    
    do {
        ret = _rtmp_queue.get_rtmp_data(packet_info);
        if (ret) {
            rtmp_write_packet(packet_info._type, packet_info._dts, (char*)packet_info._data, packet_info._len);
        }
    } while(ret);
    return;
}

srs_error_t rtmp_client::on_ts_video(std::shared_ptr<SrsBuffer> avs_ptr, uint64_t dts, uint64_t pts) {
    srs_error_t err = srs_success;

    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    dts = dts / 90;
    pts = pts / 90;

    if (dts == 0) {
        dts = pts;
    }

    // send each frame.
    while (!avs_ptr->empty()) {
        char* frame = NULL;
        int frame_size = 0;
        if ((err = _avc_ptr->annexb_demux(avs_ptr.get(), &frame, &frame_size)) != srs_success) {
            return srs_error_wrap(err, "demux annexb");
        }
        
        //srs_trace_data(frame, frame_size, "video annexb demux:");
        // 5bits, 7.3.1 NAL unit syntax,
        // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
        //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
        SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
        
        // ignore the nalu type sps(7), pps(8), aud(9)
        if (nal_unit_type == SrsAvcNaluTypeAccessUnitDelimiter) {
            continue;
        }

        // TODO: FIXME: Should cache this config, it's better not to get it for each video frame.
        if (_srs_config->get_srt_sei_filter()) {
            if (nal_unit_type == SrsAvcNaluTypeSEI) {
                continue;
            }
        }
        
        // for sps
        if (_avc_ptr->is_sps(frame, frame_size)) {
            std::string sps;
            if ((err = _avc_ptr->sps_demux(frame, frame_size, sps)) != srs_success) {
                return srs_error_wrap(err, "demux sps");
            }
            
            if (_h264_sps == sps) {
                continue;
            }
            _h264_sps_changed = true;
            _h264_sps = sps;
            
            if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write sps/pps");
            }
            continue;
        }
        
        // for pps
        if (_avc_ptr->is_pps(frame, frame_size)) {
            std::string pps;
            if ((err = _avc_ptr->pps_demux(frame, frame_size, pps)) != srs_success) {
                return srs_error_wrap(err, "demux pps");
            }
            
            if (_h264_pps == pps) {
                continue;
            }
            _h264_pps_changed = true;
            _h264_pps = pps;
            
            if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write sps/pps");
            }
            continue;
        }
        
        // ibp frame.
        // TODO: FIXME: we should group all frames to a rtmp/flv message from one ts message.
        srs_info("mpegts: demux avc ibp frame size=%d, dts=%d", frame_size, dts);
        if ((err = write_h264_ipb_frame(frame, frame_size, dts, pts)) != srs_success) {
            return srs_error_wrap(err, "write frame");
        }
        _last_live_ts = now_ms();
    }
    
    return err;
}

int rtmp_client::get_sample_rate(char sample_index) {
    int sample_rate = 44100;

    if ((sample_index >= 0) && (sample_index < SrsAAcSampleRateNumbers)) {
        sample_rate = srs_aac_srates[(uint8_t)sample_index];
    }

    return sample_rate;
}

srs_error_t rtmp_client::on_ts_audio(std::shared_ptr<SrsBuffer> avs_ptr, uint64_t dts, uint64_t pts) {
    srs_error_t err = srs_success;
    uint64_t base_dts;
    uint64_t real_dts;
    uint64_t first_dts;
    int index = 0;
    int sample_size = 1024;

    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    
    base_dts = dts/90;
    if (base_dts == 0) {
        base_dts = pts/90;
    }
    
    // send each frame.
    while (!avs_ptr->empty()) {
        char* frame = NULL;
        int frame_size = 0;
        SrsRawAacStreamCodec codec;
        if ((err = _aac_ptr->adts_demux(avs_ptr.get(), &frame, &frame_size, codec)) != srs_success) {
            return srs_error_wrap(err, "demux adts");
        }

        if (frame_size <= 0) {
            continue;
        }
        int sample_rate = get_sample_rate(codec.sound_rate);

        if (codec.aac_packet_type > SrsAudioOpusFrameTraitRaw) {
            sample_size = 2048;
        } else {
            sample_size = 1024;
        }

        real_dts = base_dts + index * 1000.0 * sample_size / sample_rate;
        if (index == 0) {
            first_dts = real_dts;
        }
        index++;

        // generate sh.
        if (_aac_specific_config.empty()) {
            std::string sh;
            if ((err = _aac_ptr->mux_sequence_header(&codec, sh)) != srs_success) {
                return srs_error_wrap(err, "mux sequence header");
            }
            _aac_specific_config = sh;
            
            codec.aac_packet_type = 0;
            
            if ((err = write_audio_raw_frame((char*)sh.data(), (int)sh.length(), &codec, real_dts)) != srs_success) {
                return srs_error_wrap(err, "write raw audio frame");
            }
        }
        
        // audio raw data.
        codec.aac_packet_type = 1;
        if ((err = write_audio_raw_frame(frame, frame_size, &codec, real_dts)) != srs_success) {
            return srs_error_wrap(err, "write audio raw frame");
        }
        _last_live_ts = now_ms();
    }
    
    uint64_t diff_t = real_dts - first_dts;
    diff_t += 100;
    if ((diff_t > 200) && (diff_t < 600)) {
        srs_info("set_queue_timeout timeout:%lu", diff_t);
        _rtmp_queue.set_queue_timeout(diff_t);
    }
    return err;
}

void rtmp_client::on_data_callback(SRT_DATA_MSG_PTR data_ptr, unsigned int media_type,
                                uint64_t dts, uint64_t pts)
{
    if (!data_ptr || (data_ptr->get_data() == nullptr) || (data_ptr->data_len() == 0)) {
        assert(0);
        return;
    }

    auto avs_ptr = std::make_shared<SrsBuffer>((char*)data_ptr->get_data(), data_ptr->data_len());

    if (media_type == STREAM_TYPE_VIDEO_H264) {
        on_ts_video(avs_ptr, dts, pts);
    } else if (media_type == STREAM_TYPE_AUDIO_AAC) {
        on_ts_audio(avs_ptr, dts, pts);
    } else {
        srs_error("mpegts demux unkown stream type:0x%02x, only support h264+aac", media_type);
    }
    return;
}

rtmp_packet_queue::rtmp_packet_queue():_queue_timeout(QUEUE_DEF_TIMEOUT)
    ,_queue_maxlen(QUEUE_LEN_MAX)
    ,_first_packet_t(-1)
    ,_first_local_t(-1) {

}

rtmp_packet_queue::~rtmp_packet_queue() {
    for (auto item : _send_map) {
        rtmp_packet_info_s info = item.second;
        if (info._data) {
            delete info._data;
        }
    }
    _send_map.clear();
}

void rtmp_packet_queue::set_queue_timeout(int64_t queue_timeout) {
    _queue_timeout = queue_timeout;
}

void rtmp_packet_queue::insert_rtmp_data(unsigned char* data, int len, int64_t dts, char media_type) {
    rtmp_packet_info_s packet_info;

    packet_info._data = data;
    packet_info._len  = len;
    packet_info._dts  = dts;
    packet_info._type = media_type;

    if (_first_packet_t == -1) {
        _first_packet_t = dts;
        _first_local_t = (int64_t)now_ms();
    }

    _send_map.insert(std::make_pair(dts, packet_info));
    return;
}

bool rtmp_packet_queue::is_ready() {
    if (!_srs_config->get_srt_mix_correct() && !_send_map.empty()) {
        return true;
    }
    if (_send_map.size() < 2) {
        return false;
    }

    if (_send_map.size() >= (size_t)_queue_maxlen) {
        return true;
    }

    auto first_item = _send_map.begin();
    int64_t now_t = (int64_t)now_ms();

    int64_t diff_t = (now_t - _first_local_t) - (first_item->first - _first_packet_t);

    if (diff_t >= _queue_timeout) {
        return true;
    }
    return false;
}

bool rtmp_packet_queue::get_rtmp_data(rtmp_packet_info_s& packet_info) {
    if (!is_ready()) {
        return false;
    }
    auto iter = _send_map.begin();
    packet_info = iter->second;
    _send_map.erase(iter);

    return true;
}