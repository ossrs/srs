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

SrsMpegDash::SrsMpegDash()
{
    hub = NULL;
    req = NULL;
    
    enabled = false;
}

SrsMpegDash::~SrsMpegDash()
{
}

int SrsMpegDash::initialize(SrsOriginHub* h, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    hub = h;
    req = r;
    
    return ret;
}

int SrsMpegDash::on_publish()
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

int SrsMpegDash::on_audio(SrsSharedPtrMessage* shared_audio)
{
    int ret = ERROR_SUCCESS;
    
    if (!enabled) {
        return ret;
    }
    
    return ret;
}

int SrsMpegDash::on_video(SrsSharedPtrMessage* shared_video, bool is_sequence_header)
{
    int ret = ERROR_SUCCESS;
    
    if (!enabled) {
        return ret;
    }
    
    return ret;
}

void SrsMpegDash::on_unpublish()
{
    // Prevent duplicated unpublish.
    if (!enabled) {
        return;
    }
    
    enabled = false;
}

