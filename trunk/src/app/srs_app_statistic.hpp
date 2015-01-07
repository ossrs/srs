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

#ifndef SRS_APP_STATISTIC_HPP
#define SRS_APP_STATISTIC_HPP

/*
#include <srs_app_statistic.hpp>
*/

#include <srs_core.hpp>

#include <map>
#include <string>

class SrsRequest;
struct SrsStatisticClient;

struct SrsStatisticVhost
{
public:
    int64_t id;
    std::string vhost;
public:
    SrsStatisticVhost();
    virtual ~SrsStatisticVhost();
};

struct SrsStatisticStream
{
public:
    int64_t id;
    SrsStatisticVhost* vhost;
    std::string app;
    std::string stream;
    std::string url;
    std::map<int, SrsStatisticClient*> clients;
public:
    SrsStatisticStream();
    virtual ~SrsStatisticStream();
};

struct SrsStatisticClient
{
public:
    SrsStatisticStream* stream;
    int id;
};

class SrsStatistic
{
private:
    static SrsStatistic *_instance;
    // the id to identify the sever.
    int64_t _server_id;
    // key: vhost name, value: vhost object.
    std::map<std::string, SrsStatisticVhost*> vhosts;
    // key: stream name, value: stream object.
    std::map<std::string, SrsStatisticStream*> streams;
    // key: client id, value: stream object.
    std::map<int, SrsStatisticClient*> clients;
private:
    SrsStatistic();
    virtual ~SrsStatistic();
public:
    static SrsStatistic* instance();
public:
    /**
    * when got a client to publish/play stream,
    * @param id, the client srs id.
    * @param req, the client request object.
    */
    virtual int on_client(int id, SrsRequest* req);
    /**
    * client close
    */
    virtual int on_close(int id);
public:
    /**
    * get the server id, used to identify the server.
    * for example, when restart, the server id must changed.
    */
    virtual int64_t server_id();
    /**
    * dumps the vhosts to sstream in json.
    */
    virtual int dumps_vhosts(std::stringstream& ss);
    /**
    * dumps the streams to sstream in json.
    */
    virtual int dumps_streams(std::stringstream& ss);
};

#endif
