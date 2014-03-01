/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#ifdef SRS_HLS

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include <srs_kernel_error.hpp>
#include <srs_core_codec.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_core_config.hpp>
#include <srs_core_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_core_pithy_print.hpp>

// max PES packets size to flush the video.
#define SRS_HLS_AUDIO_CACHE_SIZE 512 * 1024

// @see: NGX_RTMP_HLS_DELAY, 
// 63000: 700ms, ts_tbn=90000
#define SRS_HLS_DELAY 63000

// the mpegts header specifed the video/audio pid.
#define TS_VIDEO_PID 256
#define TS_AUDIO_PID 257

// ts aac stream id.
#define TS_AUDIO_AAC 0xc0
// ts avc stream id.
#define TS_VIDEO_AVC 0xe0

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

// @see: ngx_rtmp_SrsMpegtsFrame_t
struct SrsMpegtsFrame
{
    int64_t		pts;
    int64_t		dts;
    int  		pid;
    int			sid;
    int			cc;
    bool		key;
    
    SrsMpegtsFrame()
    {
        pts = dts = 0;
        pid = sid = cc = 0;
        key = false;
    }
};

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
	static int write_frame(int fd, SrsMpegtsFrame* frame, SrsCodecBuffer* buffer)
	{
		int ret = ERROR_SUCCESS;
		
		if (!buffer->bytes || buffer->size <= 0) {
			return ret;
		}
		
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

SrsHlsAacJitter::~SrsHlsAacJitter()
{
}

int64_t SrsHlsAacJitter::on_buffer_start(int64_t flv_pts, int sample_rate)
{
	// 0 = 5.5 kHz = 5512 Hz
	// 1 = 11 kHz = 11025 Hz
	// 2 = 22 kHz = 22050 Hz
	// 3 = 44 kHz = 44100 Hz
	static int flv_sample_rates[] = {5512, 11025, 22050, 44100};
	int flv_sample_rate = flv_sample_rates[sample_rate & 0x03];

	// sync time set to 0, donot adjust the aac timestamp.
	if (!sync_ms) {
		return flv_pts;
	}
	
	// @see: ngx_rtmp_hls_audio
    /* TODO: We assume here AAC frame size is 1024
     *       Need to handle AAC frames with frame size of 960 */
	int64_t est_pts = base_pts + nb_samples * 90000LL * 1024LL / flv_sample_rate;
	int64_t dpts = (int64_t) (est_pts - flv_pts);

    if (dpts <= (int64_t) sync_ms * 90 && dpts >= (int64_t) sync_ms * -90) {
        srs_info("HLS correct aac pts "
        	"from %"PRId64" to %"PRId64", base=%"PRId64", nb_samples=%d, sample_rate=%d",
        	flv_pts, est_pts, nb_samples, flv_sample_rate, base_pts);

		nb_samples++;
		
		return est_pts;
    }
    
    // resync
	srs_trace("HLS aac resync, dpts=%"PRId64", pts=%"PRId64
		", base=%"PRId64", nb_samples=%"PRId64", sample_rate=%d",
		dpts, flv_pts, base_pts, nb_samples, flv_sample_rate);
	
    base_pts = flv_pts;
    nb_samples = 1;
    
    return flv_pts;
}

void SrsHlsAacJitter::on_buffer_continue()
{
	nb_samples++;
}

SrsTSMuxer::SrsTSMuxer()
{
	fd = -1;
}

SrsTSMuxer::~SrsTSMuxer()
{
	close();
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

int SrsTSMuxer::write_audio(SrsMpegtsFrame* af, SrsCodecBuffer* ab)
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = SrsMpegtsWriter::write_frame(fd, af, ab)) != ERROR_SUCCESS) {
		return ret;
	}
	
	return ret;
}

int SrsTSMuxer::write_video(SrsMpegtsFrame* vf, SrsCodecBuffer* vb)
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = SrsMpegtsWriter::write_frame(fd, vf, vb)) != ERROR_SUCCESS) {
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

SrsM3u8Segment::SrsM3u8Segment()
{
	duration = 0;
	sequence_no = 0;
	muxer = new SrsTSMuxer();
	segment_start_dts = 0;
}

SrsM3u8Segment::~SrsM3u8Segment()
{
	srs_freep(muxer);
}

double SrsM3u8Segment::update_duration(int64_t video_stream_dts)
{
	duration = (video_stream_dts - segment_start_dts) / 90000.0;
	srs_assert(duration >= 0);
	
	return duration;
}

SrsHlsAacJitter::SrsHlsAacJitter()
{
	base_pts = 0;
	nb_samples = 0;

	// TODO: config it, 0 means no adjust
	sync_ms = SRS_CONF_DEFAULT_AAC_SYNC;
}

SrsM3u8Muxer::SrsM3u8Muxer()
{
	hls_fragment = hls_window = 0;
	video_stream_dts = 0;
	file_index = 0;
	current = NULL;
}

SrsM3u8Muxer::~SrsM3u8Muxer()
{
	std::vector<SrsM3u8Segment*>::iterator it;
	for (it = segments.begin(); it != segments.end(); ++it) {
		SrsM3u8Segment* segment = *it;
		srs_freep(segment);
	}
	segments.clear();
	
	srs_freep(current);
}

int SrsM3u8Muxer::update_config(
	std::string _app, std::string _stream, 
	std::string path, int fragment, int window
) {
	int ret = ERROR_SUCCESS;
	
	app = _app;
	stream = _stream;
	hls_path = path;
	hls_fragment = fragment;
	hls_window = window;
	
	return ret;
}

int SrsM3u8Muxer::segment_open()
{
	int ret = ERROR_SUCCESS;
	
	if (current) {
		srs_warn("ignore the segment open, for segment is already open.");
		return ret;
	}
	
	// TODO: create all parents dirs.
	// create dir for app.
	if ((ret = create_dir()) != ERROR_SUCCESS) {
		return ret;
	}
	
	// when segment open, the current segment must be NULL.
	srs_assert(!current);
	
	// new segment.
	current = new SrsM3u8Segment();
	current->sequence_no = file_index++;
	current->segment_start_dts = video_stream_dts;
	
	// generate filename.
	char filename[128];
	snprintf(filename, sizeof(filename), 
		"%s-%d.ts", stream.c_str(), current->sequence_no);
	
	// TODO: use temp file and rename it.
	current->full_path = hls_path;
	current->full_path += "/";
	current->full_path += app;
	current->full_path += "/";
	current->full_path += filename;
	
	// TODO: support base url, and so on.
	current->uri = filename;
	
	if ((ret = current->muxer->open(current->full_path)) != ERROR_SUCCESS) {
		srs_error("open hls muxer failed. ret=%d", ret);
		return ret;
	}
	srs_info("open HLS muxer success. vhost=%s, path=%s", 
		vhost.c_str(), current->full_path.c_str());
	
	return ret;
}

int SrsM3u8Muxer::flush_audio(SrsMpegtsFrame* af, SrsCodecBuffer* ab)
{
	int ret = ERROR_SUCCESS;

	// if current is NULL, segment is not open, ignore the flush event.
	if (!current) {
		srs_warn("flush audio ignored, for segment is not open.");
		return ret;
	}
	
	if (ab->size <= 0) {
		return ret;
	}
	
	if ((ret = current->muxer->write_audio(af, ab)) != ERROR_SUCCESS) {
		return ret;
	}
	
	// write success, clear and free the buffer
	ab->free();

	return ret;
}

int SrsM3u8Muxer::flush_video(
	SrsMpegtsFrame* af, SrsCodecBuffer* ab, 
	SrsMpegtsFrame* vf, SrsCodecBuffer* vb)
{
	int ret = ERROR_SUCCESS;

	// if current is NULL, segment is not open, ignore the flush event.
	if (!current) {
		srs_warn("flush video ignored, for segment is not open.");
		return ret;
	}
	
	video_stream_dts = vf->dts;
	
	srs_assert(current);
	// reopen the muxer for a gop
	if (vf->key && current->duration >= hls_fragment) {
		// TODO: flush audio before or after segment?
		/*
		if ((ret = flush_audio(af, ab)) != ERROR_SUCCESS) {
			srs_error("m3u8 muxer flush audio failed. ret=%d", ret);
			return ret;
		}
		*/
		
		if ((ret = segment_close()) != ERROR_SUCCESS) {
			srs_error("m3u8 muxer close segment failed. ret=%d", ret);
			return ret;
		}
		
		if ((ret = segment_open()) != ERROR_SUCCESS) {
			srs_error("m3u8 muxer open segment failed. ret=%d", ret);
			return ret;
		}
	
		// TODO: flush audio before or after segment?
		// segment open, flush the audio.
		// @see: ngx_rtmp_hls_open_fragment
	    /* start fragment with audio to make iPhone happy */
		if ((ret = flush_audio(af, ab)) != ERROR_SUCCESS) {
			srs_error("m3u8 muxer flush audio failed. ret=%d", ret);
			return ret;
		}
	}
	
	// update the duration of segment.
	current->update_duration(video_stream_dts);
	
	if ((ret = current->muxer->write_video(vf, vb)) != ERROR_SUCCESS) {
		return ret;
	}
		
	// write success, clear and free the buffer
	vb->free();
	
	return ret;
}

int SrsM3u8Muxer::segment_close()
{
	int ret = ERROR_SUCCESS;
	
	if (!current) {
		srs_warn("ignore the segment close, for segment is not open.");
		return ret;
	}
	
	// when close current segment, the current segment must not be NULL.
	srs_assert(current);

	// assert segment duplicate.
	std::vector<SrsM3u8Segment*>::iterator it;
	it = std::find(segments.begin(), segments.end(), current);
	srs_assert(it == segments.end());

	// valid, add to segments.
	segments.push_back(current);
	
	srs_trace("reap ts segment, sequence_no=%d, uri=%s, duration=%.2f, start=%"PRId64"",
		current->sequence_no, current->uri.c_str(), current->duration, 
		current->segment_start_dts);
	
	// close the muxer of finished segment.
	srs_freep(current->muxer);
	current = NULL;
	
	// the segments to remove
	std::vector<SrsM3u8Segment*> segment_to_remove;
	
	// shrink the segments.
	double duration = 0;
	int remove_index = -1;
	for (int i = segments.size() - 1; i >= 0; i--) {
		SrsM3u8Segment* segment = segments[i];
		duration += segment->duration;
		
		if ((int)duration > hls_window) {
			remove_index = i;
			break;
		}
	}
	for (int i = 0; i < remove_index && !segments.empty(); i++) {
		SrsM3u8Segment* segment = *segments.begin();
		segments.erase(segments.begin());
		segment_to_remove.push_back(segment);
	}
	
	// refresh the m3u8, donot contains the removed ts
	ret = refresh_m3u8();

	// remove the ts file.
	for (int i = 0; i < (int)segment_to_remove.size(); i++) {
		SrsM3u8Segment* segment = segment_to_remove[i];
		unlink(segment->full_path.c_str());
		srs_freep(segment);
	}
	segment_to_remove.clear();
	
	// check ret of refresh m3u8
	if (ret != ERROR_SUCCESS) {
		srs_error("refresh m3u8 failed. ret=%d", ret);
		return ret;
	}
	
	return ret;
}

int SrsM3u8Muxer::refresh_m3u8()
{
	int ret = ERROR_SUCCESS;
	
	std::string m3u8_file = hls_path;
	m3u8_file += "/";
	m3u8_file += app;
	m3u8_file += "/";
	m3u8_file += stream;
	m3u8_file += ".m3u8";
	
	m3u8 = m3u8_file;
	m3u8_file += ".temp";
	
	int fd = -1;
	ret = _refresh_m3u8(fd, m3u8_file);
	if (fd >= 0) {
		close(fd);
		if (rename(m3u8_file.c_str(), m3u8.c_str()) < 0) {
			ret = ERROR_HLS_WRITE_FAILED;
			srs_error("rename m3u8 file failed. ret=%d", ret);
		}
	}
	
	// remove the temp file.
	unlink(m3u8_file.c_str());
	
	return ret;
}

int SrsM3u8Muxer::_refresh_m3u8(int& fd, std::string m3u8_file)
{
	int ret = ERROR_SUCCESS;
	
	// no segments, return.
	if (segments.size() == 0) {
		return ret;
	}
	
	int flags = O_CREAT|O_WRONLY|O_TRUNC;
	mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;
	if ((fd = ::open(m3u8_file.c_str(), flags, mode)) < 0) {
		ret = ERROR_HLS_OPEN_FAILED;
		srs_error("open m3u8 file %s failed. ret=%d", m3u8_file.c_str(), ret);
		return ret;
	}
	srs_info("open m3u8 file %s success.", m3u8_file.c_str());
	
	// #EXTM3U\n#EXT-X-VERSION:3\n
	char header[] = {
		// #EXTM3U\n
		0x23, 0x45, 0x58, 0x54, 0x4d, 0x33, 0x55, 0xa, 
		// #EXT-X-VERSION:3\n
		0x23, 0x45, 0x58, 0x54, 0x2d, 0x58, 0x2d, 0x56, 0x45, 0x52, 
		0x53, 0x49, 0x4f, 0x4e, 0x3a, 0x33, 0xa
	};
	if (::write(fd, header, sizeof(header)) != sizeof(header)) {
		ret = ERROR_HLS_WRITE_FAILED;
		srs_error("write m3u8 header failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("write m3u8 header success.");
	
	// #EXT-X-MEDIA-SEQUENCE:4294967295\n
	SrsM3u8Segment* first = *segments.begin();
	char sequence[34] = {};
	int len = snprintf(sequence, sizeof(sequence), "#EXT-X-MEDIA-SEQUENCE:%d\n", first->sequence_no);
	if (::write(fd, sequence, len) != len) {
		ret = ERROR_HLS_WRITE_FAILED;
		srs_error("write m3u8 sequence failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("write m3u8 sequence success.");
	
	// #EXT-X-TARGETDURATION:4294967295\n
	int target_duration = 0;
	std::vector<SrsM3u8Segment*>::iterator it;
	for (it = segments.begin(); it != segments.end(); ++it) {
		SrsM3u8Segment* segment = *it;
		target_duration = srs_max(target_duration, (int)segment->duration);
	}
	// TODO: maybe need to take an around value
	target_duration += 1;
	char duration[34];
	len = snprintf(duration, sizeof(duration), "#EXT-X-TARGETDURATION:%d\n", target_duration);
	if (::write(fd, duration, len) != len) {
		ret = ERROR_HLS_WRITE_FAILED;
		srs_error("write m3u8 duration failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("write m3u8 duration success.");
	
	// write all segments
	for (it = segments.begin(); it != segments.end(); ++it) {
		SrsM3u8Segment* segment = *it;
		
		// "#EXTINF:4294967295.208,\n"
		char ext_info[25];
		len = snprintf(ext_info, sizeof(ext_info), "#EXTINF:%.3f\n", segment->duration);
		if (::write(fd, ext_info, len) != len) {
			ret = ERROR_HLS_WRITE_FAILED;
			srs_error("write m3u8 segment failed. ret=%d", ret);
			return ret;
		}
		srs_verbose("write m3u8 segment success.");
		
		// file name
		std::string filename = segment->uri;
		filename += "\n";
		if (::write(fd, filename.c_str(), filename.length()) != (int)filename.length()) {
			ret = ERROR_HLS_WRITE_FAILED;
			srs_error("write m3u8 segment uri failed. ret=%d", ret);
			return ret;
		}
		srs_verbose("write m3u8 segment uri success.");
	}
	srs_info("write m3u8 %s success.", m3u8_file.c_str());
	
	return ret;
}

int SrsM3u8Muxer::create_dir()
{
	int ret = ERROR_SUCCESS;
	
	std::string app_dir = hls_path;
	app_dir += "/";
	app_dir += app;
	
	// TODO: cleanup the dir when startup.

	mode_t mode = S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP|S_IROTH|S_IXOTH;
	if (::mkdir(app_dir.c_str(), mode) < 0) {
		if (errno != EEXIST) {
			ret = ERROR_HLS_CREATE_DIR;
			srs_error("create app dir %s failed. ret=%d", app_dir.c_str(), ret);
			return ret;
		}
	}
	srs_info("create app dir %s success.", app_dir.c_str());

	return ret;
}

SrsTSCache::SrsTSCache()
{
	aac_jitter = new SrsHlsAacJitter();
	
	ab = new SrsCodecBuffer();
	vb = new SrsCodecBuffer();
	
	af = new SrsMpegtsFrame();
	vf = new SrsMpegtsFrame();
}

SrsTSCache::~SrsTSCache()
{
	srs_freep(aac_jitter);
	
	ab->free();
	vb->free();
	
	srs_freep(ab);
	srs_freep(vb);
	
	srs_freep(af);
	srs_freep(vf);
}
	
int SrsTSCache::write_audio(SrsCodec* codec, SrsM3u8Muxer* muxer, int64_t pts, SrsCodecSample* sample)
{
	int ret = ERROR_SUCCESS;
	
	// start buffer, set the af
	if (ab->size == 0) {
		pts = aac_jitter->on_buffer_start(pts, sample->sound_rate);
		
		af->dts = af->pts = audio_buffer_start_pts = pts;
		af->pid = TS_AUDIO_PID;
		af->sid = TS_AUDIO_AAC;
	} else {
		aac_jitter->on_buffer_continue();
	}
	
	// write audio to cache.
	if ((ret = cache_audio(codec, sample)) != ERROR_SUCCESS) {
		return ret;
	}
	
	// flush if buffer exceed max size.
	if (ab->size > SRS_HLS_AUDIO_CACHE_SIZE) {
		if ((ret = muxer->flush_audio(af, ab)) != ERROR_SUCCESS) {
			return ret;
		}
	}
	// TODO: config it.
	// in ms, audio delay to flush the audios.
	int64_t audio_delay = SRS_CONF_DEFAULT_AAC_DELAY;
	// flush if audio delay exceed
	if (pts - audio_buffer_start_pts > audio_delay * 90) {
		if ((ret = muxer->flush_audio(af, ab)) != ERROR_SUCCESS) {
			return ret;
		}
	}
	
	return ret;
}
	
int SrsTSCache::write_video(SrsCodec* codec, SrsM3u8Muxer* muxer, int64_t dts, SrsCodecSample* sample)
{
	int ret = ERROR_SUCCESS;
	
	// write video to cache.
	if ((ret = cache_video(codec, sample)) != ERROR_SUCCESS) {
		return ret;
	}
	
	vf->dts = dts;
	vf->pts = vf->dts + sample->cts * 90;
	vf->pid = TS_VIDEO_PID;
	vf->sid = TS_VIDEO_AVC;
	vf->key = sample->frame_type == SrsCodecVideoAVCFrameKeyFrame;
	
	// flush video when got one
	if ((ret = muxer->flush_video(af, ab, vf, vb)) != ERROR_SUCCESS) {
		srs_error("m3u8 muxer flush video failed. ret=%d", ret);
		return ret;
	}
	
	return ret;
}

int SrsTSCache::flush_audio(SrsM3u8Muxer* muxer)
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = muxer->flush_audio(af, ab)) != ERROR_SUCCESS) {
		srs_error("m3u8 muxer flush audio failed. ret=%d", ret);
		return ret;
	}
	
	return ret;
}

int SrsTSCache::cache_audio(SrsCodec* codec, SrsCodecSample* sample)
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
		ab->append(adts_header, sizeof(adts_header));
		ab->append(buf->bytes, buf->size);
	}
	
	return ret;
}

int SrsTSCache::cache_video(SrsCodec* codec, SrsCodecSample* sample)
{
	int ret = ERROR_SUCCESS;
	
	static u_int8_t aud_nal[] = { 0x00, 0x00, 0x00, 0x01, 0x09, 0xf0 };
	vb->append(aud_nal, sizeof(aud_nal));
	
	bool sps_pps_sent = false;
	for (int i = 0; i < sample->nb_buffers; i++) {
		SrsCodecBuffer* buf = &sample->buffers[i];
		int32_t size = buf->size;
		
		if (!buf->bytes || size <= 0) {
			ret = ERROR_HLS_AVC_SAMPLE_SIZE;
			srs_error("invalid avc sample length=%d, ret=%d", size, ret);
			return ret;
		}
		
		// 5bits, 7.3.1 NAL unit syntax, 
		// H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
		u_int8_t nal_unit_type;
		nal_unit_type = *buf->bytes;
		nal_unit_type &= 0x1f;
		
		// Table 7-1 – NAL unit type codes, page 61
		// 1: Coded slice
		if (nal_unit_type == 1) {
			sps_pps_sent = false;
		}
		// 5: Coded slice of an IDR picture.
		// insert sps/pps before IDR or key frame is ok.
		if (nal_unit_type == 5 && !sps_pps_sent) {
		//if (vf->key && !sps_pps_sent) {
			sps_pps_sent = true;
			
			// ngx_rtmp_hls_append_sps_pps
			if (codec->sequenceParameterSetLength > 0) {
				// AnnexB prefix
				vb->append(aud_nal, 4);
				// sps
				vb->append(codec->sequenceParameterSetNALUnit, codec->sequenceParameterSetLength);
			}
			if (codec->pictureParameterSetLength > 0) {
				// AnnexB prefix
				vb->append(aud_nal, 4);
				// pps
				vb->append(codec->pictureParameterSetNALUnit, codec->pictureParameterSetLength);
			}
		}
		
		// sample start prefix, '00 00 00 01' or '00 00 01'
		u_int8_t* p = aud_nal + 1;
		u_int8_t* end = p + 3;
		
		// first AnnexB prefix is long (4 bytes)
		if (i == 0) {
			p = aud_nal;
		}
		vb->append(p, end - p);
		
		// sample data
		vb->append(buf->bytes, buf->size);
	}
	
	return ret;
}

SrsHls::SrsHls(SrsSource* _source)
{
	hls_enabled = false;
	
	source = _source;
	codec = new SrsCodec();
	sample = new SrsCodecSample();
	jitter = new SrsRtmpJitter();
	
	muxer = new SrsM3u8Muxer();
	ts_cache = new SrsTSCache();

	pithy_print = new SrsPithyPrint(SRS_STAGE_HLS);
}

SrsHls::~SrsHls()
{
	srs_freep(codec);
	srs_freep(sample);
	srs_freep(jitter);
	
	srs_freep(muxer);
	srs_freep(ts_cache);
	
	srs_freep(pithy_print);
}

int SrsHls::on_publish(SrsRequest* req)
{
	int ret = ERROR_SUCCESS;
	
	// support multiple publish.
	if (hls_enabled) {
		return ret;
	}

	std::string vhost = req->vhost;
	std::string stream = req->stream;
	std::string app = req->app;
	
	if (!config->get_hls_enabled(vhost)) {
		return ret;
	}
	
	// if enabled, open the muxer.
	hls_enabled = true;
	
	int hls_fragment = config->get_hls_fragment(vhost);
	int hls_window = config->get_hls_window(vhost);
	
	// get the hls path config
	std::string hls_path = config->get_hls_path(vhost);
	
	// open muxer
	if ((ret = muxer->update_config(app, stream, hls_path, hls_fragment, hls_window)) != ERROR_SUCCESS) {
		srs_error("m3u8 muxer update config failed. ret=%d", ret);
		return ret;
	}
	
	if ((ret = muxer->segment_open()) != ERROR_SUCCESS) {
		srs_error("m3u8 muxer open segment failed. ret=%d", ret);
		return ret;
	}
	
	// notice the source to get the cached sequence header.
	if ((ret = source->on_hls_start()) != ERROR_SUCCESS) {
		srs_error("callback source hls start failed. ret=%d", ret);
		return ret;
	}

	return ret;
}

void SrsHls::on_unpublish()
{
	int ret = ERROR_SUCCESS;
	
	// support multiple unpublish.
	if (!hls_enabled) {
		return;
	}
	
	// close muxer when unpublish.
	ret = ts_cache->flush_audio(muxer);
	ret += muxer->segment_close();

	if (ret != ERROR_SUCCESS) {
		srs_error("ignore m3u8 muxer flush/close audio failed. ret=%d", ret);
	}
	
	hls_enabled = false;
}

int SrsHls::on_meta_data(SrsAmf0Object* metadata)
{
	int ret = ERROR_SUCCESS;

	if (!metadata) {
		srs_trace("no metadata persent, hls ignored it.");
		return ret;
	}
	
	SrsAmf0Object* obj = metadata;
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

int SrsHls::on_audio(SrsSharedPtrMessage* audio)
{
	int ret = ERROR_SUCCESS;
	
	SrsAutoFree(SrsSharedPtrMessage, audio, false);
	
	if (!hls_enabled) {
		return ret;
	}
	
	sample->clear();
	if ((ret = codec->audio_aac_demux(audio->payload, audio->size, sample)) != ERROR_SUCCESS) {
		srs_error("codec demux audio failed. ret=%d", ret);
		return ret;
	}
	
	if (codec->audio_codec_id != SrsCodecAudioAAC) {
		return ret;
	}
	
	// ignore sequence header
	if (sample->aac_packet_type == SrsCodecAudioTypeSequenceHeader) {
		return ret;
	}
	
	if ((ret = jitter->correct(audio, 0, 0)) != ERROR_SUCCESS) {
		srs_error("rtmp jitter correct audio failed. ret=%d", ret);
		return ret;
	}
	
	// the pts calc from rtmp/flv header.
	int64_t pts = audio->header.timestamp * 90;
	
	if ((ret = ts_cache->write_audio(codec, muxer, pts, sample)) != ERROR_SUCCESS) {
		srs_error("ts cache write audio failed. ret=%d", ret);
		return ret;
	}
	
	return ret;
}

int SrsHls::on_video(SrsSharedPtrMessage* video)
{
	int ret = ERROR_SUCCESS;
	
	SrsAutoFree(SrsSharedPtrMessage, video, false);
	
	if (!hls_enabled) {
		return ret;
	}
	
	sample->clear();
	if ((ret = codec->video_avc_demux(video->payload, video->size, sample)) != ERROR_SUCCESS) {
		srs_error("codec demux video failed. ret=%d", ret);
		return ret;
	}
	
	if (codec->video_codec_id != SrsCodecVideoAVC) {
		return ret;
	}
	
	// ignore sequence header
	if (sample->frame_type == SrsCodecVideoAVCFrameKeyFrame
		 && sample->avc_packet_type == SrsCodecVideoAVCTypeSequenceHeader) {
		return ret;
	}
	
	if ((ret = jitter->correct(video, 0, 0)) != ERROR_SUCCESS) {
		srs_error("rtmp jitter correct video failed. ret=%d", ret);
		return ret;
	}
	
	int64_t dts = video->header.timestamp * 90;
	if ((ret = ts_cache->write_video(codec, muxer, dts, sample)) != ERROR_SUCCESS) {
		srs_error("ts cache write video failed. ret=%d", ret);
		return ret;
	}
	
	hls_mux();
	
	return ret;
}

void SrsHls::hls_mux()
{
	// reportable
	if (pithy_print->can_print()) {
		srs_trace("-> time=%"PRId64"", pithy_print->get_age());
	}
	
	pithy_print->elapse(sample->cts);
}

#endif

