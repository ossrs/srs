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

#include <srs_app_dvr.hpp>

#ifdef SRS_AUTO_DVR

#include <fcntl.h>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_stream.hpp>

SrsFileStream::SrsFileStream()
{
    fd = -1;
}

SrsFileStream::~SrsFileStream()
{
    close();
}

int SrsFileStream::open(string file)
{
    int ret = ERROR_SUCCESS;
    
    if (fd > 0) {
        ret = ERROR_SYSTEM_FILE_ALREADY_OPENED;
        srs_error("file %s already opened. ret=%d", _file.c_str(), ret);
        return ret;
    }
    
    int flags = O_CREAT|O_WRONLY|O_TRUNC;
    mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;

    if ((fd = ::open(file.c_str(), flags, mode)) < 0) {
        ret = ERROR_SYSTEM_FILE_OPENE;
        srs_error("open file %s failed. ret=%d", file.c_str(), ret);
        return ret;
    }
    
    _file = file;
    
    return ret;
}

int SrsFileStream::close()
{
    int ret = ERROR_SUCCESS;
    
    if (fd < 0) {
        return ret;
    }
    
    if (::close(fd) < 0) {
        ret = ERROR_SYSTEM_FILE_CLOSE;
        srs_error("close file %s failed. ret=%d", _file.c_str(), ret);
        return ret;
    }
    fd = -1;
    
    return ret;
}

int SrsFileStream::read(void* buf, size_t count, ssize_t* pnread)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nread;
    if ((nread = ::read(fd, buf, count)) < 0) {
        ret = ERROR_SYSTEM_FILE_READ;
        srs_error("read from file %s failed. ret=%d", _file.c_str(), ret);
        return ret;
    }
    
    if (nread == 0) {
        ret = ERROR_SYSTEM_FILE_EOF;
        return ret;
    }
    
    if (pnread != NULL) {
        *pnread = nread;
    }
    
    return ret;
}

int SrsFileStream::write(void* buf, size_t count, ssize_t* pnwrite)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nwrite;
    if ((nwrite = ::write(fd, buf, count)) < 0) {
        ret = ERROR_SYSTEM_FILE_WRITE;
        srs_error("write to file %s failed. ret=%d", _file.c_str(), ret);
        return ret;
    }
    
    if (pnwrite != NULL) {
        *pnwrite = nwrite;
    }
    
    return ret;
}

SrsFlvEncoder::SrsFlvEncoder()
{
    _fs = NULL;
    tag_stream = new SrsStream();
}

SrsFlvEncoder::~SrsFlvEncoder()
{
    srs_freep(tag_stream);
}

int SrsFlvEncoder::initialize(SrsFileStream* fs)
{
    int ret = ERROR_SUCCESS;
    
    _fs = fs;
    
    return ret;
}

int SrsFlvEncoder::write_header()
{
    int ret = ERROR_SUCCESS;
    
    static char flv_header[] = {
        'F', 'L', 'V', // Signatures "FLV"
        (char)0x01, // File version (for example, 0x01 for FLV version 1)
        (char)0x00, // 4, audio; 1, video; 5 audio+video.
        (char)0x00, (char)0x00, (char)0x00, (char)0x09, // DataOffset UI32 The length of this header in bytes
        (char)0x00, (char)0x00, (char)0x00, (char)0x00// PreviousTagSize0 UI32 Always 0
    };
    
    // flv specification should set the audio and video flag,
    // actually in practise, application generally ignore this flag,
    // so we generally set the audio/video to 0.
    
    // write data.
    if ((ret = _fs->write(flv_header, sizeof(flv_header), NULL)) != ERROR_SUCCESS) {
        srs_error("write flv header failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsFlvEncoder::write_metadata(char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    static char tag_header[] = {
        (char)18, // TagType UB [5], 18 = script data
        (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    
    // write data size.
    if ((ret = tag_stream->initialize(tag_header + 1, 3)) != ERROR_SUCCESS) {
        return ret;
    }
    tag_stream->write_3bytes(size);
    
    if ((ret = write_tag(tag_header, sizeof(tag_header), data, size)) != ERROR_SUCCESS) {
        srs_error("write flv data tag failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsFlvEncoder::write_audio(int32_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    static char tag_header[] = {
        (char)8, // TagType UB [5], 8 = audio
        (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    
    // write data size.
    if ((ret = tag_stream->initialize(tag_header + 1, 7)) != ERROR_SUCCESS) {
        return ret;
    }
    tag_stream->write_3bytes(size);
    tag_stream->write_3bytes(timestamp);
    // default to little-endian
    tag_stream->write_1bytes((timestamp >> 24) & 0xFF);
    
    if ((ret = write_tag(tag_header, sizeof(tag_header), data, size)) != ERROR_SUCCESS) {
        srs_error("write flv audio tag failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsFlvEncoder::write_video(int32_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    static char tag_header[] = {
        (char)9, // TagType UB [5], 9 = video
        (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    
    // write data size.
    if ((ret = tag_stream->initialize(tag_header + 1, 7)) != ERROR_SUCCESS) {
        return ret;
    }
    tag_stream->write_3bytes(size);
    tag_stream->write_3bytes(timestamp);
    // default to little-endian
    tag_stream->write_1bytes((timestamp >> 24) & 0xFF);
    
    if ((ret = write_tag(tag_header, sizeof(tag_header), data, size)) != ERROR_SUCCESS) {
        srs_error("write flv video tag failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsFlvEncoder::write_tag(char* header, int header_size, char* tag, int tag_size)
{
    int ret = ERROR_SUCCESS;
    
    // write tag header.
    if ((ret = _fs->write(header, header_size, NULL)) != ERROR_SUCCESS) {
        srs_error("write flv tag header failed. ret=%d", ret);
        return ret;
    }
    
    // write tag data.
    if ((ret = _fs->write(tag, tag_size, NULL)) != ERROR_SUCCESS) {
        srs_error("write flv tag failed. ret=%d", ret);
        return ret;
    }
    
    // PreviousTagSizeN UI32 Size of last tag, including its header, in bytes.
    static char pre_size[4];
    if ((ret = tag_stream->initialize(pre_size, 4)) != ERROR_SUCCESS) {
        return ret;
    }
    tag_stream->write_4bytes(tag_size + header_size);
    if ((ret = _fs->write(pre_size, sizeof(pre_size), NULL)) != ERROR_SUCCESS) {
        srs_error("write flv previous tag size failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

SrsDvrPlan::SrsDvrPlan()
{
    _source = NULL;
    dvr_enabled = false;
    fs = new SrsFileStream();
    enc = new SrsFlvEncoder();
}

SrsDvrPlan::~SrsDvrPlan()
{
    srs_freep(fs);
    srs_freep(enc);
}

int SrsDvrPlan::initialize(SrsSource* source)
{
    int ret = ERROR_SUCCESS;
    
    _source = source;

    return ret;
}

int SrsDvrPlan::flv_open(string stream, string path)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = fs->open(path)) != ERROR_SUCCESS) {
        srs_error("open file stream for file %s failed. ret=%d", path.c_str(), ret);
        return ret;
    }
    
    if ((ret = enc->initialize(fs)) != ERROR_SUCCESS) {
        srs_error("initialize enc by fs for file %s failed. ret=%d", path.c_str(), ret);
        return ret;
    }
    
    if ((ret = enc->write_header()) != ERROR_SUCCESS) {
        srs_error("write flv header for file %s failed. ret=%d", path.c_str(), ret);
        return ret;
    }
    
    if ((ret = _source->on_dvr_start()) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_trace("dvr stream %s to file %s", stream.c_str(), path.c_str());
    return ret;
}

int SrsDvrPlan::flv_close()
{
    return fs->close();
}

int SrsDvrPlan::on_meta_data(SrsOnMetaDataPacket* metadata)
{
    int ret = ERROR_SUCCESS;
    
    if (!dvr_enabled) {
        return ret;
    }
    
    int size = 0;
    char* payload = NULL;
    if ((ret = metadata->encode(size, payload)) != ERROR_SUCCESS) {
        return ret;
    }
    SrsAutoFree(char, payload, true);
    
    if ((ret = enc->write_metadata(payload, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrPlan::on_audio(SrsSharedPtrMessage* audio)
{
    int ret = ERROR_SUCCESS;
    
    if (!dvr_enabled) {
        return ret;
    }
    
    int32_t timestamp = audio->header.timestamp;
    char* payload = (char*)audio->payload;
    int size = (int)audio->size;
    if ((ret = enc->write_audio(timestamp, payload, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrPlan::on_video(SrsSharedPtrMessage* video)
{
    int ret = ERROR_SUCCESS;
    
    if (!dvr_enabled) {
        return ret;
    }
    
    int32_t timestamp = video->header.timestamp;
    char* payload = (char*)video->payload;
    int size = (int)video->size;
    if ((ret = enc->write_video(timestamp, payload, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

SrsDvrPlan* SrsDvrPlan::create_plan()
{
    return new SrsDvrSessionPlan();
}

SrsDvrSessionPlan::SrsDvrSessionPlan()
{
}

SrsDvrSessionPlan::~SrsDvrSessionPlan()
{
}

int SrsDvrSessionPlan::on_publish(SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    // support multiple publish.
    if (dvr_enabled) {
        return ret;
    }

    if (!_srs_config->get_dvr_enabled(req->vhost)) {
        return ret;
    }
    
    std::string path = _srs_config->get_dvr_path(req->vhost);
    path += "/";
    path += req->app;
    path += "/";
    path += req->stream;
    path += ".flv";
    
    if ((ret = flv_open(req->get_stream_url(), path)) != ERROR_SUCCESS) {
        return ret;
    }
    dvr_enabled = true;
    
    return ret;
}

void SrsDvrSessionPlan::on_unpublish()
{
    // support multiple publish.
    if (!dvr_enabled) {
        return;
    }
    
    // ignore error.
    int ret = flv_close();
    if (ret != ERROR_SUCCESS) {
        srs_warn("ignore flv close error. ret=%d", ret);
    }
    
    dvr_enabled = false;
}

SrsDvr::SrsDvr(SrsSource* source)
{
    _source = source;
    plan = NULL;
}

SrsDvr::~SrsDvr()
{
    srs_freep(plan);
}

int SrsDvr::initialize()
{
    int ret = ERROR_SUCCESS;
    
    srs_freep(plan);
    plan = SrsDvrPlan::create_plan();

    if ((ret = plan->initialize(_source)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvr::on_publish(SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = plan->on_publish(req)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

void SrsDvr::on_unpublish()
{
    plan->on_unpublish();
}

int SrsDvr::on_meta_data(SrsOnMetaDataPacket* metadata)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = plan->on_meta_data(metadata)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvr::on_audio(SrsSharedPtrMessage* audio)
{
    int ret = ERROR_SUCCESS;
    
    SrsAutoFree(SrsSharedPtrMessage, audio, false);
    
    if ((ret = plan->on_audio(audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvr::on_video(SrsSharedPtrMessage* video)
{
    int ret = ERROR_SUCCESS;
    
    SrsAutoFree(SrsSharedPtrMessage, video, false);
    
    if ((ret = plan->on_video(video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

#endif

