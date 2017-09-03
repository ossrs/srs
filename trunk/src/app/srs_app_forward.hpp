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

#ifndef SRS_APP_FORWARD_HPP
#define SRS_APP_FORWARD_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>

class ISrsProtocolReaderWriter;
class SrsSharedPtrMessage;
class SrsOnMetaDataPacket;
class SrsMessageQueue;
class SrsRtmpJitter;
class SrsRtmpClient;
class SrsRequest;
class SrsSource;
class SrsOriginHub;
class SrsKbps;
class SrsSimpleRtmpClient;

/**
 * forward the stream to other servers.
 */
// TODO: FIXME: refine the error log, comments it.
class SrsForwarder : public ISrsCoroutineHandler
{
private:
    // the ep to forward, server[:port].
    std::string ep_forward;
    SrsRequest* req;
private:
    SrsCoroutine* trd;
private:
    SrsOriginHub* hub;
    SrsSimpleRtmpClient* sdk;
    SrsRtmpJitter* jitter;
    SrsMessageQueue* queue;
    /**
     * cache the sequence header for retry when slave is failed.
     * @see https://github.com/ossrs/srs/issues/150
     */
    SrsSharedPtrMessage* sh_audio;
    SrsSharedPtrMessage* sh_video;
public:
    SrsForwarder(SrsOriginHub* h);
    virtual ~SrsForwarder();
public:
    virtual int initialize(SrsRequest* r, std::string ep);
    virtual void set_queue_size(double queue_size);
public:
    virtual int on_publish();
    virtual void on_unpublish();
    /**
     * forward the audio packet.
     * @param shared_metadata, directly ptr, copy it if need to save it.
     */
    virtual int on_meta_data(SrsSharedPtrMessage* shared_metadata);
    /**
     * forward the audio packet.
     * @param shared_audio, directly ptr, copy it if need to save it.
     */
    virtual int on_audio(SrsSharedPtrMessage* shared_audio);
    /**
     * forward the video packet.
     * @param shared_video, directly ptr, copy it if need to save it.
     */
    virtual int on_video(SrsSharedPtrMessage* shared_video);
// interface ISrsReusableThread2Handler.
public:
    virtual srs_error_t cycle();
private:
    virtual srs_error_t do_cycle();
private:
    virtual int forward();
};

#endif

