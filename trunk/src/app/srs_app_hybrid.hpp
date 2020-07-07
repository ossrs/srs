/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

#ifndef SRS_APP_HYBRID_HPP
#define SRS_APP_HYBRID_HPP

#include <srs_core.hpp>

#include <vector>

class SrsServer;

// The hibrid server interfaces, we could register many servers.
class ISrsHybridServer
{
public:
    ISrsHybridServer();
    virtual ~ISrsHybridServer();
public:
    // Only ST initialized before each server, we could fork processes as such.
    virtual srs_error_t initialize() = 0;
    // Run each server, should never block except the SRS master server.
    virtual srs_error_t run() = 0;
    // Stop each server, should do cleanup, for example, kill processes forked by server.
    virtual void stop() = 0;
};

// The SRS server adapter, the master server.
class SrsServerAdapter : public ISrsHybridServer
{
private:
    SrsServer* srs;
public:
    SrsServerAdapter();
    virtual ~SrsServerAdapter();
public:
    virtual srs_error_t initialize();
    virtual srs_error_t run();
    virtual void stop();
public:
    virtual SrsServer* instance();
};

// The hybrid server manager.
class SrsHybridServer
{
private:
    std::vector<ISrsHybridServer*> servers;
public:
    SrsHybridServer();
    virtual ~SrsHybridServer();
public:
    virtual void register_server(ISrsHybridServer* svr);
public:
    virtual srs_error_t initialize();
    virtual srs_error_t run();
    virtual void stop();
public:
    virtual SrsServerAdapter* srs();
};

extern SrsHybridServer* _srs_hybrid;

#endif
