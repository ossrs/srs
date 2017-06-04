/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#include <srs_app_dash.hpp>

#include <srs_kernel_error.hpp>
#include <srs_app_source.hpp>
#include <srs_app_config.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_file.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_mp4.hpp>

#include <sstream>
using namespace std;

SrsInitMp4::SrsInitMp4()
{
    fw = new SrsFileWriter();
    init = new SrsMp4M2tsInitEncoder();
}

SrsInitMp4::~SrsInitMp4()
{
    srs_freep(init);
    srs_freep(fw);
}

int SrsInitMp4::write(SrsFormat* format, bool video, int tid)
{
    int ret = ERROR_SUCCESS;
    
    string path_tmp = tmppath();
    if ((ret = fw->open(path_tmp)) != ERROR_SUCCESS) {
        srs_error("DASH: Open init mp4 failed, path=%s, ret=%d", path_tmp.c_str(), ret);
        return ret;
    }
    
    if ((ret = init->initialize(fw)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = init->write(format, video, tid)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

SrsFragmentedMp4::SrsFragmentedMp4()
{
    fw = new SrsFileWriter();
    enc = new SrsMp4M2tsSegmentEncoder();
}

SrsFragmentedMp4::~SrsFragmentedMp4()
{
    srs_freep(enc);
    srs_freep(fw);
}

int SrsFragmentedMp4::initialize(SrsRequest* r, bool video, SrsMpdWriter* mpd, uint32_t tid)
{
    int ret = ERROR_SUCCESS;
    
    string file_home;
    string file_name;
    int64_t sequence_number;
    uint64_t basetime;
    if ((ret = mpd->get_fragment(video, file_home, file_name, sequence_number, basetime)) != ERROR_SUCCESS) {
        return ret;
    }
    
    string home = _srs_config->get_dash_path(r->vhost);
    set_path(home + "/" + file_home + "/" + file_name);
    
    if ((ret = create_dir()) != ERROR_SUCCESS) {
        return ret;
    }
    
    string path_tmp = tmppath();
    if ((ret = fw->open(path_tmp)) != ERROR_SUCCESS) {
        srs_error("DASH: Open fmp4 failed, path=%s, ret=%d", path_tmp.c_str(), ret);
        return ret;
    }
    
    if ((ret = enc->initialize(fw, (uint32_t)sequence_number, basetime, tid)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsFragmentedMp4::write(SrsSharedPtrMessage* shared_msg, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (shared_msg->is_audio()) {
        uint8_t* sample = (uint8_t*)format->raw;
        uint32_t nb_sample = (uint32_t)format->nb_raw;
        
        uint32_t dts = (uint32_t)shared_msg->timestamp;
        ret = enc->write_sample(SrsMp4HandlerTypeSOUN, 0x00, dts, dts, sample, nb_sample);
    } else if (shared_msg->is_video()) {
        SrsVideoAvcFrameType frame_type = format->video->frame_type;
        uint32_t cts = (uint32_t)format->video->cts;
        
        uint32_t dts = (uint32_t)shared_msg->timestamp;
        uint32_t pts = dts + cts;
        
        uint8_t* sample = (uint8_t*)format->raw;
        uint32_t nb_sample = (uint32_t)format->nb_raw;
        ret = enc->write_sample(SrsMp4HandlerTypeVIDE, frame_type, dts, pts, sample, nb_sample);
    } else {
        return ret;
    }
    
    append(shared_msg->timestamp);
    
    return ret;
}

int SrsFragmentedMp4::reap(uint64_t& dts)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = enc->flush(dts)) != ERROR_SUCCESS) {
        srs_error("DASH: Flush encoder failed, ret=%d", ret);
        return ret;
    }
    
    srs_freep(fw);
    
    if ((ret = rename()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

SrsMpdWriter::SrsMpdWriter()
{
    req = NULL;
    timeshit = update_period = fragment = 0;
    last_update_mpd = -1;
}

SrsMpdWriter::~SrsMpdWriter()
{
}

int SrsMpdWriter::initialize(SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    req = r;
    fragment = _srs_config->get_dash_fragment(r->vhost);
    update_period = _srs_config->get_dash_update_period(r->vhost);
    timeshit = _srs_config->get_dash_timeshift(r->vhost);
    home = _srs_config->get_dash_path(r->vhost);
    mpd_file = _srs_config->get_dash_mpd_file(r->vhost);
    
    string mpd_path = srs_path_build_stream(mpd_file, req->vhost, req->app, req->stream);
    fragment_home = srs_path_dirname(mpd_path) + "/" + req->stream;
    
    srs_trace("DASH: Config fragment=%d, period=%d", fragment, update_period);
    
    return ret;
}

int SrsMpdWriter::write(SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    // MPD is not expired?
    if (last_update_mpd != -1 && srs_get_system_time_ms() - last_update_mpd < update_period) {
        return ret;
    }
    last_update_mpd = srs_get_system_time_ms();
    
    string mpd_path = srs_path_build_stream(mpd_file, req->vhost, req->app, req->stream);
    string full_path = home + "/" + mpd_path;
    string full_home = srs_path_dirname(full_path);
    
    fragment_home = srs_path_dirname(mpd_path) + "/" + req->stream;
    
    if ((ret = srs_create_dir_recursively(full_home)) != ERROR_SUCCESS) {
        srs_error("DASH: Create MPD home failed, home=%s, ret=%d", full_home.c_str(), ret);
        return ret;
    }
    
    stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>" << endl
    << "<MPD profiles=\"urn:mpeg:dash:profile:isoff-live:2011,http://dashif.org/guidelines/dash-if-simple\" " << endl
    << "    ns1:schemaLocation=\"urn:mpeg:dash:schema:mpd:2011 DASH-MPD.xsd\" " << endl
    << "    xmlns=\"urn:mpeg:dash:schema:mpd:2011\" xmlns:ns1=\"http://www.w3.org/2001/XMLSchema-instance\" " << endl
    << "    type=\"dynamic\" minimumUpdatePeriod=\"PT" << update_period / 1000 << "S\" " << endl
    << "    timeShiftBufferDepth=\"PT" << timeshit / 1000 << "S\" availabilityStartTime=\"1970-01-01T00:00:00Z\" " << endl
    << "    maxSegmentDuration=\"PT" << fragment / 1000 << "S\" minBufferTime=\"PT" << fragment / 1000 << "S\" >" << endl
    << "    <BaseURL>" << req->stream << "/" << "</BaseURL>" << endl
    << "    <Period start=\"PT0S\">" << endl;
    if (format->acodec) {
        ss  << "        <AdaptationSet mimeType=\"audio/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">" << endl;
        ss  << "            <SegmentTemplate duration=\"" << fragment / 1000 << "\" "
        << "initialization=\"$RepresentationID$-init.mp4\" "
        << "media=\"$RepresentationID$-$Number$.m4s\" />" << endl;
        ss  << "            <Representation id=\"audio\" bandwidth=\"48000\" codecs=\"mp4a.40.2\" />" << endl;
        ss  << "        </AdaptationSet>" << endl;
    }
    if (format->vcodec) {
        int w = format->vcodec->width;
        int h = format->vcodec->height;
        ss  << "        <AdaptationSet mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">" << endl;
        ss  << "            <SegmentTemplate duration=\"" << fragment / 1000 << "\" "
        << "initialization=\"$RepresentationID$-init.mp4\" "
        << "media=\"$RepresentationID$-$Number$.m4s\" />" << endl;
        ss  << "            <Representation id=\"video\" bandwidth=\"800000\" codecs=\"avc1.64001e\" "
        << "width=\"" << w << "\" height=\"" << h << "\"/>" << endl;
        ss  << "        </AdaptationSet>" << endl;
    }
    ss  << "    </Period>" << endl
    << "</MPD>" << endl;
    
    SrsFileWriter* fw = new SrsFileWriter();
    SrsAutoFree(SrsFileWriter, fw);
    
    string full_path_tmp = full_path + ".tmp";
    if ((ret = fw->open(full_path_tmp)) != ERROR_SUCCESS) {
        srs_error("DASH: Open MPD file=%s failed, ret=%d", full_path_tmp.c_str(), ret);
        return ret;
    }
    
    string content = ss.str();
    if ((ret = fw->write((void*)content.data(), content.length(), NULL)) != ERROR_SUCCESS) {
        srs_error("DASH: Write MPD file=%s failed, ret=%d", full_path.c_str(), ret);
        return ret;
    }
    
    if (::rename(full_path_tmp.c_str(), full_path.c_str()) < 0) {
        ret = ERROR_DASH_WRITE_FAILED;
        srs_error("DASH: Rename %s to %s failed, ret=%d", full_path_tmp.c_str(), full_path.c_str(), ret);
        return ret;
    }
    
    srs_trace("DASH: Refresh MPD success, size=%dB, file=%s", content.length(), full_path.c_str());
    
    return ret;
}

int SrsMpdWriter::get_fragment(bool video, std::string& home, std::string& file_name, int64_t& sn, uint64_t& basetime)
{
    int ret = ERROR_SUCCESS;
    
    home = fragment_home;
    
    sn = srs_update_system_time_ms() / fragment;
    basetime = sn * fragment;
    
    if (video) {
        file_name = "video-" + srs_int2str(sn) + ".m4s";
    } else {
        file_name = "audio-" + srs_int2str(sn) + ".m4s";
    }
    
    return ret;
}

SrsDashController::SrsDashController()
{
    req = NULL;
    video_tack_id = 2;
    audio_track_id = 1;
    mpd = new SrsMpdWriter();
    vcurrent = acurrent = NULL;
    vfragments = new SrsFragmentWindow();
    afragments = new SrsFragmentWindow();
    audio_dts = video_dts = 0;
}

SrsDashController::~SrsDashController()
{
    srs_freep(mpd);
    srs_freep(vcurrent);
    srs_freep(acurrent);
    srs_freep(vfragments);
    srs_freep(afragments);
}

int SrsDashController::initialize(SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    req = r;
    fragment = _srs_config->get_dash_fragment(r->vhost);
    home = _srs_config->get_dash_path(r->vhost);
    
    if ((ret = mpd->initialize(r)) != ERROR_SUCCESS) {
        return ret;
    }
    
    string home, path;
    
    srs_freep(vcurrent);
    vcurrent = new SrsFragmentedMp4();
    if ((ret = vcurrent->initialize(req, true, mpd, video_tack_id)) != ERROR_SUCCESS) {
        srs_error("DASH: Initialize the video fragment failed, ret=%d", ret);
        return ret;
    }
    
    srs_freep(acurrent);
    acurrent = new SrsFragmentedMp4();
    if ((ret = acurrent->initialize(req, false, mpd, audio_track_id)) != ERROR_SUCCESS) {
        srs_error("DASH: Initialize the audio fragment failed, ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsDashController::on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (format->is_aac_sequence_header()) {
        return refresh_init_mp4(shared_audio, format);
    }
    
    if (acurrent->duration() >= fragment) {
        if ((ret = acurrent->reap(audio_dts)) != ERROR_SUCCESS) {
            return ret;
        }
        
        afragments->append(acurrent);
        acurrent = new SrsFragmentedMp4();
        
        if ((ret = acurrent->initialize(req, false, mpd, audio_track_id)) != ERROR_SUCCESS) {
            srs_error("DASH: Initialize the audio fragment failed, ret=%d", ret);
            return ret;
        }
    }
    
    if ((ret = acurrent->write(shared_audio, format)) != ERROR_SUCCESS) {
        srs_error("DASH: Write audio to fragment failed, ret=%d", ret);
        return ret;
    }
    
    if ((ret = refresh_mpd(format)) != ERROR_SUCCESS) {
        srs_error("DASH: Refresh the MPD failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsDashController::on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (format->is_avc_sequence_header()) {
        return refresh_init_mp4(shared_video, format);
    }
    
    bool reopen = format->video->frame_type == SrsVideoAvcFrameTypeKeyFrame
    && vcurrent->duration() >= fragment;
    if (reopen) {
        if ((ret = vcurrent->reap(video_dts)) != ERROR_SUCCESS) {
            return ret;
        }
        
        vfragments->append(vcurrent);
        vcurrent = new SrsFragmentedMp4();
        
        if ((ret = vcurrent->initialize(req, true, mpd, video_tack_id)) != ERROR_SUCCESS) {
            srs_error("DASH: Initialize the video fragment failed, ret=%d", ret);
            return ret;
        }
    }
    
    if ((ret = vcurrent->write(shared_video, format)) != ERROR_SUCCESS) {
        srs_error("DASH: Write video to fragment failed, ret=%d", ret);
        return ret;
    }
    
    if ((ret = refresh_mpd(format)) != ERROR_SUCCESS) {
        srs_error("DASH: Refresh the MPD failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsDashController::refresh_mpd(SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    // TODO: FIXME: Support pure audio streaming.
    if (!format->acodec || !format->vcodec) {
        return ret;
    }
    
    if ((ret = mpd->write(format)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDashController::refresh_init_mp4(SrsSharedPtrMessage* msg, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (msg->size <= 0 || (msg->is_video() && !format->vcodec->is_avc_codec_ok())
        || (msg->is_audio() && !format->acodec->is_aac_codec_ok())) {
        srs_warn("DASH: Ignore empty sequence header.");
        return ret;
    }
    
    string full_home = home + "/" + req->app + "/" + req->stream;
    if ((ret = srs_create_dir_recursively(full_home)) != ERROR_SUCCESS) {
        srs_error("DASH: Create media home failed, home=%s, ret=%d", full_home.c_str(), ret);
        return ret;
    }
    
    std::string path = full_home;
    if (msg->is_video()) {
        path += "/video-init.mp4";
    } else {
        path += "/audio-init.mp4";
    }
    
    SrsInitMp4* init_mp4 = new SrsInitMp4();
    SrsAutoFree(SrsInitMp4, init_mp4);
    
    init_mp4->set_path(path);
    
    int tid = msg->is_video()? video_tack_id:audio_track_id;
    if ((ret = init_mp4->write(format, msg->is_video(), tid)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = init_mp4->rename()) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_trace("DASH: Refresh media success, file=%s", path.c_str());
    
    return ret;
}

SrsDash::SrsDash()
{
    hub = NULL;
    req = NULL;
    controller = new SrsDashController();
    
    enabled = false;
}

SrsDash::~SrsDash()
{
    srs_freep(controller);
}

int SrsDash::initialize(SrsOriginHub* h, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    hub = h;
    req = r;
    
    if ((ret = controller->initialize(req)) != ERROR_SUCCESS) {
        srs_error("DASH: Initialize controller failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsDash::on_publish()
{
    int ret = ERROR_SUCCESS;
    
    // Prevent duplicated publish.
    if (enabled) {
        return ret;
    }
    
    if (!_srs_config->get_dash_enabled(req->vhost)) {
        return ret;
    }
    enabled = true;
    
    return ret;
}

int SrsDash::on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (!enabled) {
        return ret;
    }
    
    if ((ret = controller->on_audio(shared_audio, format)) != ERROR_SUCCESS) {
        srs_error("DASH: Consume audio failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsDash::on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (!enabled) {
        return ret;
    }
    
    if ((ret = controller->on_video(shared_video, format)) != ERROR_SUCCESS) {
        srs_error("DASH: Consume video failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

void SrsDash::on_unpublish()
{
    // Prevent duplicated unpublish.
    if (!enabled) {
        return;
    }
    
    enabled = false;
}

