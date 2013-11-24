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
	if ((prop = obj->get_property("audiosamplerate")) != NULL && prop->is_number()) {
		int sound_rate = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
		if (sound_rate == 5512) {
			codec->sound_rate = SrsCodecAudioSampleRate5512;
		} else if (sound_rate == 11025) {
			codec->sound_rate = SrsCodecAudioSampleRate11025;
		} else if (sound_rate == 22050) {
			codec->sound_rate = SrsCodecAudioSampleRate22050;
		} else if (sound_rate == 44100) {
			codec->sound_rate = SrsCodecAudioSampleRate44100;
		} else {
			ret = ERROR_HLS_METADATA;
			srs_error("invalid sound_rate of metadata: %d, ret=%d", sound_rate, ret);
			return ret;
		}
	}
	if ((prop = obj->get_property("audiosamplesize")) != NULL && prop->is_number()) {
		int sound_size = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
		if (sound_size == 16) {
			codec->sound_size = SrsCodecAudioSampleSize16bit;
		} else if (sound_size == 8) {
			codec->sound_size = SrsCodecAudioSampleSize8bit;
		} else {
			ret = ERROR_HLS_METADATA;
			srs_error("invalid sound_size of metadata: %d, ret=%d", sound_size, ret);
			return ret;
		}
	}
	if ((prop = obj->get_property("stereo")) != NULL && prop->is_number()) {
		if (srs_amf0_convert<SrsAmf0Boolean>(prop)->value) {
			codec->sound_type = SrsCodecAudioSoundTypeStereo;
		} else {
			codec->sound_type = SrsCodecAudioSoundTypeMono;
		}
	}
	
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
	
	if ((ret = muxer->write(codec, sample)) != ERROR_SUCCESS) {
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
	
	if ((ret = muxer->write(codec, sample)) != ERROR_SUCCESS) {
		return ret;
	}
	
	return ret;
}

// @see: ngx_rtmp_mpegts_header
static u_char mpegts_header[] = {
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

// the mpegts header specifed the video/audio pid.
#define TS_VIDEO_PID 256
#define TS_AUDIO_PID 257

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
	if (::write(fd, mpegts_header, sizeof(mpegts_header)) != sizeof(mpegts_header)) {
		ret = ERROR_HLS_WRITE_FAILED;
		srs_error("write ts file header %s failed. ret=%d", path.c_str(), ret);
		return ret;
	}
	
	return ret;
}

int SrsTSMuxer::write(SrsCodec* codec, SrsCodecSample* sample)
{
	int ret = ERROR_SUCCESS;
	return ret;
}

void SrsTSMuxer::close()
{
	if (fd > 0) {
		::close(fd);
		fd = -1;
	}
}

