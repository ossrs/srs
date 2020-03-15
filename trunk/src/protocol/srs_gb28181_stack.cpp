

#include <srs_gb28181_stack.hpp>

#include <stdlib.h>
#include <arpa/inet.h>
#include <map>
using namespace std;

#include <srs_protocol_io.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>

 
Srs2SRtpPacket::Srs2SRtpPacket()
{
    version = 2;
    padding = 0;
    extension = 0;
    csrc_count = 0;
    marker = 1;
    
    payload_type = 0;
    sequence_number = 0;
    timestamp = 0;
    ssrc = 0;
    
    payload = new SrsSimpleBufferX();
    tgtstream = new SrsSimpleBufferX();
    audio = new SrsAudioFrame();
    chunked = false;
    completed = false;
}

Srs2SRtpPacket::~Srs2SRtpPacket()
{
    srs_freep(payload);
    srs_freep(tgtstream);
    srs_freep(audio);
}

void Srs2SRtpPacket::copy(Srs2SRtpPacket* src)
{
    version = src->version;
    padding = src->padding;
    extension = src->extension;
    csrc_count = src->csrc_count;
    marker = src->marker;
    payload_type = src->payload_type;
    sequence_number = src->sequence_number;
    timestamp = src->timestamp;
    ssrc = src->ssrc;
    
    chunked = src->chunked;
    completed = src->completed;

    srs_freep(audio);
    audio = new SrsAudioFrame();
}

void Srs2SRtpPacket::reap(Srs2SRtpPacket* src)
{
    copy(src);
    
    srs_freep(payload);
    payload = src->payload;
    src->payload = NULL;
    
    srs_freep(audio);
    audio = src->audio;
    src->audio = NULL;
}

void Srs2SRtpPacket::copy_v2(Srs2SRtpPacket* src)
{
    version = src->version;
    padding = src->padding;
    extension = src->extension;
    csrc_count = src->csrc_count;
    marker = src->marker;
    payload_type = src->payload_type;
    sequence_number = src->sequence_number;
    timestamp = src->timestamp;
    ssrc = src->ssrc;
    
    chunked = src->chunked;
    completed = src->completed;

    // only works on h264 format and chunked and last chank is completed
	if (payload_type == 96 && chunked && completed) {
		
        srs_freep(audio);
        audio = src->audio;
        src->audio = NULL;
	}
}

void Srs2SRtpPacket::reap_v2(Srs2SRtpPacket* src)
{
    copy_v2(src);
    
    srs_freep(payload);
    payload = src->payload;
    src->payload = NULL;
    
    srs_freep(audio);
    audio = src->audio;
    src->audio = NULL;
}

// beisong: decode gb28181 stream
int Srs2SRtpPacket::decode(SrsBuffer* stream)
{
	int ret = ERROR_SUCCESS;

	if (!stream->require(12)) {
		ret = ERROR_RTP_HEADER_CORRUPT;
		srs_error("rtp header corrupt. ret=%d", ret);
		return ret;
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

	// TODO: may support other stream types in future
	if (payload_type == 96) {
		return decode_96ps_rtp(stream, marker);
	}
	else{
		srs_error("rtp type is not 96 ps . ret=%d", ret);
	}

	return ret;
}

int Srs2SRtpPacket::decode_stream()
{
	int ret = 0;

	// TODO: will switch to different stream cores in future
	ret = decode_96ps_core();
	payload->resetoft();

	return ret;
}

int Srs2SRtpPacket::decode_96ps_rtp(SrsBuffer* stream, int8_t marker)
{
	int ret = 0;

	// atleast 2bytes content.
	if (!stream->require(0)) {
		ret = ERROR_RTP_TYPE96_CORRUPT;
		srs_error("rtsp: rtp type 96 ps corrupt. ret=%d", ret);
		return ret;
	}

	if(marker == 0){
		chunked = true;
		completed = false;
	}
	else {
		// always chunked in ps 
		// considering compatibility for other streams, we set chunked as true always
		chunked = true;
		completed = true;
	}
	payload->append(stream->data() + stream->pos(), stream->size() - stream->pos());

	return ret;
}


#define PH_PSC 0x01ba
#define SYS_HEAD_PSC 0x01bb
#define PS_MAP_PSC 0x01bc
#define PES_V_PSC 0x01e0
#define PES_A_PSC 0x01c0
#define HK_PRIVATE_PSC 0x01bd
int Srs2SRtpPacket::decode_96ps_core()
{
	int ret = 0;

	bool a, b, c;
	Packet_Start_Code psc;
	int psc_len = sizeof(Packet_Start_Code);
	PS_Packet_Header ps_ph;
	int ph_len = sizeof(PS_Packet_Header);
	PS_Sys_Header sys_header;
	int sys_header_len = sizeof(PS_Sys_Header);
	PS_Map ps_map;
	int psm_len = sizeof(PS_Map);
	PS_PES pes;
	int pes_len = sizeof(PS_PES);

	// pesv and pesa
	int p_skip_0 = 0;
	int p_skip_1 = 0;
	psc.start_code = 0;

	while (payload->chk_bytes((char*)&psc, sizeof(Packet_Start_Code))) {

		psc.start_code = htonl(psc.start_code);
		if (psc.start_code == PES_V_PSC) {

			psc.start_code = 0;
			a = payload->read_bytes_x((char*)&pes, sizeof(PS_PES));
			b = payload->skip_x(pes.pes_header_data_length);

			pes.pes_packet_length = htons(pes.pes_packet_length);
			u_int32_t load_len = pes.pes_packet_length - pes.pes_header_data_length - 3;
			c = payload->require(load_len);

			if (!a || !b) {
				ret = ERROR_RTP_PS_CORRUPT;
				srs_error(" core- rtp type 96 ps Currepted. size not enough at 4. ret=%d", ret);
				return ret;
			}

			if (!c) {
				// may loss some packets, copy the last buffer
				srs_warn("core- rtp type 96 ps Loss some packet pesVV len:%d, cursize:%d", load_len, payload->cursize());
				load_len = payload->cursize();

				if (load_len <= 0) {
					srs_warn("core- rtp type 96 pesVV len <=0 cursize:%d, oft:%d, payload len:%d, tgt len:%d will return!",
						payload->cursize(), payload->getoft(), payload->length(), tgtstream->length());
					return ret;
				}
			}

			tgtstream->append(payload->curat(), load_len);
			payload->skip_x(load_len);

		}
		else if (psc.start_code == PES_A_PSC) {

			psc.start_code = 0;
			a = payload->read_bytes_x((char*)&pes, sizeof(PS_PES));
			b = payload->skip_x(pes.pes_header_data_length);

			pes.pes_packet_length = htons(pes.pes_packet_length);
			u_int32_t load_len = pes.pes_packet_length - pes.pes_header_data_length - 3;
			c = payload->require(load_len);

			if (!a || !b) {
				ret = ERROR_RTP_PS_CORRUPT;
				srs_error(" core- rtp type 96 ps Currepted. size not enough at 5. ret=%d", ret);
				return ret;
			}
            
			// len may not enough as packet Loss, but still can work well as we call require(x) in skip_x
			// this is a very stable strategy
			if (!c) {
				// may loss some packets, copy the last buffer
				srs_warn("core- rtp type 96 ps Loss some packet pesA len:%d, cursize:%d", load_len, payload->cursize());
				load_len = payload->cursize();

				if (load_len <= 0) {
					srs_warn("core- rtp type 96 pesA len <=0 cursize:%d, oft:%d, payload len:%d, tgt len:%d will return!",
						payload->cursize(), payload->getoft(), payload->length(), tgtstream->length());
					return ret;
				}
			}

			payload->skip_x(load_len);
		}
		else {
			tgtstream->append(payload->curat(), 1);
			payload->skip_x(1);
		}
	} //while pesn

	return ret;
}