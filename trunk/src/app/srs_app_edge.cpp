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

#include <srs_app_edge.hpp>

#include <srs_kernel_error.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_kernel_log.hpp>

// when error, edge ingester sleep for a while and retry.
#define SRS_EDGE_INGESTER_SLEEP_US (int64_t)(3*1000*1000LL)

SrsEdgeIngester::SrsEdgeIngester()
{
    _edge = NULL;
    _req = NULL;
    pthread = new SrsThread(this, SRS_EDGE_INGESTER_SLEEP_US);
}

SrsEdgeIngester::~SrsEdgeIngester()
{
}

int SrsEdgeIngester::initialize(SrsEdge* edge, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    _edge = edge;
    _req = req;
    
    return ret;
}

int SrsEdgeIngester::start()
{
    int ret = ERROR_SUCCESS;
    return ret;
    //return pthread->start();
}

int SrsEdgeIngester::cycle()
{
    int ret = ERROR_SUCCESS;
    return ret;
}

SrsEdge::SrsEdge()
{
    state = SrsEdgeStateInit;
    ingester = new SrsEdgeIngester();
}

SrsEdge::~SrsEdge()
{
    srs_freep(ingester);
}

int SrsEdge::initialize(SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = ingester->initialize(this, req)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsEdge::on_client_play()
{
    int ret = ERROR_SUCCESS;
    
    // error state.
    if (state == SrsEdgeStateAborting || state == SrsEdgeStateReloading) {
        ret = ERROR_RTMP_EDGE_PLAY_STATE;
        srs_error("invalid state for client to play stream on edge. state=%d, ret=%d", state, ret);
        return ret;
    }
    
    // start ingest when init state.
    if (state == SrsEdgeStateInit) {
        state = SrsEdgeStatePlay;
        return ingester->start();
    }

    return ret;
}

