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

#include <srs_app_recv_thread.hpp>

#include <srs_protocol_rtmp.hpp>
#include <srs_protocol_stack.hpp>

SrsRecvThread::SrsRecvThread(SrsRtmpServer* rtmp_sdk)
{
    rtmp = rtmp_sdk;
    trd = new SrsThread(this, 0, true);
}

SrsRecvThread::~SrsRecvThread()
{
    // stop recv thread.
    stop();
    
    // destroy the thread.
    srs_freep(trd);
    
    // clear all messages.
    std::vector<SrsMessage*>::iterator it;
    for (it = queue.begin(); it != queue.end(); ++it) {
        SrsMessage* msg = *it;
        srs_freep(msg);
    }
    queue.clear();
}

bool SrsRecvThread::empty()
{
    return queue.empty();
}

SrsMessage* SrsRecvThread::pump()
{
    srs_assert(!queue.empty());
    
    SrsMessage* msg = *queue.begin();
    
    queue.erase(queue.begin());
    
    return msg;
}

int SrsRecvThread::start()
{
    return trd->start();
}

void SrsRecvThread::stop()
{
    trd->stop();
}

int SrsRecvThread::cycle()
{
    int ret = ERROR_SUCCESS;
    
    SrsMessage* msg = NULL;
    
    if ((ret = rtmp->recv_message(&msg)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("recv client control message failed. ret=%d", ret);
        }
        
        // we use no timeout to recv, should never got any error.
        trd->stop_loop();
        
        return ret;
    }
    srs_verbose("play loop recv message. ret=%d", ret);
    
    return ret;
}

