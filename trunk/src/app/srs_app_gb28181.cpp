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

#include <srs_app_gb28181.hpp>

#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

using namespace std;

#include <srs_app_rtsp.hpp>
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
#include <srs_app_rtmp_conn.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_format.hpp>
#include <srs_sip_stack.hpp>
#include <srs_core_autofree.hpp>


//#define W_PS_FILE
//#define W_VIDEO_FILE
//#define W_AUDIO_FILE

SrsPsRtpPacket::SrsPsRtpPacket()
{ 
}

SrsPsRtpPacket::~SrsPsRtpPacket()
{
}

srs_error_t SrsPsRtpPacket::decode(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // 12bytes header
    if (!stream->require(12)) {
        return srs_error_new(ERROR_RTP_HEADER_CORRUPT, "requires 12 only %d bytes", stream->left());
    }
    
    int8_t vv = stream->read_1bytes();
    version = (vv >> 6) & 0x03;
    padding = (vv >> 5) & 0x01;
    extension = (vv >> 4) & 0x01;
    csrc_count = vv & 0x0f;
    
    int8_t mv = stream->read_1bytes();
    marker = (mv >> 7) & 0x01;
    payload_type = mv & 0x7f;
    
    sequence_number = stream->read_2bytes();
    timestamp = stream->read_4bytes();
    ssrc = stream->read_4bytes();
    
    // TODO: FIXME: check sequence number.
    
    // video codec.
    if (payload_type == 96) {
        // ps stream atleast 4bytes content.
        if (!stream->require(4)) {
            return srs_error_new(ERROR_RTP_TYPE96_CORRUPT, "requires 4 only %d bytes", stream->left());
        }
        
        // append left bytes to payload.
        payload->append(stream->data() + stream->pos() , stream->size()-stream->pos());

    } 
    return err;
}


SrsPsRtpConn::SrsPsRtpConn(SrsGb28181Conn* conn, int p, std::string sid, bool b, bool k)
{
    gb28181 = conn;
    _port = p;
    session_id = sid;
    // TODO: support listen at <[ip:]port>
    listener = new SrsUdpListener(this, srs_any_address_for_listener(), p);
    cache = new SrsPsRtpPacket();
    pprint = SrsPithyPrint::create_caster();
    pre_timestamp = -1;

    audio_enable = b;
    first_keyframe_flag = false;
    wait_first_keyframe = k;
}

SrsPsRtpConn::~SrsPsRtpConn()
{
    ps_fw.close();
    video_fw.close();
    audio_fw.close();

    dispose();

    srs_freep(listener);
    srs_freep(cache);
    srs_freep(pprint);
}

int SrsPsRtpConn::port()
{
    return _port;
}

srs_error_t SrsPsRtpConn::listen()
{
    return listener->listen();
}

void SrsPsRtpConn::dispose()
{
    map<uint32_t, SrsSimpleStream*>::iterator it;
    for (it = cache_payload.begin(); it != cache_payload.end(); ++it) {
        srs_freep(it->second);
    }

    cache_payload.clear();
     return;
}

int64_t  SrsPsRtpConn::parse_ps_timestamp(const uint8_t* p)
{
	unsigned long b;
	//total 33 bits
	unsigned long val, val2, val3;

	//1st byte, 5、6、7 bit
	b = *p++;
	val = (b & 0x0e);

	//2 byte, all bit 
	b = (*(p++)) << 8;
    //3 bytes 1--7 bit
	b += *(p++);
	val2 = (b & 0xfffe) >> 1;
	
	//4 byte, all bit
	b = (*(p++)) << 8;
    //5 byte 1--7 bit
	b += *(p++);
	val3 = (b & 0xfffe) >> 1;

    //<32--val--30> <29----val2----15> <14----val3----0>
	val = (val << 29) | (val2 << 15) | val3;
	return val;
}

srs_error_t SrsPsRtpConn::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
{
    srs_error_t err = srs_success;
    bool completed = false;
    int keyframe = 0;
    
    pprint->elapse();
    
    if (true) {
        SrsBuffer stream(buf, nb_buf);
        
        SrsPsRtpPacket pkt;
        
        if ((err = pkt.decode(&stream)) != srs_success) {
            srs_trace("decode error");
            return srs_success;
            //return srs_error_wrap(err, "decode");
        }

        if (pre_timestamp == -1) {
            pre_timestamp = pkt.timestamp;
        }

        //cache pkt payload by timestamp
        if (cache_payload.find(pkt.timestamp) == cache_payload.end()) {
            cache_payload[pkt.timestamp] = new SrsSimpleStream();
        }

        cache_payload[pkt.timestamp]->append(pkt.payload);
        
        uint32_t cur_timestamp = pkt.timestamp;

        
        if (pkt.marker) {
            completed = true;
        }else if (pre_timestamp != pkt.timestamp){
            if (cache_payload.find(pre_timestamp) != cache_payload.end()) {
                completed = true;
                cur_timestamp = pre_timestamp;
            }
        }

        pre_timestamp =  pkt.timestamp;

        if (pprint->can_print()) {
            srs_trace("<- " SRS_CONSTS_LOG_STREAM_CASTER " gb28181: client_id %s, ps rtp packet %dB, age=%d, vt=%d/%u, sts=%u/%u/%#x, paylod=%dB",
                        session_id.c_str(), nb_buf, pprint->age(), pkt.version, pkt.payload_type, pkt.sequence_number, pkt.timestamp, pkt.ssrc,
                        pkt.payload->length()
                        );
        }

        if (!completed){
            return err;
        }

        //process completed frame data
        char *payload = cache_payload[cur_timestamp]->bytes();
        int  payload_len = cache_payload[cur_timestamp]->length();
      
        on_ps_stream(payload, payload_len, cur_timestamp);

        //clear processed one ps frame
        map<uint32_t, SrsSimpleStream*>::iterator key = cache_payload.find(cur_timestamp);
        if(key!=cache_payload.end())
        {
            srs_freep(key->second);
            cache_payload.erase(key);
        }
    }
  
    return err;
}

bool SrsPsRtpConn::can_send_ps_av_packet(){
    if (!wait_first_keyframe)
        return true;
    
    if (first_keyframe_flag)
       return true;

    return false;
}


srs_error_t SrsPsRtpConn::on_ps_stream(char* ps_data, int ps_size, uint32_t timestamp)
{
    srs_error_t err = srs_success;

    int complete_len = 0;
    int incomplete_len = ps_size;
    char *next_ps_pack = ps_data;

    SrsSimpleStream video_stream;
    SrsSimpleStream audio_stream;
    uint64_t audio_pts = 0;
    uint64_t video_pts = 0;
    int keyframe = 0;
    int pse_index = 0;

#ifdef W_PS_FILE           
        if (!ps_fw.is_open()) {
                std::string filename = "test_ps_" + session_id + ".mpg";
                ps_fw.open(filename.c_str());
        }
        ps_fw.write(ps_data, ps_size, NULL);          
#endif

	while(incomplete_len >= sizeof(SrsPsPacketStartCode))
    {
    	if (next_ps_pack
			&& next_ps_pack[0] == (char)0x00
			&& next_ps_pack[1] == (char)0x00
			&& next_ps_pack[2] == (char)0x01
			&& next_ps_pack[3] == (char)0xBA)
		{
            //ps header 
            SrsPsPacketHeader *head = (SrsPsPacketHeader *)next_ps_pack;
            unsigned char pack_stuffing_length = head->stuffing_length & 0x07;
        
            next_ps_pack = next_ps_pack + sizeof(SrsPsPacketHeader) + pack_stuffing_length;
            complete_len = complete_len + sizeof(SrsPsPacketHeader) + pack_stuffing_length;
            incomplete_len = ps_size - complete_len;
        }
        else if(next_ps_pack
			&& next_ps_pack[0] == (char)0x00
			&& next_ps_pack[1] == (char)0x00
			&& next_ps_pack[2] == (char)0x01
			&& next_ps_pack[3] == (char)0xBB)
        {
            //ps system header 
            SrsPsPacketBBHeader *bbhead=(SrsPsPacketBBHeader *)(next_ps_pack);
            int bbheaderlen = htons(bbhead->length);
            next_ps_pack = next_ps_pack + sizeof(SrsPsPacketBBHeader) + bbheaderlen;
            complete_len = complete_len + sizeof(SrsPsPacketBBHeader) + bbheaderlen;
            incomplete_len = ps_size - complete_len;

            first_keyframe_flag = true;
        }
        else if(next_ps_pack
			&& next_ps_pack[0] == (char)0x00
			&& next_ps_pack[1] == (char)0x00
			&& next_ps_pack[2] == (char)0x01
			&& next_ps_pack[3] == (char)0xBC)
        {
            //program stream map 

		    SrsPsMapPacket* psmap_pack = (SrsPsMapPacket*)next_ps_pack;
          
            psmap_pack->length = htons(psmap_pack->length);
          
            next_ps_pack = next_ps_pack + psmap_pack->length + sizeof(SrsPsMapPacket);
            complete_len = complete_len + psmap_pack->length + sizeof(SrsPsMapPacket);
            incomplete_len = ps_size - complete_len;
    
        }
        else if(next_ps_pack
			&& next_ps_pack[0] == (char)0x00
			&& next_ps_pack[1] == (char)0x00
			&& next_ps_pack[2] == (char)0x01
			&& next_ps_pack[3] == (char)0xE0)
        {
            //pse  

            SrsPsePacket* pse_pack = (SrsPsePacket*)next_ps_pack;

            unsigned char pts_dts_flags = (pse_pack->info[0] & 0xF0) >> 6;
            int64_t pts = 0;
            if (pse_index == 0 && pts_dts_flags > 0) {
				pts = parse_ps_timestamp((unsigned char*)next_ps_pack + 9);
                //srs_trace("vvvvvvvvvvvvvvvvvvvvvvv ts=%u pkt_ts=%u", pts, timestamp);
			}

            if (pse_index == 0) video_pts = pts;

            pse_index +=1;

            int packlength = htons(pse_pack->length);
            int payloadlen = packlength - 2 - 1 - pse_pack->stuffing_length;
         
            next_ps_pack = next_ps_pack + 9 + pse_pack->stuffing_length;
            complete_len = complete_len + 9 + pse_pack->stuffing_length;

            video_stream.append(next_ps_pack, payloadlen);

#ifdef W_VIDEO_FILE            
            if (!video_fw.is_open()) {
                 std::string filename = "test_video_" + session_id + ".h264";
                 video_fw.open(filename.c_str());
            }
            video_fw.write(next_ps_pack,  payloadlen, NULL);          
#endif

            next_ps_pack = next_ps_pack + payloadlen;
            complete_len = complete_len + payloadlen;
            incomplete_len = ps_size - complete_len;


            //srs_trace("====================== V pts=%u", pts);
        }
     	else if (next_ps_pack
			&& next_ps_pack[0] == (char)0x00
			&& next_ps_pack[1] == (char)0x00
			&& next_ps_pack[2] == (char)0x01
			&& next_ps_pack[3] == (char)0xBD)
        {
            //private stream 

			SrsPsePacket* pse_pack = (SrsPsePacket*)next_ps_pack;
			
            int packlength = htons(pse_pack->length);
			int payload_len = packlength - 2 - 1 - pse_pack->stuffing_length;
            
			next_ps_pack = next_ps_pack + payload_len + 9 + pse_pack->stuffing_length;
            complete_len = complete_len + (payload_len + 9 + pse_pack->stuffing_length);
            incomplete_len = ps_size - complete_len;
		}
		else if (next_ps_pack
			&& next_ps_pack[0] == (char)0x00
			&& next_ps_pack[1] == (char)0x00
			&& next_ps_pack[2] == (char)0x01
			&& next_ps_pack[3] == (char)0xC0)
        {
            //audio stream
            
            SrsPsePacket* pse_pack = (SrsPsePacket*)next_ps_pack;

		    unsigned char pts_dts_flags = (pse_pack->info[0] & 0xF0) >> 6;
			if (pts_dts_flags > 0 ) {
				audio_pts = parse_ps_timestamp((unsigned char*)next_ps_pack + 9);
                //srs_trace("aaaaaaaaaaaaaaaaaaaaaaaaaa ts=%u pkt_ts=%u", audio_pts, timestamp);
			}

			int packlength = htons(pse_pack->length);
			int payload_len = packlength - 2 - 1 - pse_pack->stuffing_length;
            next_ps_pack = next_ps_pack + 9 + pse_pack->stuffing_length;

            audio_stream.append(next_ps_pack, payload_len);

#ifdef W_AUDIO_FILE            
            if (!audio_fw.is_open()) {
                 std::string filename = "test_audio_" + session_id + ".aac";
                 audio_fw.open(filename.c_str());
            }
            audio_fw.write(next_ps_pack,  payload_len, NULL);          
#endif
            
			next_ps_pack = next_ps_pack + payload_len;
            complete_len = complete_len + (payload_len + 9 + pse_pack->stuffing_length);
            incomplete_len = ps_size - complete_len;

            if (audio_enable && audio_stream.length() && can_send_ps_av_packet()) {
                if ((err = gb28181->on_rtp_audio(&audio_stream, audio_pts)) != srs_success) {
                    srs_trace("process ps audio packet error %s", err);
                    //return srs_success;
                    //return srs_error_wrap(err, "process rtp packet");
                }
            }
		}
        else
        {
			srs_trace("gb28181: client_id %s, unkonw ps data %02x %02x %02x %02x\n", 
               session_id.c_str(), next_ps_pack[0], next_ps_pack[1], next_ps_pack[2], next_ps_pack[3]);
            //srs_trace(" ps ps_size=%d  complete=%d h264len=%d\n", ps_size, complete_len, *h264length);
            break;
        }
    }

    if (complete_len != ps_size){
         srs_trace("gb28181:  client_id %s decode ps packet error! ps_size=%d  complete=%d \n", 
                     session_id.c_str(), ps_size, complete_len);
    }else if (video_stream.length() && can_send_ps_av_packet()) {
         if ((err = gb28181->on_rtp_video(&video_stream, video_pts, keyframe)) != srs_success) {
            srs_trace("process ps video packet error");
            //return srs_success;
            return srs_error_wrap(err, "process ps video packet error");
        }
    }
  
    return err;
}

SrsGb28281ClientInfo::SrsGb28281ClientInfo() 
{
    req = new SrsSipRequest();
}

SrsGb28281ClientInfo::~SrsGb28281ClientInfo() 
{
    srs_freep(req);
}


SrsGb28181Conn::SrsGb28181Conn(SrsGb28181Caster* c, std::string id)
{
    session_id = id;

    video_rtp = NULL;
    audio_rtp = NULL;
  
    req = NULL;
    caster = c;
    info = new SrsGb28281ClientInfo;
    pprint = SrsPithyPrint::create_caster();
    
    skt = new SrsStSocket();
    sip = new SrsSipStack();
    trd = new SrsSTCoroutine("gb28181conn", this);
    
    sdk = NULL;
    vjitter = new SrsRtspJitter();
    ajitter = new SrsRtspJitter();
    
    avc = new SrsRawH264Stream();
    aac = new SrsRawAacStream();

    h264_sps_changed = false;
    h264_pps_changed = false;
    h264_sps_pps_sent = false;

    reg_expires = 3600;
    register_time = 0;
    alive_time = 0;
    invite_time = 0;
    register_status = Srs28181Unkonw;
    alive_status = Srs28181Unkonw;
    invite_status = Srs28181Unkonw;
}

SrsGb28181Conn::~SrsGb28181Conn()
{
    close();
    
    srs_freep(info);
     
    srs_freep(video_rtp);
    srs_freep(audio_rtp);
    
    srs_freep(trd);
    srs_freep(skt);
    srs_freep(sip);
    
    srs_freep(sdk);
    srs_freep(req);
    
    srs_freep(vjitter);
    srs_freep(ajitter);

    srs_freep(pprint);
}

srs_error_t SrsGb28181Conn::serve()
{
    srs_error_t err = srs_success;
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "gb28181 connection");
    }
    
    return err;
}

std::string SrsGb28181Conn::remote_ip()
{
    return "";
}

void SrsGb28181Conn::set_request_info(SrsSipRequest *req)
{
    srs_assert(req != NULL);
    info->req->copy(req);
}

std::string SrsGb28181Conn::get_session_id()
{
    return session_id;
}

srs_error_t SrsGb28181Conn::do_cycle()
{
    srs_error_t err = srs_success;
     
    // consume all sip messages.
    while (true) {

        pprint->elapse();

        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "gb28181conn cycle");
        }

        srs_utime_t now = srs_get_system_time();
        SrsGb28181Config config = caster->GetGb28181Config();

        srs_utime_t reg_duration = 0;
        srs_utime_t invite_duration = 0;
        srs_utime_t alive_duration = 0;
        srs_utime_t recv_rtp_duration = 0;

        if (register_status == Srs28181RegisterOk && register_time > 0) {
            reg_duration = (now - register_time) / (1000*1000);
            if (reg_duration > reg_expires) {
                register_status = Srs28181Unkonw;
                alive_status = Srs28181Unkonw;
                invite_status = Srs28181Unkonw;
                stop_rtp_listen();
            }
        }
      
        if (alive_status == Srs28181AliveOk && alive_time > 0){
            alive_duration = (now - alive_time) / (1000*1000);
            if (alive_duration > config.sip_keepalive_timeout) {
                srs_trace("gb28181: client id=%s alive timeout, remove conn", session_id.c_str());
                break;
            }
        }

        if (invite_status && invite_time > 0) {
            invite_duration = (now - invite_time) / (1000*1000);
            if (invite_status == Srs28181Trying && invite_duration > config.sip_ack_timeout) {
                invite_status = Srs28181Unkonw;
                stop_rtp_listen();
            }

            recv_rtp_duration = (now - recv_rtp_time) / (1000*1000);
            if (recv_rtp_duration > config.rtp_idle_timeout) {
                invite_status = Srs28181Unkonw;
                stop_rtp_listen();
            }
        }
       
       if (pprint->can_print()) {
            srs_trace("gb28181: client id=%s, druation reg=%u alive=%u invite=%u",
            session_id.c_str(), reg_duration, alive_duration, invite_duration);

            srs_trace("gb28181: client id=%s, status reg_status=%u alive_status=%u invite_status=%u",
            session_id.c_str(), register_status, alive_status, invite_status);
       }
     
        srs_usleep(1000 * 1000);
    }
    
    return err;
}

srs_error_t SrsGb28181Conn::start_rtp_listen(int port)
{
    srs_error_t err = srs_success;
    
    SrsPsRtpConn* rtp = NULL;
    srs_freep(video_rtp);
    SrsGb28181Config config = caster->GetGb28181Config();
    rtp = video_rtp = new SrsPsRtpConn(this, port, session_id, 
                config.audio_enable, config.wait_keyframe);

    if ((err = rtp->listen()) != srs_success) {
        return srs_error_wrap(err, "rtp listen");
    }
    srs_trace("gb28181conn: start rtp ps stream over server-port=%d", port);

    return err;

}

srs_error_t SrsGb28181Conn::stop_rtp_listen()
{
    srs_error_t err = srs_success;

    if (video_rtp) {
        caster->free_port(video_rtp->port(), video_rtp->port() + 1);
        srs_freep(video_rtp);
    } 
    
    if (audio_rtp) {
        caster->free_port(audio_rtp->port(), audio_rtp->port() + 1);
        srs_freep(audio_rtp);
    }

    //stop rtmp publish
    close();

    return err;
}


srs_error_t SrsGb28181Conn::cycle()
{
    // serve the sip client.
    srs_error_t err = do_cycle();
    
    stop_rtp_listen();

    caster->remove(this);
    srs_trace("gb28181conn: client id=%d conn is remove", session_id.c_str());
    
    if (err == srs_success) {
        srs_trace("client finished.");
    } else if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. code=%d", srs_error_code(err));
        srs_freep(err);
    }
   
    return err;
}

srs_error_t SrsGb28181Conn::on_rtp_video(SrsSimpleStream *stream, int64_t fpts, int keyframe)
{
    srs_error_t err = srs_success;
   
    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    
    if ((err = vjitter->correct(fpts)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }

    
    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(fpts / 90);
    uint32_t pts = (uint32_t)(fpts / 90);

    recv_rtp_time = srs_get_system_time();

    //srs_trace("==========================================VVV pts=%u", dts);
    SrsBuffer *avs = new SrsBuffer(stream->bytes(), stream->length());
    SrsAutoFree(SrsBuffer, avs);
    // send each frame.
    while (!avs->empty()) {
        char* frame = NULL;
        int frame_size = 0;
        if ((err = avc->annexb_demux(avs, &frame, &frame_size)) != srs_success) {
            return srs_error_wrap(err, "demux annexb");
        }
        
        // 5bits, 7.3.1 NAL unit syntax,
        // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
        //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
        SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
        
        // ignore the nalu type sei(6) aud(9) 
        if (nal_unit_type == SrsAvcNaluTypeAccessUnitDelimiter ||
            nal_unit_type == SrsAvcNaluTypeSEI) {
            continue;
        }
      
        // for sps
        if (avc->is_sps(frame, frame_size)) {
            std::string sps;
            if ((err = avc->sps_demux(frame, frame_size, sps)) != srs_success) {
                return srs_error_wrap(err, "demux sps");
            }
            
            if (h264_sps == sps) {
                continue;
            }
            h264_sps_changed = true;
            h264_sps = sps;
            
            if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write sps/pps");
            }
            continue;
        }
        
        // for pps
        if (avc->is_pps(frame, frame_size)) {
            std::string pps;
            if ((err = avc->pps_demux(frame, frame_size, pps)) != srs_success) {
                return srs_error_wrap(err, "demux pps");
            }
            
            if (h264_pps == pps) {
                continue;
            }
            h264_pps_changed = true;
            h264_pps = pps;
            
            if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write sps/pps");
            }
            continue;
        }
        
        // ibp frame.
        // TODO: FIXME: we should group all frames to a rtmp/flv message from one ts message.
        srs_info("gb28181: demux avc ibp frame size=%d, dts=%d", frame_size, dts);
        if ((err = write_h264_ipb_frame(frame, frame_size, dts, pts)) != srs_success) {
            return srs_error_wrap(err, "write frame");
        }
    }
    
    return err;
}

srs_error_t SrsGb28181Conn::on_rtp_audio(SrsSimpleStream* stream, int64_t fdts)
{
    srs_error_t err = srs_success;
    
    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    
    // sip tbn is ts tbn.
    if ((err = ajitter->correct(fdts)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }
    
    recv_rtp_time = srs_get_system_time();

    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(fdts / 90);

   // send each frame.
   SrsBuffer  *avs = new SrsBuffer(stream->bytes(), stream->length());
   SrsAutoFree(SrsBuffer, avs);
   if (!avs->empty()) {
        char* frame = NULL;
        int frame_size = 0;
        SrsRawAacStreamCodec codec;
        if ((err = aac->adts_demux(avs, &frame, &frame_size, codec)) != srs_success) {
            return srs_error_wrap(err, "demux adts");
        }

        if (frame_size <= 0) {
            return err;
        }

        bool send_adts = false;
        static int srs_aac_srates[] = {
            96000, 88200, 64000, 48000,
            44100, 32000, 24000, 22050,
            16000, 12000, 11025,  8000,
            7350,     0,     0,    0
        };
        switch (srs_aac_srates[codec.sampling_frequency_index]) {
            case 11025:
                codec.sound_rate = SrsAudioSampleRate11025;
                break;
            case 22050:
                codec.sound_rate = SrsAudioSampleRate22050;
                break;
            case 44100:
                codec.sound_rate = SrsAudioSampleRate44100;
                break;
            default:
                send_adts = true; //raw with adts
                break;
        };

        std::string sh;
        if ((err = aac->mux_sequence_header(&codec, sh)) != srs_success) {
            return srs_error_wrap(err, "mux sequence header");
        }
        
        if (aac_specific_config != sh){
            std::string sh;
            if ((err = aac->mux_sequence_header(&codec, sh)) != srs_success) {
                return srs_error_wrap(err, "mux sequence header");
            }
            aac_specific_config = sh;
            codec.aac_packet_type = 0;
            if ((err = write_audio_raw_frame((char*)sh.data(), (int)sh.length(), &codec, dts)) != srs_success) {
                return srs_error_wrap(err, "write raw audio frame");
            }
        }

        codec.aac_packet_type = 1;
        if  (send_adts) {  // audio raw data. with  adts header
            if ((err = write_audio_raw_frame(stream->bytes(), stream->length(), &codec, dts)) != srs_success) {
                    return srs_error_wrap(err, "write audio raw frame");
                }
        }else {  // audio raw data. without  adts header
             if ((err = write_audio_raw_frame(frame, frame_size, &codec, dts)) != srs_success) {
                    return srs_error_wrap(err, "write audio raw frame");
                }
        }
    }//end if (!avs->empty()) 
   
    return err;
}

srs_error_t SrsGb28181Conn::write_h264_sps_pps(uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;

    if (!h264_sps_changed || !h264_pps_changed) {
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

    // reset sps and pps.
    h264_sps_changed = false;
    h264_pps_changed = false;
    h264_sps_pps_sent = true;
    
    return err;
}

srs_error_t SrsGb28181Conn::write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts)
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

srs_error_t SrsGb28181Conn::write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts)
{
    srs_error_t err = srs_success;
    
    char* data = NULL;
    int size = 0;
    if ((err = aac->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success) {
        return srs_error_wrap(err, "mux aac to flv");
    }
    
    return rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
}

srs_error_t SrsGb28181Conn::rtmp_write_packet(char type, uint32_t timestamp, char* data, int size)
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

srs_error_t SrsGb28181Conn::connect()
{
    srs_error_t err = srs_success;
    
    // Ignore when connected.
    if (sdk) {
        return err;
    }
    
    // generate rtmp url to connect to.
    std::string url;
    if (true) {
        std::string schema, host, vhost, app, param;
        int port;
        //srs_discovery_tc_url(rtmp_url, schema, host, vhost, app, rtsp_stream, port, param);

        // generate output by template.
        std::string output = rtmp_url;
        output = srs_string_replace(output, "[app]", "live");
        output = srs_string_replace(output, "[stream]", session_id);
        output = srs_path_build_timestamp(output);
        url = output;
    }
    // connect host.
    srs_utime_t cto = SRS_CONSTS_RTMP_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_PULSE;
    sdk = new SrsSimpleRtmpClient(url, cto, sto);
    
    srs_trace("gb28181: rtmp connect url=%s", url.c_str());
    if ((err = sdk->connect()) != srs_success) {
        close();
        return srs_error_wrap(err, "connect %s failed, cto=%dms, sto=%dms.", url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }
    
    // publish.
    if ((err = sdk->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        close();
        return srs_error_wrap(err, "publish %s failed", url.c_str());
    }
    
    return err;
}

void SrsGb28181Conn::close()
{
    srs_freep(sdk);
}

SrsGb28181Config::SrsGb28181Config(SrsConfDirective* c)
{
    // TODO: FIXME: support reload.
    output = _srs_config->get_stream_caster_output(c);
    rtp_port_min = _srs_config->get_stream_caster_rtp_port_min(c);
    rtp_port_max = _srs_config->get_stream_caster_rtp_port_max(c);
    rtp_idle_timeout = _srs_config->get_stream_caster_gb28181_rtp_ide_timeout(c);
    sip_ack_timeout = _srs_config->get_stream_caster_gb28181_ack_timeout(c);
    sip_keepalive_timeout = _srs_config->get_stream_caster_gb28181_keepalive_timeout(c);
    listen_port = _srs_config->get_stream_caster_listen(c);
    sip_host = _srs_config->get_stream_caster_gb28181_host(c);
    sip_realm = _srs_config->get_stream_caster_gb28181_realm(c);
    sip_serial = _srs_config->get_stream_caster_gb28181_serial(c);
    audio_enable = _srs_config->get_stream_caster_gb28181_audio_enable(c);
    print_sip_message = _srs_config->get_stream_caster_gb28181_print_sip_message(c);
    wait_keyframe = _srs_config->get_stream_caster_gb28181_wait_keyframe(c);
}

SrsGb28181Config::~SrsGb28181Config()
{
   
}

//gb28181 caster
SrsGb28181Caster::SrsGb28181Caster(SrsConfDirective* c)
{
    // TODO: FIXME: support reload.
    //output = _srs_config->get_stream_caster_output(c);
    //local_port_min = _srs_config->get_stream_caster_rtp_port_min(c);
    //local_port_max = _srs_config->get_stream_caster_rtp_port_max(c);
    sip = new SrsSipStack();
    manager = new SrsCoroutineManager();
    config = new SrsGb28181Config(c);
    lfd = NULL;
}

SrsGb28181Caster::~SrsGb28181Caster()
{
    used_ports.clear();

    srs_freep(manager);
    srs_freep(sip);
    srs_freep(config);
    
    destroy();
}

srs_error_t SrsGb28181Caster::initialize()
{
    srs_error_t err = srs_success;
    if ((err = manager->start()) != srs_success) {
        return srs_error_wrap(err, "start manager");
    }

    return err;
}

void SrsGb28181Caster::set_stfd(srs_netfd_t fd)
{
    lfd = fd;
}

SrsGb28181Config SrsGb28181Caster::GetGb28181Config()
{
    return *config;
}

srs_error_t SrsGb28181Caster::alloc_port(int* pport)
{
    srs_error_t err = srs_success;
    
    // use a pair of port.
    for (int i = config->rtp_port_min; i < config->rtp_port_max - 1; i += 2) {
        if (!used_ports[i]) {
            used_ports[i] = true;
            used_ports[i + 1] = true;
            *pport = i;
            break;
        }
    }
    srs_info("gb28181: alloc port=%d-%d", *pport, *pport + 1);
    
    return err;
}

void SrsGb28181Caster::free_port(int lpmin, int lpmax)
{
    for (int i = lpmin; i < lpmax; i++) {
        used_ports[i] = false;
    }
    srs_trace("gb28181: free rtp port=%d-%d", lpmin, lpmax);
}

srs_error_t SrsGb28181Caster::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
{
    char address_string[64];
    char port_string[16];
    if(getnameinfo(from, fromlen, 
                   (char*)&address_string, sizeof(address_string),
                   (char*)&port_string, sizeof(port_string),
                   NI_NUMERICHOST|NI_NUMERICSERV)) {
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "bad address");
    }
    std::string peer_ip = std::string(address_string);
    int peer_port = atoi(port_string);
    
    // append to buffer.
    //buffer->append(buf, nb_buf);
    srs_error_t err = on_udp_bytes(peer_ip, peer_port, buf, nb_buf, (sockaddr*)from, fromlen);
    if (err != srs_success) {
        return srs_error_wrap(err, "process udp");
    }
    return err;
}

srs_error_t SrsGb28181Caster::on_udp_bytes(string peer_ip, int peer_port, 
        char* buf, int nb_buf, sockaddr* from, const int fromlen)
{
    srs_error_t err = srs_success;
    
    if (config->print_sip_message)
    {
        srs_trace("gb28181: request peer_ip=%s, peer_port=%d nbbuf=%d", peer_ip.c_str(), peer_port, nb_buf);
        srs_trace("gb28181: request recv message=%s", buf);
    }
    
    if (nb_buf < 10) {
        return err;
    }

    SrsSipRequest* req = NULL;

    if ((err = sip->parse_request(&req, buf, nb_buf)) != srs_success) {
        return srs_error_wrap(err, "recv message");
    }

    if (config->print_sip_message)
    { 
        srs_trace("gb28181: %s method=%s, uri=%s, version=%s ", 
            req->get_cmdtype_str().c_str(), req->method.c_str(), req->uri.c_str(), req->version.c_str());
        srs_trace("gb28281: request client id=%s",  req->sip_auth_id.c_str());
    }
    
    req->peer_ip = peer_ip;
    req->peer_port = peer_port;

    SrsAutoFree(SrsSipRequest, req);

    if (req->is_register()) {
        std::vector<std::string> serial =  srs_string_split(srs_string_replace(req->uri,"sip:", ""), "@");
        if (serial.at(0) != config->sip_serial){
            srs_trace("gb28181: client:%s request serial and server serial inconformity(%s:%s)",
             req->sip_auth_id.c_str(), serial.at(0).c_str(), config->sip_serial.c_str());
            return  srs_success;
        }

        srs_trace("gb28181: request peer_ip=%s, peer_port=%d", peer_ip.c_str(), peer_port, nb_buf);
        srs_trace("gb28181: request %s method=%s, uri=%s, version=%s ", 
            req->get_cmdtype_str().c_str(), req->method.c_str(), req->uri.c_str(), req->version.c_str());
        srs_trace("gb28281: request client id=%s",  req->sip_auth_id.c_str());

        SrsGb28181Conn* conn = NULL;

        if ((err = fetch_or_create(req, &conn)) != srs_success) {
            srs_trace("gb28181: conn create faild:%s", req->uri.c_str());
            return  srs_error_wrap(err, "conn create faild");;
        }
        srs_assert(conn != NULL);
        
        // if (conn->register_status == Srs28181Unkonw)
        // {
        // }else{
        //     srs_trace("gb28181: %s client is register", req->sip_auth_id.c_str());
        // }

        send_status(req, from, fromlen);
        conn->register_status = Srs28181RegisterOk;
        conn->register_time = srs_get_system_time();
        conn->reg_expires = req->expires;
      
  
    }else if (req->is_message()) {
        SrsGb28181Conn* conn = fetch(req);
        if (!conn){
            srs_trace("gb28181: %s client not registered", req->sip_auth_id.c_str());
            return srs_success;
        }
        
        if (conn->register_status == Srs28181Unkonw) {
            send_bye(req, from, fromlen);
            srs_trace("gb28181: %s client not registered", req->sip_auth_id.c_str());
            return srs_success;
        }
         
        send_status(req, from, fromlen);
        conn->alive_status = Srs28181AliveOk;
        conn->alive_time = srs_get_system_time();

        if (conn->register_status == Srs28181RegisterOk &&
            conn->alive_status == Srs28181AliveOk &&
            conn->invite_status != Srs28181InviteOk)
        {
            int lpm = 0;
            if (alloc_port(&lpm) != srs_success) {
                return srs_error_wrap(err, "alloc port");
            }

            if (lpm){
                send_invite(req, from, fromlen, lpm);
                conn->rtmp_url = config->output;
                conn->start_rtp_listen(lpm);
                conn->invite_status == Srs28181Trying;
                conn->invite_time = srs_get_system_time();
            }

        }

    }else if (req->is_invite()) {
        SrsGb28181Conn* conn = fetch(req);

        srs_trace("gb28181: request peer_ip=%s, peer_port=%d", peer_ip.c_str(), peer_port, nb_buf);
        srs_trace("gb28181: request %s method=%s, uri=%s, version=%s ", 
            req->get_cmdtype_str().c_str(), req->method.c_str(), req->uri.c_str(), req->version.c_str());
        srs_trace("gb28281: request client id=%s",  req->sip_auth_id.c_str());
       
        if (!conn){
            send_bye(req, from, fromlen);
            srs_trace("gb28181: %s client not registered", req->sip_auth_id.c_str());
            return srs_success;
        }

        if (conn->register_status == Srs28181Unkonw ||
            conn->alive_status == Srs28181Unkonw) {
            send_bye(req, from, fromlen);
            srs_trace("gb28181: %s client not registered or not alive", req->sip_auth_id.c_str());
            return srs_success;
        }
        
        if (req->cmdtype == SrsSipCmdRespone && req->status == "200") {
            srs_trace("gb28181: INVITE response %s client status=%s", req->sip_auth_id.c_str(), req->status.c_str());
            send_ack(req, from, fromlen);
            conn->invite_status = Srs28181InviteOk;
            conn->invite_time = srs_get_system_time();
        }
    }else if (req->is_bye()) {
        srs_trace("gb28181: request peer_ip=%s, peer_port=%d", peer_ip.c_str(), peer_port, nb_buf);
        srs_trace("gb28181: request %s method=%s, uri=%s, version=%s ", 
            req->get_cmdtype_str().c_str(), req->method.c_str(), req->uri.c_str(), req->version.c_str());
        srs_trace("gb28281: request client id=%s",  req->sip_auth_id.c_str());

        SrsGb28181Conn* conn = fetch(req);
        send_status(req, from, fromlen);

        if (!conn){
            srs_trace("gb28181: %s client not registered", req->sip_auth_id.c_str());
            return srs_success;
        }
       
        conn->stop_rtp_listen();
        conn->invite_status = Srs28181Bye;
        conn->invite_time = 0;
   
    }else{
        srs_trace("gb28181: ingor request method=%s", req->method.c_str());
    }
  
    return err;
}

srs_error_t SrsGb28181Caster::send_message(sockaddr* from, int fromlen, std::stringstream& ss)
{
    srs_error_t err = srs_success;

    std::string str = ss.str();
    if (config->print_sip_message)
        srs_trace("gb28181: send_message:%s", str.c_str());
    srs_assert(!str.empty());

    int ret = srs_sendto(lfd, (char*)str.c_str(), (int)str.length(), from, fromlen, SRS_UTIME_NO_TIMEOUT);
    if (ret <= 0){
        return srs_error_wrap(err, "gb28181: send_message falid");
    }
    
    return err;
}

srs_error_t SrsGb28181Caster::send_bye(SrsSipRequest *req, sockaddr *f, int l)
{
    srs_error_t err = srs_success;
    srs_assert(req);

    std::stringstream ss;
    
    req->host =  config->sip_host;
    req->host_port = config->listen_port;
    req->realm = config->sip_realm;
    req->serial = config->sip_serial;

    sip->req_bye(ss, req);
    send_message(f, l, ss);

    return err;

}
srs_error_t SrsGb28181Caster::send_ack(SrsSipRequest *req, sockaddr *f, int l)
{
    srs_error_t err = srs_success;
    srs_assert(req);

    std::stringstream ss;
    
    req->host =  config->sip_host;
    req->host_port = config->listen_port;
    req->realm = config->sip_realm;
    req->serial = config->sip_serial;

    sip->resp_ack(ss, req);
    send_message(f, l, ss);

    return err;
}

srs_error_t SrsGb28181Caster::send_invite(SrsSipRequest *req,  sockaddr *f, int l, int port)
{
    srs_error_t err = srs_success;
    srs_assert(req);

    std::stringstream ss;
    
    req->host =  config->sip_host;
    req->host_port = config->listen_port;
    req->realm = config->sip_realm;
    req->serial = config->sip_serial;

    sip->req_invite(ss, req, port);
    send_message(f, l, ss);

    return err;

}

srs_error_t SrsGb28181Caster::send_status(SrsSipRequest *req,  sockaddr *f, int l)
{
    srs_error_t err = srs_success;
    srs_assert(req);

    std::stringstream ss;
    
    req->host =  config->sip_host;
    req->host_port = config->listen_port;
    req->realm = config->sip_realm;
    req->serial = config->sip_serial;

    sip->resp_status(ss, req);
    send_message(f, l, ss);

    return err;

}


srs_error_t SrsGb28181Caster::fetch_or_create(SrsSipRequest* r,  SrsGb28181Conn** gb28181)
{
    srs_error_t err = srs_success;

    SrsGb28181Conn* conn = NULL;
    if ((conn = fetch(r)) != NULL) {
        *gb28181 = conn;
        return err;
    }
    
    string key = r->sip_auth_id;
    conn = new SrsGb28181Conn(this, key);
    conn->set_request_info(r);
    if ((err = conn->serve()) != srs_success) {
        return srs_error_wrap(err, "sipconn serve %s", key.c_str());
    }
    clients[key] = conn;
    *gb28181 = conn;
    
    return err;
}

SrsGb28181Conn* SrsGb28181Caster::fetch(const SrsSipRequest* r)
{
    SrsGb28181Conn* conn = NULL;
    
    string key = r->sip_auth_id;
    if (clients.find(key) == clients.end()) {
        return NULL;
    }
    
    conn = clients[key];
    return conn;
}


void SrsGb28181Caster::destroy()
{
    std::map<std::string, SrsGb28181Conn*>::iterator it;
    for (it = clients.begin(); it != clients.end(); ++it) {
        SrsGb28181Conn* conn = it->second;
        manager->remove(conn);
    }
    clients.clear();
}

void SrsGb28181Caster::remove(SrsGb28181Conn* conn)
{
    std::string id = conn->get_session_id();
    map<std::string, SrsGb28181Conn*>::iterator key = clients.find(id);
    if (key != clients.end()) {
        clients.erase(key);
    }
    manager->remove(conn);
}
