/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

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

#include <srs_app_dash.hpp>

#include <srs_kernel_error.hpp>
#include <srs_app_source.hpp>
#include <srs_app_config.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_file.hpp>

#include <sstream>
using namespace std;

SrsFragmentedMp4::SrsFragmentedMp4()
{
}

SrsFragmentedMp4::~SrsFragmentedMp4()
{
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
    
    if ((ret = srs_create_dir_recursively(full_home)) != ERROR_SUCCESS) {
        srs_error("DASH: create MPD home failed, home=%s, ret=%d", full_home.c_str(), ret);
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
    
    SrsFileWriter fw;
    string full_path_tmp = full_path + ".tmp";
    if ((ret = fw.open(full_path_tmp)) != ERROR_SUCCESS) {
        srs_error("DASH: open MPD file=%s failed, ret=%d", full_path_tmp.c_str(), ret);
        return ret;
    }
    
    string content = ss.str();
    if ((ret = fw.write((void*)content.data(), content.length(), NULL)) != ERROR_SUCCESS) {
        srs_error("DASH: write MPD file=%s failed, ret=%d", full_path.c_str(), ret);
        return ret;
    }
    
    if (::rename(full_path_tmp.c_str(), full_path.c_str()) < 0) {
        ret = ERROR_DASH_WRITE_FAILED;
        srs_error("DASH: rename %s to %s failed, ret=%d", full_path_tmp.c_str(), full_path.c_str(), ret);
        return ret;
    }
    
    srs_trace("DASH: refresh MPD successed, size=%dB, file=%s", content.length(), full_path.c_str());
    
    return ret;
}

SrsDashController::SrsDashController()
{
    mpd = new SrsMpdWriter();
}

SrsDashController::~SrsDashController()
{
    srs_freep(mpd);
}

int SrsDashController::initialize(SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = mpd->initialize(r)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDashController::on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = refresh_mpd(format)) != ERROR_SUCCESS) {
        srs_error("DASH: refresh the MPD failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsDashController::on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = refresh_mpd(format)) != ERROR_SUCCESS) {
        srs_error("DASH: refresh the MPD failed. ret=%d", ret);
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
        srs_error("DASH: initialize controller failed. ret=%d", ret);
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
        srs_error("DASH: consume audio failed. ret=%d", ret);
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
        srs_error("DASH: consume video failed. ret=%d", ret);
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

