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

#include <srs_app_gb28181.hpp>
#include <sys/socket.h>
#include <netdb.h>

using namespace std;

#include <srs_app_rtsp.hpp>
#include <srs_protocol_json.hpp>
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

//#define W_PS_FILE
//#define W_VIDEO_FILE
//#define W_AUDIO_FILE
//#define W_UNKONW_FILE

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
        // ps stream playload atleast 1bytes content.
        if (!stream->require(1)) {
            return srs_error_new(ERROR_RTP_TYPE96_CORRUPT, "requires 1 only %d bytes", stream->left());
        }
        // append left bytes to payload.
        payload->append(stream->data() + stream->pos() , stream->size()-stream->pos());
    } 
    return err;
}

//SrsPsRtpListener
SrsPsRtpListener::SrsPsRtpListener(SrsGb28181Config* c, int p, std::string s)
{
    rtp_processor = new SrsGb28181PsRtpProcessor(c, s);
    _port = p;
    // TODO: support listen at <[ip:]port>
    listener = new SrsUdpListener(this, srs_any_address_for_listener(), p);
}

SrsPsRtpListener::~SrsPsRtpListener()
{
    srs_freep(listener);
    srs_freep(rtp_processor);
}

int SrsPsRtpListener::port()
{
    return _port;
}

srs_error_t SrsPsRtpListener::listen()
{
    return listener->listen();
}

srs_error_t SrsPsRtpListener::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf){
    srs_error_t err = srs_success;
    if (rtp_processor){
        err = rtp_processor->on_udp_packet(from, fromlen, buf, nb_buf);
    }
    return err;
}

//SrsGb28181RtpMuxService 
SrsGb28181RtpMuxService::SrsGb28181RtpMuxService(SrsConfDirective* c)
{
    config = new SrsGb28181Config(c);
    rtp_processor = new SrsGb28181PsRtpProcessor(config,"");
}

SrsGb28181RtpMuxService::~SrsGb28181RtpMuxService()
{
    srs_freep(config);
    srs_freep(rtp_processor);
}

srs_error_t SrsGb28181RtpMuxService::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf){
    srs_error_t err = srs_success;
    if (rtp_processor){
        err = rtp_processor->on_udp_packet(from, fromlen, buf, nb_buf);
    }
    return err;
}

//SrsGb28181PsRtpProcessor
SrsGb28181PsRtpProcessor::SrsGb28181PsRtpProcessor(SrsGb28181Config* c, std::string id)
{
    config = c;
    pprint = SrsPithyPrint::create_caster();
    channel_id = id;
}

SrsGb28181PsRtpProcessor::~SrsGb28181PsRtpProcessor()
{
    dispose();
    srs_freep(pprint);
}

void SrsGb28181PsRtpProcessor::dispose()
{
    map<std::string, SrsPsRtpPacket*>::iterator it2;
    for (it2 = cache_ps_rtp_packet.begin(); it2 != cache_ps_rtp_packet.end(); ++it2) {
        srs_freep(it2->second);
    }
    cache_ps_rtp_packet.clear();

    clear_pre_packet();

    return;
}

void SrsGb28181PsRtpProcessor::clear_pre_packet()
{
    map<std::string, SrsPsRtpPacket*>::iterator it;
    for (it = pre_packet.begin(); it != pre_packet.end(); ++it) {
        srs_freep(it->second);
    }
    pre_packet.clear();
}

srs_error_t SrsGb28181PsRtpProcessor::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
{
    srs_error_t err = srs_success;
    bool completed = false;
    
    pprint->elapse();
    
    char address_string[64];
    char port_string[16];
    if (getnameinfo(from, fromlen, 
                (char*)&address_string, sizeof(address_string),
                (char*)&port_string, sizeof(port_string),
                NI_NUMERICHOST|NI_NUMERICSERV)){
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "bad address");
    }
    
    int peer_port = atoi(port_string);

    if (true) {
        SrsBuffer stream(buf, nb_buf);
        SrsPsRtpPacket pkt;
        
        if ((err = pkt.decode(&stream)) != srs_success) {
            return srs_error_wrap(err, "ps rtp decode error");
        }

        //TODO: fixme: the same device uses the same SSRC to send with different local ports
        
        std::stringstream ss;
        ss << pkt.ssrc << ":" << pkt.timestamp << ":" << port_string;
        std::string pkt_key = ss.str();
      
        std::stringstream ss2;
        ss2 << pkt.ssrc << ":" << port_string;
        std::string pre_pkt_key = ss2.str();

        if (pre_packet.find(pre_pkt_key) == pre_packet.end()){
            pre_packet[pre_pkt_key] = new SrsPsRtpPacket();
            pre_packet[pre_pkt_key]->copy(&pkt);
        }
        //cache pkt by ssrc and timestamp
        if (cache_ps_rtp_packet.find(pkt_key) == cache_ps_rtp_packet.end()) {
           cache_ps_rtp_packet[pkt_key] =  new SrsPsRtpPacket();
        }
        
        //get previous timestamp by ssrc
        uint32_t pre_timestamp = pre_packet[pre_pkt_key]->timestamp;
        uint32_t pre_sequence_number = pre_packet[pre_pkt_key]->sequence_number;

        //TODO:  check sequence number out of order
        //it may be out of order, or multiple streaming ssrc are the same
        if (pre_sequence_number + 1 != pkt.sequence_number && 
            pre_sequence_number != pkt.sequence_number){
            srs_warn("gb28181: ps sequence_number out of order, ssrc=%#x, pre=%u, cur=%u, peer(%s, %s)",
              pkt.ssrc, pre_sequence_number, pkt.sequence_number, address_string, port_string);
            //return err;
        }

        //copy header to cache
        cache_ps_rtp_packet[pkt_key]->copy(&pkt);
        //accumulate one frame of data, to payload cache
        cache_ps_rtp_packet[pkt_key]->payload->append(pkt.payload);

        //detect whether it is a completed frame
        if (pkt.marker) {// rtp maker is true, is a completed frame
            completed = true;
        }else if (pre_timestamp != pkt.timestamp){ 
            //current timestamp is different from previous timestamp
            //previous timestamp, is a completed frame
            std::stringstream ss;
            ss << pkt.ssrc << ":" << pre_timestamp << ":" << port_string;
            pkt_key = ss.str();
            if (cache_ps_rtp_packet.find(pkt_key) != cache_ps_rtp_packet.end()) {
                completed = true;
            }
        }

        if (pprint->can_print()) {
            srs_trace("<- " SRS_CONSTS_LOG_GB28181_CASTER " gb28181: client_id %s, peer(%s, %d) ps rtp packet %dB, age=%d, vt=%d/%u, sts=%u/%u/%#x, paylod=%dB",
                        channel_id.c_str(),  address_string, peer_port, nb_buf, pprint->age(), pkt.version, 
                        pkt.payload_type, pkt.sequence_number, pkt.timestamp, pkt.ssrc,
                        pkt.payload->length()
                        );
        }

        //current packet becomes previous packet
        srs_freep(pre_packet[pre_pkt_key]);
        pre_packet[pre_pkt_key] = new SrsPsRtpPacket();
        pre_packet[pre_pkt_key]->copy(&pkt);;

        if (!completed){
            return err;
        }
      
        //process completed frame data
        //clear processed one ps frame
        //on completed frame data rtp packet in muxer enqueue
        map<std::string, SrsPsRtpPacket*>::iterator key = cache_ps_rtp_packet.find(pkt_key);
        if(key != cache_ps_rtp_packet.end())
        {
            SrsGb28181RtmpMuxer* muxer = NULL;
            
            //First, search according to the channel_id. Otherwise, search according to the SSRC. 
            //Some channel_id are created by RTP pool, which are different ports. 
            //No channel_id are created by multiplexing ports, which are the same port
            if (!channel_id.empty()){
                muxer = _srs_gb28181->fetch_rtmpmuxer(channel_id);
            }else {
                muxer = _srs_gb28181->fetch_rtmpmuxer_by_ssrc(pkt.ssrc);
            }

            //auto crate channel
            if (!muxer && config->auto_create_channel){
                //auto create channel generated id
                std::stringstream ss, ss1;
                ss << "chid" << pkt.ssrc;
                std::string tmp_id = ss.str();

                SrsGb28181StreamChannel channel;
                channel.set_channel_id(tmp_id);
                channel.set_port_mode(RTP_PORT_MODE_FIXED);
                channel.set_ssrc(pkt.ssrc);
              
                srs_error_t err2 = srs_success;
                if ((err2 = _srs_gb28181->create_stream_channel(&channel)) != srs_success){
                    srs_warn("gb28181: RtpProcessor create stream channel error %s", srs_error_desc(err2).c_str());
                    srs_error_reset(err2);
                };

                muxer = _srs_gb28181->fetch_rtmpmuxer(tmp_id);
            }
          
            if (muxer){
                //TODO: fixme: the same device uses the same SSRC to send with different local ports
                //record the first peer port
                muxer->set_channel_peer_port(peer_port);
                muxer->set_channel_peer_ip(address_string);
                //not the first peer port's non processing
                if (muxer->channel_peer_port() != peer_port){
                    srs_warn("<- " SRS_CONSTS_LOG_GB28181_CASTER " gb28181: client_id %s, ssrc=%#x, first peer_port=%d cur peer_port=%d",
                        muxer->get_channel_id().c_str(), pkt.ssrc, muxer->channel_peer_port(), peer_port);
                    srs_freep(key->second);
                }else {
                    //put it in queue, wait for consumer to process, and then free
                    muxer->ps_packet_enqueue(key->second);
                }
            }else{
                //no consumer process it, discarded
                srs_freep(key->second);
            }
            cache_ps_rtp_packet.erase(pkt_key);
        }
    }
    return err;
}

//ISrsPsStreamHander ps stream raw video/audio hander interface
ISrsPsStreamHander::ISrsPsStreamHander()
{
}

ISrsPsStreamHander::~ISrsPsStreamHander()
{
}

//SrsPsStreamDemixer ps stream parse to h264/aac
SrsPsStreamDemixer::SrsPsStreamDemixer(ISrsPsStreamHander *h, std::string id, bool a, bool k)
{
    hander = h;
    audio_enable = a;
    wait_first_keyframe = k;
    channel_id = id;
    first_keyframe_flag = false;
}

SrsPsStreamDemixer::~SrsPsStreamDemixer()
{
}

bool SrsPsStreamDemixer::can_send_ps_av_packet(){
    if (!wait_first_keyframe)
        return true;
    
    if (first_keyframe_flag)
       return true;

    return false;
}

int64_t  SrsPsStreamDemixer::parse_ps_timestamp(const uint8_t* p)
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


srs_error_t SrsPsStreamDemixer::on_ps_stream(char* ps_data, int ps_size, uint32_t timestamp, uint32_t ssrc)
{
    srs_error_t err = srs_success;
    int complete_len = 0;
    int incomplete_len = ps_size;
    char *next_ps_pack = ps_data;

    SrsSimpleStream video_stream;
    SrsSimpleStream audio_stream;
    uint64_t audio_pts = 0;
    uint64_t video_pts = 0;
    int pse_index = 0;

#ifdef W_PS_FILE           
        if (!ps_fw.is_open()) {
                std::string filename = "test_ps_" + channel_id + ".mpg";
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
            //pse video stream
            SrsPsePacket* pse_pack = (SrsPsePacket*)next_ps_pack;

            unsigned char pts_dts_flags = (pse_pack->info[0] & 0xF0) >> 6;
            //in a frame of data, pts is obtained from the first PSE packet
            if (pse_index == 0 && pts_dts_flags > 0) {
				video_pts = parse_ps_timestamp((unsigned char*)next_ps_pack + 9);
                srs_info("gb28181: ps stream video ts=%u pkt_ts=%u", video_pts, timestamp);
			}
            pse_index +=1;

            int packlength = htons(pse_pack->length);
            int payloadlen = packlength - 2 - 1 - pse_pack->stuffing_length;
         
            next_ps_pack = next_ps_pack + 9 + pse_pack->stuffing_length;
            complete_len = complete_len + 9 + pse_pack->stuffing_length;

            video_stream.append(next_ps_pack, payloadlen);

#ifdef W_VIDEO_FILE            
            if (!video_fw.is_open()) {
                 std::string filename = "test_video_" + channel_id + ".h264";
                 video_fw.open(filename.c_str());
            }
            video_fw.write(next_ps_pack,  payloadlen, NULL);          
#endif

            next_ps_pack = next_ps_pack + payloadlen;
            complete_len = complete_len + payloadlen;
            incomplete_len = ps_size - complete_len;
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
                srs_info("gb28181: ps stream video ts=%u pkt_ts=%u", audio_pts, timestamp);
         	}

			int packlength = htons(pse_pack->length);
			int payload_len = packlength - 2 - 1 - pse_pack->stuffing_length;
            next_ps_pack = next_ps_pack + 9 + pse_pack->stuffing_length;

            audio_stream.append(next_ps_pack, payload_len);

#ifdef W_AUDIO_FILE            
            if (!audio_fw.is_open()) {
                 std::string filename = "test_audio_" + channel_id + ".aac";
                 audio_fw.open(filename.c_str());
            }
            audio_fw.write(next_ps_pack,  payload_len, NULL);          
#endif
            
			next_ps_pack = next_ps_pack + payload_len;
            complete_len = complete_len + (payload_len + 9 + pse_pack->stuffing_length);
            incomplete_len = ps_size - complete_len;

            if (hander && audio_enable && audio_stream.length() && can_send_ps_av_packet()) {
                if ((err = hander->on_rtp_audio(&audio_stream, audio_pts)) != srs_success) {
                    return srs_error_wrap(err, "process ps audio packet");
                }
            }
		}
        else
        {

#ifdef W_UNKONW_FILE            
            if (!unknow_fw.is_open()) {
                 std::string filename = "test_unknow_" + channel_id + ".mpg";
                 unknow_fw.open(filename.c_str());
            }
            unknow_fw.write(next_ps_pack,  incomplete_len, NULL);          
#endif      
            //TODO: fixme unkonw ps data parse
            if (next_ps_pack
            && next_ps_pack[0] == (char)0x00
			&& next_ps_pack[1] == (char)0x00
			&& next_ps_pack[2] == (char)0x00
			&& next_ps_pack[3] == (char)0x01){
                //dahua's PS header may lose packets. It is sent by an RTP packet of Dahua's PS header
                //dahua rtp send format:
                //ts=1000 seq=1 mark=false payload= ps header
                //ts=1000 seq=2 mark=false payload= video
                //ts=1000 seq=3 mark=true payload= video
                //ts=1000 seq=4 mark=true payload= audio
                incomplete_len = ps_size - complete_len; 
                complete_len = complete_len + incomplete_len;
                
            }

            first_keyframe_flag = false;
            srs_trace("gb28181: client_id %s, unkonw ps data (%#x/%u) %02x %02x %02x %02x\n", 
                channel_id.c_str(), ssrc, timestamp,  
                next_ps_pack[0], next_ps_pack[1], next_ps_pack[2], next_ps_pack[3]);
            break;
        }
    }

    if (complete_len != ps_size){
         srs_trace("gb28181: client_id %s decode ps packet error (%#x/%u)! ps_size=%d  complete=%d \n", 
                     channel_id.c_str(), ssrc, timestamp, ps_size, complete_len);
    }else if (hander && video_stream.length() && can_send_ps_av_packet()) {
         if ((err = hander->on_rtp_video(&video_stream, video_pts)) != srs_success) {
             return srs_error_wrap(err, "process ps video packet");
        }
    }
  
    return err;
}

static std::string get_host_candidate_ips(SrsConfDirective* c)
{
    string candidate = _srs_config->get_stream_caster_gb28181_host(c);
    if (candidate == "*" || candidate == "0.0.0.0") {
        std::vector<std::string> ips = srs_get_local_ips();
        int index = _srs_config->get_stats_network();
        return ips.at(index);
    } else {
        return candidate;
    }
}

//Gb28181 Config
SrsGb28181Config::SrsGb28181Config(SrsConfDirective* c)
{
    // TODO: FIXME: support reload.
    host = get_host_candidate_ips(c);
    output = _srs_config->get_stream_caster_output(c);
    rtp_mux_port = _srs_config->get_stream_caster_listen(c);
    rtp_port_min = _srs_config->get_stream_caster_rtp_port_min(c);
    rtp_port_max = _srs_config->get_stream_caster_rtp_port_max(c);
    rtp_idle_timeout = _srs_config->get_stream_caster_gb28181_rtp_idle_timeout(c);

    wait_keyframe = _srs_config->get_stream_caster_gb28181_wait_keyframe(c);
    audio_enable = _srs_config->get_stream_caster_gb28181_audio_enable(c);
    auto_create_channel = _srs_config->get_stream_caster_gb28181_auto_create_channel(c);

    //sip config
    sip_enable = _srs_config->get_stream_caster_gb28181_sip_enable(c);
    sip_port = _srs_config->get_stream_caster_gb28181_sip_listen(c);
    sip_realm = _srs_config->get_stream_caster_gb28181_realm(c);
    sip_serial = _srs_config->get_stream_caster_gb28181_serial(c);
    sip_auto_play = _srs_config->get_stream_caster_gb28181_sip_auto_play(c);
    sip_ack_timeout = _srs_config->get_stream_caster_gb28181_ack_timeout(c);
    sip_keepalive_timeout = _srs_config->get_stream_caster_gb28181_keepalive_timeout(c);
    sip_invite_port_fixed = _srs_config->get_stream_caster_gb28181_sip_invite_port_fixed(c);
    sip_query_catalog_interval = _srs_config->get_stream_caster_gb28181_sip_query_catalog_interval(c);
}

SrsGb28181Config::~SrsGb28181Config()
{
   
}

//SrsGb28181RtmpMuxer gb28181 rtmp muxer, process ps stream to rtmp
SrsGb28181RtmpMuxer::SrsGb28181RtmpMuxer(SrsGb28181Manger* c, std::string id, bool a, bool k)
{
    channel_id = id;
    gb28181_manger = c;
    channel = new SrsGb28181StreamChannel();

    pprint = SrsPithyPrint::create_caster();
    trd = new SrsSTCoroutine("gb28181rtmpmuxer", this);
    
    sdk = NULL;
    vjitter = new SrsRtspJitter();
    ajitter = new SrsRtspJitter();
    
    avc = new SrsRawH264Stream();
    aac = new SrsRawAacStream();

    ps_demixer = new SrsPsStreamDemixer(this, id, a, k);
    wait_ps_queue = srs_cond_new();

    stream_idle_timeout = -1;
    recv_rtp_stream_time = 0;
    send_rtmp_stream_time = 0;

    _rtmp_url = "";

    h264_sps = "";
    h264_pps = "";
    aac_specific_config = "";

}

SrsGb28181RtmpMuxer::~SrsGb28181RtmpMuxer()
{
    close();
    destroy();
    srs_cond_destroy(wait_ps_queue);

    srs_freep(channel);
    srs_freep(ps_demixer);
    srs_freep(trd);
    srs_freep(sdk);
    srs_freep(vjitter);
    srs_freep(ajitter);
    srs_freep(pprint);
}

srs_error_t SrsGb28181RtmpMuxer::serve()
{
    srs_error_t err = srs_success;
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "gb28181rtmpmuxer");
    }
    
    return err;
}

std::string SrsGb28181RtmpMuxer::remote_ip()
{
    return "";
}

std::string SrsGb28181RtmpMuxer::get_channel_id()
{
    return channel_id;
}

void SrsGb28181RtmpMuxer::copy_channel(SrsGb28181StreamChannel *s)
{
    channel->copy(s);
}

SrsGb28181StreamChannel SrsGb28181RtmpMuxer::get_channel()
{
   return *channel;
}

void SrsGb28181RtmpMuxer::set_channel_peer_ip(std::string ip)
{
    if (channel->get_rtp_peer_ip().empty()){
       channel->set_rtp_peer_ip(ip);
    }
}

void SrsGb28181RtmpMuxer::set_channel_peer_port(int port)
{
    if (channel->get_rtp_peer_port() == 0){
        channel->set_recv_time_str(srs_sip_get_utc_date());
        channel->set_recv_time(srs_get_system_time());
        channel->set_rtp_peer_port(port);
    }
}

int SrsGb28181RtmpMuxer::channel_peer_port()
{
    return channel->get_rtp_peer_port();
}

std::string SrsGb28181RtmpMuxer::channel_peer_ip()
{
    return channel->get_rtp_peer_ip();
}

void SrsGb28181RtmpMuxer::set_rtmp_url(std::string url)
{
    _rtmp_url = url;
}
std::string SrsGb28181RtmpMuxer::rtmp_url()
{
    return _rtmp_url;
}

srs_utime_t SrsGb28181RtmpMuxer::get_recv_stream_time()
{
    return recv_rtp_stream_time;
}


void SrsGb28181RtmpMuxer::destroy()
{
    while(!ps_queue.empty()){
        SrsPsRtpPacket* pkt =  ps_queue.front();
        ps_queue.pop();
        //must be free pkt
        srs_freep(pkt);
    }
}

srs_error_t SrsGb28181RtmpMuxer::do_cycle()
{
    srs_error_t err = srs_success;
    recv_rtp_stream_time =  srs_get_system_time();
    send_rtmp_stream_time = srs_get_system_time();

    //consume ps stream, and check status
    while (true) {

        pprint->elapse();

        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "gb28181 rtmp muxer cycle");
        }

        //demix ps to h264/aac, to rtmp
        while(!ps_queue.empty()){
            SrsPsRtpPacket* pkt =  ps_queue.front();
            if (pkt){ 
                if ((err = ps_demixer->on_ps_stream(pkt->payload->bytes(),
                    pkt->payload->length(), pkt->timestamp, pkt->ssrc)) != srs_success){
                    srs_warn("gb28181: demix ps stream error:%s",  srs_error_desc(err).c_str());
                    srs_freep(err);
                };
            }
            ps_queue.pop();
            //must be free pkt
            srs_freep(pkt);
        }

        if (pprint->can_print()) {
            srs_trace("gb28181: client id=%s,  ssrc=%#x, peer(%s, %d), rtmp muxer is alive",
                channel_id.c_str(),  channel->get_ssrc(), 
                channel->get_rtp_peer_ip().c_str(),
                channel->get_rtp_peer_port());
        }
        
        srs_utime_t now = srs_get_system_time();
        srs_utime_t duration = now - recv_rtp_stream_time;

        //if no RTP data is received within 2 seconds, 
        //the peer-port and peer-ip will be cleared and 
        //other port data will be received again
        if (duration > (2 * SRS_UTIME_SECONDS) && channel->get_rtp_peer_port() != 0){
            srs_warn("gb28181: client id=%s ssrc=%#x, peer(%s, %d), no rtp data %d in seconds, clean it, wait other port!", 
                channel_id.c_str(), channel->get_ssrc(), channel->get_rtp_peer_ip().c_str(),
                channel->get_rtp_peer_port(), duration/SRS_UTIME_SECONDS);
            channel->set_rtp_peer_port(0);
            channel->set_rtp_peer_ip("");
        }

        SrsGb28181Config config = gb28181_manger->get_gb28181_config();
        if (duration > config.rtp_idle_timeout){
            srs_trace("gb28181: client id=%s, stream idle timeout, stop!!!", channel_id.c_str());
            break;
        }

        //RTMP connection is about to timeout without receiving any data., 
        //waiting for the next time there is data automatically connected
        //it is related to the following two parameter settings of the rtmp server
        //the publish 1st packet timeout in srs_utime_t
        //publish_1stpkt_timeout  default 20000ms
        //the publish normal packet timeout in srs_utime_t
        //publish_normal_timeout  default 5000ms
        duration = now - send_rtmp_stream_time;
        bool will_timeout = duration > (5 * SRS_UTIME_SECONDS);
        if (will_timeout && sdk){
            srs_warn("gb28181: client id=%s RTMP connection is about to time out without receiving any data",
                channel_id.c_str());
            rtmp_close();
        }
      
        if (ps_queue.empty()){
           srs_cond_timedwait(wait_ps_queue, 200 * SRS_UTIME_MILLISECONDS);
        }else {
           srs_cond_timedwait(wait_ps_queue, 10 * SRS_UTIME_MILLISECONDS);
        }
    }
    
    return err;
}


void SrsGb28181RtmpMuxer::stop()
{
    if (trd){
        trd->interrupt();
    }
    //stop rtmp publish
    close();
}

void SrsGb28181RtmpMuxer::ps_packet_enqueue(SrsPsRtpPacket *pkt)
{
    srs_assert(pkt);
   
    recv_rtp_stream_time = srs_get_system_time();

    //prevent consumers from being unable to process data 
    //and accumulating in the queue
    uint32_t size = ps_queue.size();
    if (size > 100){
        srs_warn("gb28181: rtmpmuxer too much queue data, need to clear!!!");
        while(ps_queue.empty()) {
            SrsPsRtpPacket* pkt =  ps_queue.front();
            ps_queue.pop();
            srs_freep(pkt);
        }
    }
   
    ps_queue.push(pkt);
    srs_cond_signal(wait_ps_queue);
}

srs_error_t SrsGb28181RtmpMuxer::cycle()
{
    // serve the rtmp muxer.
    srs_error_t err = do_cycle();
    
    gb28181_manger->stop_rtp_listen(channel_id);

    gb28181_manger->remove(this);
    srs_trace("gb28181: client id=%s rtmp muxer is remove", channel_id.c_str());
    
    if (err == srs_success) {
        srs_trace("client finished.");
    } else if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. code=%d", srs_error_code(err));
        srs_freep(err);
    }
   
    return err;
}

srs_error_t SrsGb28181RtmpMuxer::on_rtp_video(SrsSimpleStream *stream, int64_t fpts)
{
    srs_error_t err = srs_success;
   
    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        //after the connection fails, need to clear flag 
        //and send the av header again next time
        h264_sps = "";
        h264_pps = "";
        aac_specific_config = "";
        return srs_error_wrap(err, "connect");
    }
    
    if ((err = vjitter->correct(fpts)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }
    
    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(fpts / 90);
    uint32_t pts = (uint32_t)(fpts / 90);
    srs_info("gb28181rtmpmuxer: on_rtp_video dts=%u", dts);
    


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
            h264_pps = pps;
            
            if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write sps/pps");
            }
            continue;
        }
        
        // ibp frame.
        srs_info("gb28181: demux avc ibp frame size=%d, dts=%d", frame_size, dts);
        if ((err = write_h264_ipb_frame(frame, frame_size, dts, pts)) != srs_success) {
            return srs_error_wrap(err, "write frame");
        }
    }
    
    return err;
}

srs_error_t SrsGb28181RtmpMuxer::on_rtp_audio(SrsSimpleStream* stream, int64_t fdts)
{
    srs_error_t err = srs_success;
    
    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        //after the connection fails, need to clear flag 
        //and send the av header again next time
        h264_sps = "";
        h264_pps = "";
        aac_specific_config = "";
        return srs_error_wrap(err, "connect");
    }
    
    if ((err = ajitter->correct(fdts)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }
    
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

srs_error_t SrsGb28181RtmpMuxer::write_h264_sps_pps(uint32_t dts, uint32_t pts)
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

srs_error_t SrsGb28181RtmpMuxer::write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts)
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

srs_error_t SrsGb28181RtmpMuxer::write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts)
{
    srs_error_t err = srs_success;
    
    char* data = NULL;
    int size = 0;
    if ((err = aac->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success) {
        return srs_error_wrap(err, "mux aac to flv");
    }
    
    return rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
}

srs_error_t SrsGb28181RtmpMuxer::rtmp_write_packet(char type, uint32_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;
    
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    
    SrsSharedPtrMessage* msg = NULL;
    
    send_rtmp_stream_time = srs_get_system_time();

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

srs_error_t SrsGb28181RtmpMuxer::connect()
{
    srs_error_t err = srs_success;
    
    // Ignore when connected.
    if (sdk) {
        return err;
    }
    
    // generate rtmp url to connect to.
    std::string url = _rtmp_url;
   
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

void SrsGb28181RtmpMuxer::close()
{
    srs_freep(sdk);
  
    // cleared and sequence header will be sent again next time.
    // RTMP close may stop through API(rtmp_close)
    h264_sps = "";
    h264_pps = "";
    aac_specific_config = "";
}

void SrsGb28181RtmpMuxer::rtmp_close(){
    close();
}

SrsGb28181StreamChannel::SrsGb28181StreamChannel(){
    channel_id = "";
    port_mode = "";
    app = "";
    stream = "";
    ip = "";
    rtp_port = 0;
    rtmp_port = 0;
    ssrc = 0;
    rtp_peer_port = 0;
    rtp_peer_ip = "";
    rtmp_url = "";
    recv_time = 0;
    recv_time_str = "";
}

SrsGb28181StreamChannel::~SrsGb28181StreamChannel()
{
    
}

void SrsGb28181StreamChannel::copy(const SrsGb28181StreamChannel *s){
    channel_id = s->get_channel_id();
    port_mode = s->get_port_mode();
    app = s->get_app();
    stream = s->get_stream();

    ip = s->get_ip();
    rtp_port = s->get_rtp_port();
    rtmp_port = s->get_rtmp_port();
    ssrc = s->get_ssrc();

    rtp_peer_ip = s->get_rtp_peer_ip();
    rtp_peer_port = s->get_rtp_peer_port();

    rtmp_url = s->get_rtmp_url();
    
    recv_time_str = s->get_recv_time_str();
    recv_time = s->get_recv_time();

}

void SrsGb28181StreamChannel::dumps(SrsJsonObject* obj)
{
    obj->set("id", SrsJsonAny::str(channel_id.c_str()));
    obj->set("ip", SrsJsonAny::str(ip.c_str()));
    obj->set("rtmp_port", SrsJsonAny::integer(rtmp_port));
    obj->set("app", SrsJsonAny::str(app.c_str()));
    obj->set("stream", SrsJsonAny::str(stream.c_str()));
    obj->set("rtmp_url", SrsJsonAny::str(rtmp_url.c_str()));
   
    obj->set("ssrc", SrsJsonAny::integer(ssrc));
    obj->set("rtp_port", SrsJsonAny::integer(rtp_port));
    obj->set("port_mode", SrsJsonAny::str(port_mode.c_str()));
    obj->set("rtp_peer_port", SrsJsonAny::integer(rtp_peer_port));
    obj->set("rtp_peer_ip", SrsJsonAny::str(rtp_peer_ip.c_str()));
    obj->set("recv_time", SrsJsonAny::integer(recv_time/SRS_UTIME_SECONDS));
    obj->set("recv_time_str", SrsJsonAny::str(recv_time_str.c_str()));
    
}


//Global Singleton instance, init in SrsServer
SrsGb28181Manger* _srs_gb28181 = NULL;

//SrsGb28181Manger
SrsGb28181Manger::SrsGb28181Manger(SrsConfDirective* c)
{
    // TODO: FIXME: support reload.
    config = new SrsGb28181Config(c);
    manager = new SrsCoroutineManager();
}

SrsGb28181Manger::~SrsGb28181Manger()
{
    used_ports.clear();

    srs_freep(manager);
    srs_freep(config);
    
    destroy();
}

srs_error_t SrsGb28181Manger::initialize()
{
    srs_error_t err = srs_success;
    if ((err = manager->start()) != srs_success) {
        return srs_error_wrap(err, "start manager");
    }
  
    return err;
}

SrsGb28181Config SrsGb28181Manger::get_gb28181_config()
{
    return *config;
}

void SrsGb28181Manger::alloc_port(int* pport)
{
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
}

void SrsGb28181Manger::free_port(int lpmin, int lpmax)
{
    for (int i = lpmin; i < lpmax; i++) {
        used_ports[i] = false;
    }
    srs_trace("gb28181: free rtp port=%d-%d", lpmin, lpmax);
}

uint32_t SrsGb28181Manger::hash_code(std::string str)
{  
    uint32_t h = 0;  
    int len = str.length();
    
    if (h == 0) {  
        int off = 0;  
        const char *val = str.c_str();
        
        for (int i = 0; i < len; i++) {  
            h = 31 * h + val[off++];  
        }  
    }  
    return h;  
} 

uint32_t SrsGb28181Manger::generate_ssrc(std::string id)
{
    srand(uint(time(0)));
    // TODO: SSRC rules can be customized,
    //gb28181 live ssrc max value 0999999999(3B9AC9FF)  
    //gb28181 vod ssrc max value 1999999999(773593FF)
    uint8_t  index = uint8_t(rand() % (0x0F - 0x01 + 1) + 0x01);
    uint32_t ssrc = 0x2FFFF00 & (hash_code(id) << 8) | index;
    //uint32_t ssrc = 0x00FFFFFF & (hash_code(id));
    srs_trace("gb28181: generate ssrc id=%s, ssrc=%u", id.c_str(), ssrc);
    return  ssrc;
}

srs_error_t SrsGb28181Manger::fetch_or_create_rtmpmuxer(std::string id,  SrsGb28181RtmpMuxer** gb28181)
{
    srs_error_t err = srs_success;

    SrsGb28181RtmpMuxer* muxer = NULL;
    if ((muxer = fetch_rtmpmuxer(id)) != NULL) {
        *gb28181 = muxer;
        return err;
    }
    
    muxer = new SrsGb28181RtmpMuxer(this, id, config->audio_enable, config->wait_keyframe);
    if ((err = muxer->serve()) != srs_success) {
        return srs_error_wrap(err, "gb28181: rtmp muxer serve %s", id.c_str());
    }
    rtmpmuxers[id] = muxer;
    *gb28181 = muxer;
    
    return err;
}

SrsGb28181RtmpMuxer* SrsGb28181Manger::fetch_rtmpmuxer(std::string id)
{
    SrsGb28181RtmpMuxer* muxer = NULL;
    
    if (rtmpmuxers.find(id) == rtmpmuxers.end()) {
        return NULL;
    }
    
    muxer = rtmpmuxers[id];
    return muxer;
}

SrsGb28181RtmpMuxer* SrsGb28181Manger::fetch_rtmpmuxer_by_ssrc(uint32_t ssrc)
{
    SrsGb28181RtmpMuxer* muxer = NULL;
    if (rtmpmuxers_ssrc.find(ssrc) == rtmpmuxers_ssrc.end()) {
        return NULL;
    }
    
    muxer = rtmpmuxers_ssrc[ssrc];
    return muxer;
}

void SrsGb28181Manger::rtmpmuxer_map_by_ssrc(SrsGb28181RtmpMuxer*muxer, uint32_t ssrc)
{
    if (rtmpmuxers_ssrc.find(ssrc) == rtmpmuxers_ssrc.end()) {
         rtmpmuxers_ssrc[ssrc] = muxer;
    }
}

void SrsGb28181Manger::rtmpmuxer_unmap_by_ssrc(uint32_t ssrc)
{
    std::map<uint32_t, SrsGb28181RtmpMuxer*>::iterator it = rtmpmuxers_ssrc.find(ssrc);
    if (it != rtmpmuxers_ssrc.end()) {
         rtmpmuxers_ssrc.erase(it);
    }
}

void SrsGb28181Manger::destroy()
{
    //destory ps rtp listen
    std::map<uint32_t, SrsPsRtpListener*>::iterator it;
    for (it = rtp_pool.begin(); it != rtp_pool.end(); ++it) {
        SrsPsRtpListener* listener = it->second;
        srs_freep(listener);
    }
    rtp_pool.clear();

    //destory gb28181 muxer
    std::map<std::string, SrsGb28181RtmpMuxer*>::iterator it2;
    for (it2 = rtmpmuxers.begin(); it2 != rtmpmuxers.end(); ++it2) {
        SrsGb28181RtmpMuxer* muxer = it2->second;
        SrsGb28181StreamChannel sess = muxer->get_channel();
        rtmpmuxer_unmap_by_ssrc(sess.get_ssrc());
        manager->remove(muxer);
    }
    rtmpmuxers.clear();
}

void SrsGb28181Manger::remove(SrsGb28181RtmpMuxer* muxer)
{
    std::string id = muxer->get_channel_id();
  
    map<std::string, SrsGb28181RtmpMuxer*>::iterator it = rtmpmuxers.find(id);
    if (it != rtmpmuxers.end()) {
        SrsGb28181RtmpMuxer* muxer = it->second;
        SrsGb28181StreamChannel sess = muxer->get_channel();
        rtmpmuxer_unmap_by_ssrc(sess.get_ssrc());
        rtmpmuxers.erase(it);
    }
    manager->remove(muxer);
}

void SrsGb28181Manger::remove_sip_session(SrsGb28181SipSession* sess)
{
    manager->remove(sess);
}

srs_error_t SrsGb28181Manger::start_ps_rtp_listen(std::string id, int port)
{
    srs_error_t err = srs_success;
    if (port == config->rtp_mux_port) {
        return srs_error_wrap(err, "start rtp listen port is mux port"); 
    }

    map<std::string, SrsGb28181RtmpMuxer*>::iterator key = rtmpmuxers.find(id);
    if (key == rtmpmuxers.end()){
       return srs_error_wrap(err, "start rtp listen port rtmp muxer is null"); 
    }

    if (rtp_pool.find(port) == rtp_pool.end())
    {
        SrsPsRtpListener* rtp = new SrsPsRtpListener(this->config, port, id);
        rtp_pool[port] = rtp;
        if ((err = rtp_pool[port]->listen()) != srs_success) {
            stop_rtp_listen(id);
            return srs_error_wrap(err, "rtp listen");
        }

        srs_trace("gb28181: start rtp ps stream over server-port=%d", port);
    }

    return err;
}

void SrsGb28181Manger::stop_rtp_listen(std::string id)
{
    map<std::string, SrsGb28181RtmpMuxer*>::iterator it = rtmpmuxers.find(id);
    if (it == rtmpmuxers.end()){
       return; 
    }

    SrsGb28181RtmpMuxer* muxer = it->second;
    SrsGb28181StreamChannel sess = muxer->get_channel();

    int port = sess.get_rtp_port();
    if (port == config->rtp_mux_port) {
        return; 
    }

    map<uint32_t, SrsPsRtpListener*>::iterator it2 = rtp_pool.find(port);
    if (it2 != rtp_pool.end()){
        srs_freep(it2->second);
        rtp_pool.erase(it2);
    }

    free_port(port, port+1);
}

//api
srs_error_t SrsGb28181Manger::create_stream_channel(SrsGb28181StreamChannel *channel)
{
    srs_error_t err = srs_success;
    srs_assert(channel);

    std::string id = channel->get_channel_id();
    SrsGb28181RtmpMuxer *muxer = NULL;

    muxer = fetch_rtmpmuxer(id);
    if (muxer){
       SrsGb28181StreamChannel s = muxer->get_channel();
       channel->copy(&s);
       //return ERROR_GB28181_SESSION_IS_EXIST;
       return err;
    }

    //create on rtmp muxer, gb28181 stream to rtmp
   
    if ((err = fetch_or_create_rtmpmuxer(id, &muxer)) != srs_success){
        srs_warn("gb28181: create rtmp muxer error, %s", srs_error_desc(err).c_str());
        return err;
    }

    //Start RTP listening port, receive gb28181 stream, 
    //fixed is mux port, 
    //random is random allocation port
    int rtp_port = 0;
    std::string port_mode = channel->get_port_mode();
    
    if (port_mode.empty()){
        port_mode = RTP_PORT_MODE_FIXED;
        channel->set_port_mode(port_mode);
    }
   
    if (port_mode == RTP_PORT_MODE_RANDOM){
        alloc_port(&rtp_port);
        if (rtp_port <= 0){
           return srs_error_new(ERROR_GB28181_RTP_PORT_FULL, "gb28181: rtp port full");
        }
     
        if ((err = start_ps_rtp_listen(id, rtp_port)) != srs_success){
            free_port(rtp_port, rtp_port + 1);
            return  err;
        }
    }
    else if(port_mode == RTP_PORT_MODE_FIXED) {
        rtp_port = config->rtp_mux_port;
    }
    else{
        return srs_error_new(ERROR_GB28181_PORT_MODE_INVALID, "gb28181: port mode invalid");
    }

    uint32_t ssrc = channel->get_ssrc();
    if (ssrc == 0){
        //auto generate SSRC according to the hash code, 
        //of the string value of the id
        ssrc = generate_ssrc(id);
    }
    rtmpmuxer_map_by_ssrc(muxer, ssrc);

    //generate RTMP push stream address,
    //if the app and stream in the API are empty, 
    //RTMP URL is generated using the output template parameter
    std::string url = "";
    int rtmp_port;
    string app = channel->get_app();
    string stream = channel->get_stream();

    if (true) {
        string tcUrl, stream_name;

        //get template rtmp url configuration
        std::string output = config->output;
        srs_parse_rtmp_url(output, tcUrl, stream_name);
        
        string _schema, _host, _vhost, _param, _app, _stream;
        srs_discovery_tc_url(tcUrl, _schema, _host, _vhost, _app, _stream, rtmp_port, _param);

        //if the stream name is not parameterized, 
        //it needs to be parameterized to ensure that the stream name is different
        if (!srs_string_contains(stream_name, "[stream]") &&
            !srs_string_contains(stream_name, "[timestamp]") &&
            !srs_string_contains(stream_name, "[ssrc]")){
            stream_name = stream_name + "_[stream]";
        }

        if (app.empty()){
            app = _app;
        }

        if (stream.empty())
        {
            stream = stream_name;
        }

        url = srs_generate_rtmp_url(_host, rtmp_port, "", "", app, stream, "");
        url = srs_string_replace(url, "[app]", "live");
        url = srs_string_replace(url, "[stream]", id);
        std::stringstream ss;
        ss << ssrc;
        url = srs_string_replace(url, "[ssrc]", ss.str());
        url = srs_path_build_timestamp(url);
        
        //update channel app stream value
        srs_parse_rtmp_url(url, tcUrl, stream_name);
        srs_discovery_tc_url(tcUrl, _schema, _host, _vhost, _app, _stream, rtmp_port, _param);
        
        //generate the value returned to the api response
        channel->set_rtp_port(rtp_port);
        channel->set_ssrc(ssrc);

        channel->set_app(_app);
        channel->set_stream(stream_name);
        channel->set_rtmp_port(rtmp_port);
        channel->set_ip(config->host);
        std::string play_url = srs_generate_rtmp_url(config->host, rtmp_port, "", "", app, stream_name, "");
        channel->set_rtmp_url(play_url);
    }

    muxer->set_rtmp_url(url);
    srs_trace("gb28181: create new stream channel id:%s rtmp url=%s", id.c_str(), url.c_str());

    muxer->copy_channel(channel);

    return err;
}

srs_error_t SrsGb28181Manger::delete_stream_channel(std::string id)
{
    srs_error_t err = srs_success;

    //notify the device to stop streaming 
    //if an internal sip service controlled channel
    notify_sip_bye(id, id);

    SrsGb28181RtmpMuxer *muxer = fetch_rtmpmuxer(id);
    if (muxer){
        stop_rtp_listen(id);
        muxer->stop();
       return err;
    }else {
       return srs_error_new(ERROR_GB28181_SESSION_IS_NOTEXIST, "stream channel is not exists");
    }
}


srs_error_t SrsGb28181Manger::query_stream_channel(std::string id, SrsJsonArray* arr)
{
    srs_error_t err = srs_success;

    if (!id.empty()){
        SrsGb28181RtmpMuxer *muxer = fetch_rtmpmuxer(id);
        if (!muxer){
            return srs_error_new(ERROR_GB28181_SESSION_IS_NOTEXIST, "stream channel not exists");
        }
        SrsJsonObject* obj = SrsJsonAny::object();
        arr->append(obj);
        muxer->get_channel().dumps(obj);
    }else {
        std::map<std::string, SrsGb28181RtmpMuxer*>::iterator it2;
        for (it2 = rtmpmuxers.begin(); it2 != rtmpmuxers.end(); ++it2) {
            SrsGb28181RtmpMuxer* muxer = it2->second;
            SrsJsonObject* obj = SrsJsonAny::object();
            arr->append(obj);
            muxer->get_channel().dumps(obj);
        }
    }

    return err;
}

srs_error_t SrsGb28181Manger::notify_sip_invite(std::string id, std::string ip, int port, uint32_t ssrc, std::string chid)
{
    srs_error_t err = srs_success;
    
    if (!sip_service){
        return srs_error_new(ERROR_GB28181_SIP_NOT_RUN, "sip not run");
    }
   
    //if RTMP Muxer does not exist, you need to create
    std::string key = id+"@"+chid;
    SrsGb28181RtmpMuxer *muxer = fetch_rtmpmuxer(key);
   
    if (!muxer){
        //if there is an invalid parameter, the channel will be created automatically
        if (ip.empty() || port == 0 || ssrc == 0){
             //channel not exist
            SrsGb28181StreamChannel channel;
            channel.set_channel_id(key);
            err =  create_stream_channel(&channel);
            if (err != srs_success){
                return err;
            }

            ip = channel.get_ip();
            port = channel.get_rtp_port();
            ssrc = channel.get_ssrc();
        }
    }else {
        //channel exit, use channel config
        SrsGb28181StreamChannel channel = muxer->get_channel();
        ip = channel.get_ip();
        port = channel.get_rtp_port();
        ssrc = channel.get_ssrc();
    }

    SrsSipRequest req;
    req.sip_auth_id = id;
    return sip_service->send_invite(&req, ip, port, ssrc, chid);
}

srs_error_t SrsGb28181Manger::notify_sip_bye(std::string id, std::string chid)
{
    if (!sip_service){
        return srs_error_new(ERROR_GB28181_SIP_NOT_RUN, "sip not run");
    }

    SrsSipRequest req;
    req.sip_auth_id = id;
    return sip_service->send_bye(&req, chid);
}

srs_error_t SrsGb28181Manger::notify_sip_ptz(std::string id, std::string chid, std::string cmd, 
            uint8_t speed,  int priority)
{
    if (!sip_service){
        return srs_error_new(ERROR_GB28181_SIP_NOT_RUN, "sip not run");
    }

    SrsSipRequest req;
    req.sip_auth_id = id;
    return sip_service->send_ptz(&req, chid, cmd, speed, priority);
}

srs_error_t SrsGb28181Manger::notify_sip_raw_data(std::string id, std::string data)
{
    if (!sip_service){
        return srs_error_new(ERROR_GB28181_SIP_NOT_RUN, "sip not run");
    }

    SrsSipRequest req;
    req.sip_auth_id = id;
    return sip_service->send_sip_raw_data(&req, data);

}

srs_error_t SrsGb28181Manger::notify_sip_unregister(std::string id)
{
    if (!sip_service){
        return srs_error_new(ERROR_GB28181_SIP_NOT_RUN, "sip not run");
    }
    sip_service->remove_session(id);
    return delete_stream_channel(id);
}

srs_error_t SrsGb28181Manger::notify_sip_query_catalog(std::string id)
{
    if (!sip_service){
        return srs_error_new(ERROR_GB28181_SIP_NOT_RUN, "sip not run");
    }

    SrsSipRequest req;
    req.sip_auth_id = id;
    return sip_service->send_query_catalog(&req);
}

srs_error_t SrsGb28181Manger::query_sip_session(std::string id, SrsJsonArray* arr)
{
    if (!sip_service){
        return srs_error_new(ERROR_GB28181_SIP_NOT_RUN, "sip not run");
    }
    
    return sip_service->query_sip_session(id, arr);
}