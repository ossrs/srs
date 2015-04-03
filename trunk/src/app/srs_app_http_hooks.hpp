/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#ifndef SRS_APP_HTTP_HOOKS_HPP
#define SRS_APP_HTTP_HOOKS_HPP

/*
#include <srs_app_http_hooks.hpp>
*/
#include <srs_core.hpp>

#include <string>

#ifdef SRS_AUTO_HTTP_CALLBACK

#include <http_parser.h>

class SrsHttpUri;
class SrsStSocket;
class SrsRequest;
class SrsHttpParser;
class SrsFlvSegment;

/**
* the http hooks, http callback api,
* for some event, such as on_connect, call
* a http api(hooks).
*/
class SrsHttpHooks
{
private:
    SrsHttpHooks();
public:
    virtual ~SrsHttpHooks();
public:
    /**
    * on_connect hook, when client connect to srs.
    * @param url the api server url, to valid the client. 
    *         ignore if empty.
    */
    static int on_connect(std::string url, SrsRequest* req);
    /**
    * on_close hook, when client disconnect to srs, where client is valid by on_connect.
    * @param url the api server url, to process the event. 
    *         ignore if empty.
    */
    static void on_close(std::string url, SrsRequest* req, int64_t send_bytes, int64_t recv_bytes);
    /**
    * on_publish hook, when client(encoder) start to publish stream
    * @param url the api server url, to valid the client. 
    *         ignore if empty.
    */
    static int on_publish(std::string url, SrsRequest* req);
    /**
    * on_unpublish hook, when client(encoder) stop publish stream.
    * @param url the api server url, to process the event. 
    *         ignore if empty.
    */
    static void on_unpublish(std::string url, SrsRequest* req);
    /**
    * on_play hook, when client start to play stream.
    * @param url the api server url, to valid the client. 
    *         ignore if empty.
    */
    static int on_play(std::string url, SrsRequest* req);
    /**
    * on_stop hook, when client stop to play the stream.
    * @param url the api server url, to process the event. 
    *         ignore if empty.
    */
    static void on_stop(std::string url, SrsRequest* req);
    /**
    * on_dvr hook, when reap a dvr file.
    * @param url the api server url, to process the event. 
    *         ignore if empty.
    * @param file the file path, can be relative or absolute path.
    */
    static int on_dvr(std::string url, SrsRequest* req, std::string file);
    /**
    * when hls reap segment, callback.
    * @param url the api server url, to process the event. 
    *         ignore if empty.
    * @param file the ts file path, can be relative or absolute path.
    * @param sn the seq_no, the sequence number of ts in hls/m3u8.
    * @param duration the segment duration in seconds.
    */
    static int on_hls(std::string url, SrsRequest* req, std::string file, int sn, double duration);
private:
    static int do_post(std::string url, std::string req, int& code, std::string& res);
};

#endif

#endif

