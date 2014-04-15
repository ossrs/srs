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

#ifndef SRS_APP_HTTP_CONN_HPP
#define SRS_APP_HTTP_CONN_HPP

/*
#include <srs_app_http_conn.hpp>
*/

#include <srs_core.hpp>

#ifdef SRS_AUTO_HTTP_SERVER

#include <srs_app_st.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_http.hpp>

class SrsSocket;
class SrsHttpParser;
class SrsHttpMessage;
class SrsHttpHandler;

// for http root.
class SrsHttpRoot : public SrsHttpHandler
{
public:
    SrsHttpRoot();
    virtual ~SrsHttpRoot();
public:
    virtual int initialize();
    virtual int best_match(const char* path, int length, SrsHttpHandlerMatch** ppmatch);
protected:
    virtual bool is_handler_valid(SrsHttpMessage* req, int& status_code, std::string& reason_phrase);
    virtual int do_process_request(SrsSocket* skt, SrsHttpMessage* req);
};

class SrsHttpVhost : public SrsHttpHandler
{
private:
    std::string _vhost;
    std::string _mount;
    std::string _dir;
public:
    SrsHttpVhost(std::string vhost, std::string mount, std::string dir);
    virtual ~SrsHttpVhost();
public:
    virtual bool can_handle(const char* path, int length, const char** pchild);
protected:
    virtual bool is_handler_valid(SrsHttpMessage* req, int& status_code, std::string& reason_phrase);
    virtual int do_process_request(SrsSocket* skt, SrsHttpMessage* req);
private:
    virtual std::string get_request_file(SrsHttpMessage* req);
public:
    virtual std::string vhost();
    virtual std::string mount();
    virtual std::string dir();
};

class SrsHttpConn : public SrsConnection
{
private:
    SrsHttpParser* parser;
    SrsHttpHandler* handler;
    bool requires_crossdomain;
public:
    SrsHttpConn(SrsServer* srs_server, st_netfd_t client_stfd, SrsHttpHandler* _handler);
    virtual ~SrsHttpConn();
protected:
    virtual int do_cycle();
private:
    virtual int process_request(SrsSocket* skt, SrsHttpMessage* req);
};

#endif

#endif
