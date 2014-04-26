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

#ifndef SRS_APP_EDGE_HPP
#define SRS_APP_EDGE_HPP

/*
#include <srs_app_edge.hpp>
*/

#include <srs_core.hpp>

#include <srs_app_thread.hpp>

class SrsEdge;
class SrsRequest;

/**
* the state of edge
*/
enum SrsEdgeState
{
    SrsEdgeStateInit = 0,
    SrsEdgeStatePlay = 100,
    SrsEdgeStatePublish,
    SrsEdgeStateConnected,
    SrsEdgeStateAborting,
    SrsEdgeStateReloading,
};

/**
* edge used to ingest stream from origin.
*/
class SrsEdgeIngester : public ISrsThreadHandler
{
private:
    SrsEdge* _edge;
    SrsRequest* _req;
    SrsThread* pthread;
public:
    SrsEdgeIngester();
    virtual ~SrsEdgeIngester();
public:
    virtual int initialize(SrsEdge* edge, SrsRequest* req);
    virtual int start();
// interface ISrsThreadHandler
public:
    virtual int cycle();
};

/**
* edge control service.
*/
class SrsEdge
{
private:
    SrsEdgeState state;
    SrsEdgeIngester* ingester;
public:
    SrsEdge();
    virtual ~SrsEdge();
public:
    virtual int initialize(SrsRequest* req);
    /**
    * when client play stream on edge.
    */
    virtual int on_client_play();
};

#endif
