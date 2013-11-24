/*
The MIT License (MIT)

Copyright (c) 2013 winlin

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

#include <srs_core_hls.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include <srs_core_error.hpp>
#include <srs_core_codec.hpp>
#include <srs_core_amf0.hpp>
#include <srs_core_protocol.hpp>
#include <srs_core_config.hpp>

SrsHLS::SrsHLS()
{
	hls_enabled = false;
	codec = new SrsCodec();
	sample = new SrsCodecSample();
	muxer = NULL;
}

SrsHLS::~SrsHLS()
{
	srs_freep(codec);
	srs_freep(sample);
	srs_freep(muxer);
}

int SrsHLS::on_publish(std::string _vhost)
{
	int ret = ERROR_SUCCESS;

	if (muxer) {
		ret = ERROR_HLS_BUSY;
		srs_error("hls is busy, something error, "
			"vhost=%s, ret=%d", _vhost.c_str(), ret);
		return ret;
	}
	
	vhost = _vhost;
	muxer = new SrsTSMuxer();
	
	// try to open the HLS muxer
	SrsConfDirective* conf = config->get_hls(vhost);
	if (!conf && conf->arg0() == "off") {
		return ret;
	}
	
	// TODO: check the audio and video, ensure both exsists.
	// for use fixed mpegts header specifeid the audio and video pid.

	hls_enabled = true;
	
	std::string path = SRS_CONF_DEFAULT_HLS_PATH;
	if ((conf = config->get_hls_path(vhost)) != NULL) {
		path = conf->arg0();
	}
	
	// TODO: generate by m3u8 muxer.
	path += "/1.ts";
	
	if ((ret = muxer->open(path)) != ERROR_SUCCESS) {
		srs_error("open hls muxer failed. ret=%d", ret);
		return ret;
	}
	
	return ret;
}

void SrsHLS::on_unpublish()
{
	hls_enabled = false;
	muxer->close();
	srs_freep(muxer);
}

int SrsHLS::on_meta_data(SrsOnMetaDataPacket* metadata)
{
	int ret = ERROR_SUCCESS;

	if (!metadata || !metadata->metadata) {
		srs_trace("no metadata persent, hls ignored it.");
		return ret;
	}
	
	SrsAmf0Object* obj = metadata->metadata;
	if (obj->size() <= 0) {
		srs_trace("no metadata persent, hls ignored it.");
		return ret;
	}
	
	//	finger out the codec info from metadata if possible.
	SrsAmf0Any* prop = NULL;

	if ((prop = obj->get_property("duration")) != NULL && prop->is_number()) {
		codec->duration = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	if ((prop = obj->get_property("width")) != NULL && prop->is_number()) {
		codec->width = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	if ((prop = obj->get_property("height")) != NULL && prop->is_number()) {
		codec->height = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	if ((prop = obj->get_property("framerate")) != NULL && prop->is_number()) {
		codec->frame_rate = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	if ((prop = obj->get_property("videocodecid")) != NULL && prop->is_number()) {
		codec->video_codec_id = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	if ((prop = obj->get_property("videodatarate")) != NULL && prop->is_number()) {
		codec->video_data_rate = (int)(1000 * srs_amf0_convert<SrsAmf0Number>(prop)->value);
	}
	
	if ((prop = obj->get_property("audiocodecid")) != NULL && prop->is_number()) {
		codec->audio_codec_id = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	if ((prop = obj->get_property("audiodatarate")) != NULL && prop->is_number()) {
		codec->audio_data_rate = (int)(1000 * srs_amf0_convert<SrsAmf0Number>(prop)->value);
	}
	
	// ignore the following, for each flv/rtmp packet contains them:
	// audiosamplerate, sample->sound_rate
	// audiosamplesize, sample->sound_size
	// stereo, 			sample->sound_type
	
	return ret;
}

int SrsHLS::on_audio(SrsCommonMessage* audio)
{
	int ret = ERROR_SUCCESS;
	
	sample->clear();
	if ((ret = codec->audio_aac_demux(audio->payload, audio->size, sample)) != ERROR_SUCCESS) {
		return ret;
	}
	
	if (codec->audio_codec_id != SrsCodecAudioAAC) {
		return ret;
	}
	
	// TODO: maybe donot need to demux the aac?
	if (!hls_enabled) {
		return ret;
	}
	
	// ignore sequence header
	if (sample->aac_packet_type == SrsCodecAudioTypeSequenceHeader) {
		return ret;
	}
	
	u_int32_t timestamp = audio->header.timestamp;
	// TODO: correct the timestamp.
	
	if ((ret = muxer->write_audio(timestamp, codec, sample)) != ERROR_SUCCESS) {
		return ret;
	}
	
	return ret;
}

int SrsHLS::on_video(SrsCommonMessage* video)
{
	int ret = ERROR_SUCCESS;
	
	sample->clear();
	if ((ret = codec->video_avc_demux(video->payload, video->size, sample)) != ERROR_SUCCESS) {
		return ret;
	}
	
	if (codec->video_codec_id != SrsCodecVideoAVC) {
		return ret;
	}
	
	// TODO: maybe donot need to demux the avc?
	if (!hls_enabled) {
		return ret;
	}
	
	// ignore sequence header
	if (sample->frame_type == SrsCodecVideoAVCFrameKeyFrame && sample->avc_packet_type == SrsCodecVideoAVCTypeSequenceHeader) {
		return ret;
	}
	
	u_int32_t timestamp = video->header.timestamp;
	// TODO: correct the timestamp.
	
	if ((ret = muxer->write_video(timestamp, codec, sample)) != ERROR_SUCCESS) {
		return ret;
	}
	
	return ret;
}

// @see: ngx_rtmp_mpegts_header
u_int8_t mpegts_header[] = {
    /* TS */
    0x47, 0x40, 0x00, 0x10, 0x00,
    /* PSI */
    0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00, 0x00,
    /* PAT */
    0x00, 0x01, 0xf0, 0x01,
    /* CRC */
    0x2e, 0x70, 0x19, 0x05,
    /* stuffing 167 bytes */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    
    /* TS */
    0x47, 0x50, 0x01, 0x10, 0x00,
    /* PSI */
    0x02, 0xb0, 0x17, 0x00, 0x01, 0xc1, 0x00, 0x00,
    /* PMT */
    0xe1, 0x00,
    0xf0, 0x00,
    0x1b, 0xe1, 0x00, 0xf0, 0x00, /* h264, pid=0x100=256 */
    0x0f, 0xe1, 0x01, 0xf0, 0x00, /* aac, pid=0x101=257 */
    /*0x03, 0xe1, 0x01, 0xf0, 0x00,*/ /* mp3 */
    /* CRC */
    0x2f, 0x44, 0xb9, 0x9b, /* crc for aac */
    /*0x4e, 0x59, 0x3d, 0x1e,*/ /* crc for mp3 */
    /* stuffing 157 bytes */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

// @see: NGX_RTMP_HLS_DELAY, 700ms, ts_tbn=90000
#define SRS_HLS_DELAY 63000

// @see: ngx_rtmp_mpegts.c
// TODO: support full mpegts feature in future.
class SrsMpegtsWriter
{
public:
	static int write_header(int fd)
	{
		int ret = ERROR_SUCCESS;
		
		if (::write(fd, mpegts_header, sizeof(mpegts_header)) != sizeof(mpegts_header)) {
			ret = ERROR_HLS_WRITE_FAILED;
			srs_error("write ts file header failed. ret=%d", ret);
			return ret;
		}

		return ret;
	}
	static int write_frame(int fd, mpegts_frame* frame, SrsCodecBuffer* buffer)
	{
		int ret = ERROR_SUCCESS;
		
		char* last = buffer->bytes + buffer->size;
		char* pos = buffer->bytes;
		
		bool first = true;
		while (pos < last) {
			static char packet[188];
			char* p = packet;
			
			frame->cc++;
			
			// sync_byte; //8bits
			*p++ = 0x47;
			// pid; //13bits
			*p++ = (frame->pid >> 8) & 0x1f;
			// payload_unit_start_indicator; //1bit
			if (first) {
				p[-1] |= 0x40;
			}
			*p++ = frame->pid;
			
			// transport_scrambling_control; //2bits
			// adaption_field_control; //2bits, 0x01: PayloadOnly
			// continuity_counter; //4bits
			*p++ = 0x10 | (frame->cc & 0x0f);
			
			if (first) {
				first = false;
				if (frame->key) {
					p[-1] |= 0x20; // Both Adaption and Payload
					*p++ = 7;    // size
					*p++ = 0x50; // random access + PCR
					p = write_pcr(p, frame->dts - SRS_HLS_DELAY);
				}
				
				// PES header
				// packet_start_code_prefix; //24bits, '00 00 01'
				*p++ = 0x00;
				*p++ = 0x00;
				*p++ = 0x01;
				//8bits
				*p++ = frame->sid;
				
				// pts(33bits) need 5bytes.
				u_int8_t header_size = 5;
				u_int8_t flags = 0x80; // pts
				
				// dts(33bits) need 5bytes also
				if (frame->dts != frame->pts) {
					header_size += 5;
					flags |= 0x40; // dts
				}
				
				// 3bytes: flag fields from PES_packet_length to PES_header_data_length
				int pes_size = (last - pos) + header_size + 3;
				if (pes_size > 0xffff) {
					/**
					* when actual packet length > 0xffff(65535),
					* which exceed the max u_int16_t packet length,
					* use 0 packet length, the next unit start indicates the end of packet.
					*/
					pes_size = 0;
				}
				
				// PES_packet_length; //16bits
				*p++ = (pes_size >> 8);
				*p++ = pes_size;
				
				// PES_scrambling_control; //2bits, '10'
				// PES_priority; //1bit
				// data_alignment_indicator; //1bit
				// copyright; //1bit
				// original_or_copy; //1bit	
				*p++ = 0x80; /* H222 */
				
				// PTS_DTS_flags; //2bits
				// ESCR_flag; //1bit
				// ES_rate_flag; //1bit
				// DSM_trick_mode_flag; //1bit
				// additional_copy_info_flag; //1bit
				// PES_CRC_flag; //1bit
				// PES_extension_flag; //1bit
				*p++ = flags;
				
				// PES_header_data_length; //8bits
				*p++ = header_size;

				// pts; // 33bits
				p = write_pts(p, flags >> 6, frame->pts + SRS_HLS_DELAY);
				
				// dts; // 33bits
				if (frame->dts != frame->pts) {
					p = write_pts(p, 1, frame->dts + SRS_HLS_DELAY);
				}
			}
			
			int body_size = sizeof(packet) - (p - packet);
			int in_size = last - pos;
			
			if (body_size <= in_size) {
				memcpy(p, pos, body_size);
				pos += body_size;
			} else {
				p = fill_stuff(p, packet, body_size, in_size);
				memcpy(p, pos, in_size);
				pos = last;
			}
			
			// write ts packet
			if (::write(fd, packet, sizeof(packet)) != sizeof(packet)) {
				ret = ERROR_HLS_WRITE_FAILED;
				srs_error("write ts file failed. ret=%d", ret);
				return ret;
			}
		}
		
		// write success, clear and free the buffer
		buffer->free();
		
		return ret;
	}
private:
	static char* fill_stuff(char* pes_body_end, char* packet, int body_size, int in_size)
	{
		char* p = pes_body_end;
		
		// insert the stuff bytes before PES body
		int stuff_size = (body_size - in_size);
		
		// adaption_field_control; //2bits
		if (packet[3] & 0x20) {
			//  has adaptation
			// packet[4]: adaption_field_length
			// packet[5]: adaption field data
			// base: start of PES body
			char* base = &packet[5] + packet[4];
			int len = p - base;
			p = (char*)memmove(base + stuff_size, base, len) + len;
			// increase the adaption field size.
			packet[4] += stuff_size;
			
			return p;
		}

		// create adaption field.
		// adaption_field_control; //2bits
		packet[3] |= 0x20;
		// base: start of PES body
		char* base = &packet[4];
		int len = p - base;
		p = (char*)memmove(base + stuff_size, base, len) + len;
		// adaption_field_length; //8bits
		packet[4] = (stuff_size - 1);
		if (stuff_size >= 2) {
			// adaption field flags.
			packet[5] = 0;
			// adaption data.
			if (stuff_size > 2) {
				memset(&packet[6], 0xff, stuff_size - 2);
			}
		}
		
		return p;
	}
	static char* write_pcr(char* p, int64_t pcr)
	{
	    *p++ = (char) (pcr >> 25);
	    *p++ = (char) (pcr >> 17);
	    *p++ = (char) (pcr >> 9);
	    *p++ = (char) (pcr >> 1);
	    *p++ = (char) (pcr << 7 | 0x7e);
	    *p++ = 0;
	
	    return p;
	}
	static char* write_pts(char* p, u_int8_t fb, int64_t pts)
	{
	    int32_t val;
	
	    val = fb << 4 | (((pts >> 30) & 0x07) << 1) | 1;
	    *p++ = val;
	
	    val = (((pts >> 15) & 0x7fff) << 1) | 1;
	    *p++ = (val >> 8);
	    *p++ = val;
	
	    val = (((pts) & 0x7fff) << 1) | 1;
	    *p++ = (val >> 8);
	    *p++ = val;
	
	    return p;
	}
};

// the mpegts header specifed the video/audio pid.
#define TS_VIDEO_PID 256
#define TS_AUDIO_PID 257

// ts aac stream id.
#define TS_AUDIO_AAC 0xc0
// ts avc stream id.
#define TS_VIDEO_AVC 0xe0

SrsTSMuxer::SrsTSMuxer()
{
	fd = -1;
	
	audio_buffer = new SrsCodecBuffer();
	video_buffer = new SrsCodecBuffer();
}

SrsTSMuxer::~SrsTSMuxer()
{
	close();
	
	audio_buffer->free();
	video_buffer->free();
	
	srs_freep(audio_buffer);
	srs_freep(video_buffer);
}

int SrsTSMuxer::open(std::string _path)
{
	int ret = ERROR_SUCCESS;
	
	path = _path;
	
	close();
	
	int flags = O_CREAT|O_WRONLY|O_TRUNC;
	mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;
	if ((fd = ::open(path.c_str(), flags, mode)) < 0) {
		ret = ERROR_HLS_OPEN_FAILED;
		srs_error("open ts file %s failed. ret=%d", path.c_str(), ret);
		return ret;
	}

	// write mpegts header
	if ((ret = SrsMpegtsWriter::write_header(fd)) != ERROR_SUCCESS) {
		return ret;
	}
	
	return ret;
}

int SrsTSMuxer::write_audio(u_int32_t time, SrsCodec* codec, SrsCodecSample* sample)
{
	int ret = ERROR_SUCCESS;
	
	for (int i = 0; i < sample->nb_buffers; i++) {
		SrsCodecBuffer* buf = &sample->buffers[i];
		int32_t size = buf->size;
		
		if (!buf->bytes || size <= 0 || size > 0x1fff) {
			ret = ERROR_HLS_AAC_FRAME_LENGTH;
			srs_error("invalid aac frame length=%d, ret=%d", size, ret);
			return ret;
		}
		
		// the frame length is the AAC raw data plus the adts header size.
		int32_t frame_length = size + 7;
		
		// AAC-ADTS
		// 6.2 Audio Data Transport Stream, ADTS
		// in aac-iso-13818-7.pdf, page 26.
		// fixed 7bytes header
		static u_int8_t adts_header[7] = {0xff, 0xf1, 0x00, 0x00, 0x00, 0x0f, 0xfc};
		/*
		// adts_fixed_header
		// 2B, 16bits
		int16_t syncword; //12bits, '1111 1111 1111'
		int8_t ID; //1bit, '0'
		int8_t layer; //2bits, '00'
		int8_t protection_absent; //1bit, can be '1'
		// 12bits
		int8_t profile; //2bit, 7.1 Profiles, page 40
		TSAacSampleFrequency sampling_frequency_index; //4bits, Table 35, page 46
		int8_t private_bit; //1bit, can be '0'
		int8_t channel_configuration; //3bits, Table 8
		int8_t original_or_copy; //1bit, can be '0'
		int8_t home; //1bit, can be '0'
		
		// adts_variable_header
		// 28bits
		int8_t copyright_identification_bit; //1bit, can be '0'
		int8_t copyright_identification_start; //1bit, can be '0'
		int16_t frame_length; //13bits
		int16_t adts_buffer_fullness; //11bits, 7FF signals that the bitstream is a variable rate bitstream.
		int8_t number_of_raw_data_blocks_in_frame; //2bits, 0 indicating 1 raw_data_block()
		*/
		// profile, 2bits
		adts_header[2] = (codec->aac_profile << 6) & 0xc0;
		// sampling_frequency_index 4bits
		adts_header[2] |= (codec->aac_sample_rate << 2) & 0x3c;
		// channel_configuration 3bits
		adts_header[2] |= (codec->aac_channels >> 2) & 0x01;
		adts_header[3] = (codec->aac_channels << 6) & 0xc0;
		// frame_length 13bits
		adts_header[3] |= (frame_length >> 11) & 0x03;
		adts_header[4] = (frame_length >> 3) & 0xff;
		adts_header[5] = ((frame_length << 5) & 0xe0);
		// adts_buffer_fullness; //11bits
		adts_header[5] |= 0x1f;

		// copy to audio buffer
		audio_buffer->append(adts_header, sizeof(adts_header));
		audio_buffer->append(buf->bytes, buf->size);
	}
	
	audio_frame.dts = audio_frame.pts = time * 90;
	audio_frame.pid = TS_AUDIO_PID;
	audio_frame.sid = TS_AUDIO_AAC;
	
	return ret;
}

int SrsTSMuxer::write_video(u_int32_t time, SrsCodec* codec, SrsCodecSample* sample)
{
	int ret = ERROR_SUCCESS;
	
	static u_int8_t aud_nal[] = { 0x00, 0x00, 0x00, 0x01, 0x09, 0xf0 };
	video_buffer->append(aud_nal, sizeof(aud_nal));
	
	for (int i = 0; i < sample->nb_buffers; i++) {
		SrsCodecBuffer* buf = &sample->buffers[i];
		int32_t size = buf->size;
		
		if (!buf->bytes || size <= 0) {
			ret = ERROR_HLS_AVC_SAMPLE_SIZE;
			srs_error("invalid avc sample length=%d, ret=%d", size, ret);
			return ret;
		}
		
		// sample start prefix, '00 00 00 01' or '00 00 01'
		u_int8_t* p = aud_nal + 1;
		u_int8_t* end = p + 3;
		
		// first AnnexB prefix is long (4 bytes)
		if (i == 0) {
			p = aud_nal;
		}
		video_buffer->append(p, end - p);
		
		// sample data
		video_buffer->append(buf->bytes, buf->size);
	}
	
	video_frame.dts = time * 90;
	video_frame.pts = video_frame.dts + sample->cts * 90;
	video_frame.pid = TS_VIDEO_PID;
	video_frame.sid = TS_VIDEO_AVC;
	video_frame.key = sample->frame_type == SrsCodecVideoAVCFrameKeyFrame;
	
	if ((ret = SrsMpegtsWriter::write_frame(fd, &video_frame, video_buffer)) != ERROR_SUCCESS) {
		return ret;
	}
	if ((ret = SrsMpegtsWriter::write_frame(fd, &audio_frame, audio_buffer)) != ERROR_SUCCESS) {
		return ret;
	}
	
	return ret;
}

void SrsTSMuxer::close()
{
	if (fd > 0) {
		::close(fd);
		fd = -1;
	}
}

