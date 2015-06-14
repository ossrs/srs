/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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

#ifndef SRS_APP_HTTP_STATIC_HPP
#define SRS_APP_HTTP_STATIC_HPP

/*
#include <srs_app_http_static.hpp>
*/

#include <srs_core.hpp>

#include <srs_app_http_conn.hpp>

#ifdef SRS_AUTO_HTTP_SERVER

/**
 * the flv vod stream supports flv?start=offset-bytes.
 * for example, http://server/file.flv?start=10240
 * server will write flv header and sequence header,
 * then seek(10240) and response flv tag data.
 */
class SrsVodStream : public SrsHttpFileServer
{
public:
    SrsVodStream(std::string root_dir);
    virtual ~SrsVodStream();
protected:
    virtual int serve_flv_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath, int offset);
    virtual int serve_mp4_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath, int start, int end);
};

/**
* the http static server instance,
* serve http static file and flv/mp4 vod stream.
*/
class SrsHttpStaticServer : virtual public ISrsReloadHandler
{
private:
    SrsServer* server;
public:
    SrsHttpServeMux mux;
public:
    SrsHttpStaticServer(SrsServer* svr);
    virtual ~SrsHttpStaticServer();
public:
    virtual int initialize();
// interface ISrsReloadHandler.
public:
    virtual int on_reload_vhost_http_updated();
};

#endif

#endif

