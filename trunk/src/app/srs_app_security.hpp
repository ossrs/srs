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

#ifndef SRS_APP_SECURITY_HPP
#define SRS_APP_SECURITY_HPP

/*
#include <srs_app_security.hpp>
*/

#include <srs_core.hpp>

#include <string>

#include <srs_protocol_rtmp.hpp>

class SrsConfDirective;

/**
* the security apply on vhost.
* @see https://github.com/winlinvip/simple-rtmp-server/issues/211
*/
class SrsSecurity
{
public:
    SrsSecurity();
    virtual ~SrsSecurity();
public:
    /**
    * security check the client apply by vhost security strategy
    * @param type the client type, publish or play.
    * @param ip the ip address of client.
    * @param req the request object of client.
    */
    virtual int check(SrsRtmpConnType type, std::string ip, SrsRequest* req);
private:
    /**
    * security check the allow,
    * @return, if allowed, ERROR_SYSTEM_SECURITY_ALLOW.
    */
    virtual int allow_check(SrsConfDirective* rules, SrsRtmpConnType type, std::string ip, SrsRequest* req);
    /**
    * security check the deny,
    * @return, if allowed, ERROR_SYSTEM_SECURITY_DENY.
    */
    virtual int deny_check(SrsConfDirective* rules, SrsRtmpConnType type, std::string ip, SrsRequest* req);
};

#endif

