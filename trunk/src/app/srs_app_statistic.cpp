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

#include <srs_app_statistic.hpp>

#include <unistd.h>
#include <sstream>
using namespace std;

#include <srs_protocol_rtmp.hpp>
#include <srs_app_json.hpp>

int64_t __srs_gvid = getpid();

int64_t __srs_generate_id()
{
    return __srs_gvid++;
}

SrsStatisticVhost::SrsStatisticVhost()
{
    id = __srs_generate_id();
}

SrsStatisticVhost::~SrsStatisticVhost()
{
}

SrsStatisticStream::SrsStatisticStream()
{
    id = __srs_generate_id();
    vhost = NULL;
    clients = 0;
}

SrsStatisticStream::~SrsStatisticStream()
{
}

SrsStatistic* SrsStatistic::_instance = new SrsStatistic();

SrsStatistic::SrsStatistic()
{
    _server_id = __srs_generate_id();
}

SrsStatistic::~SrsStatistic()
{
    if (true) {
        std::map<std::string, SrsStatisticVhost*>::iterator it;
        for (it = vhosts.begin(); it != vhosts.end(); it++) {
            SrsStatisticVhost* vhost = it->second;
            srs_freep(vhost);
        }
    }
    if (true) {
        std::map<std::string, SrsStatisticStream*>::iterator it;
        for (it = streams.begin(); it != streams.end(); it++) {
            SrsStatisticStream* stream = it->second;
            srs_freep(stream);
        }
    }
    if (true) {
        std::map<int, SrsStatisticClient*>::iterator it;
        for (it = clients.begin(); it != clients.end(); it++) {
            SrsStatisticClient* client = it->second;
            srs_freep(client);
        }
    }
}

SrsStatistic* SrsStatistic::instance()
{
    return _instance;
}

int SrsStatistic::on_client(int id, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    // create vhost if not exists.
    SrsStatisticVhost* vhost = NULL;
    if (vhosts.find(req->vhost) == vhosts.end()) {
        vhost = new SrsStatisticVhost();
        vhost->vhost = req->vhost;
        vhosts[req->vhost] = vhost;
    } else {
        vhost = vhosts[req->vhost];
    }
    
    // the url to identify the stream.
    std::string url = req->get_stream_url();
    
    // create stream if not exists.
    SrsStatisticStream* stream = NULL;
    if (streams.find(url) == streams.end()) {
        stream = new SrsStatisticStream();
        stream->vhost = vhost;
        stream->stream = req->stream;
        stream->url = url;
        streams[url] = stream;
    } else {
        stream = streams[url];
    }

    // create client if not exists
    SrsStatisticClient* client = NULL;
    if (clients.find(id) == clients.end()) {
        client = new SrsStatisticClient();
        client->stream = stream;
        clients[id] = client;
    } else {
        client = clients[id];
    }
    
    return ret;
}

int SrsStatistic::on_client_play_start(int id)
{
    int ret = ERROR_SUCCESS;

    std::map<int, SrsStatisticClient*>::iterator it;
    it = clients.find(id);
    if (it != clients.end()) {
        it->second->stream->clients++;
    }

    return ret;
}

int SrsStatistic::on_client_play_stop(int id)
{
    int ret = ERROR_SUCCESS;

    std::map<int, SrsStatisticClient*>::iterator it;
    it = clients.find(id);
    if (it != clients.end()) {
        it->second->stream->clients--;
    }

    return ret;
}

int64_t SrsStatistic::server_id()
{
    return _server_id;
}

int SrsStatistic::dumps_vhosts(stringstream& ss)
{
    int ret = ERROR_SUCCESS;

    ss << __SRS_JARRAY_START;
    bool first = true;
    std::map<std::string, SrsStatisticVhost*>::iterator it;
    for (it = vhosts.begin(); it != vhosts.end(); it++) {
        SrsStatisticVhost* vhost = it->second;
        if (first) {
            first = false;
        } else {
            ss << __SRS_JFIELD_CONT;
        }

        ss << __SRS_JOBJECT_START
                << __SRS_JFIELD_ORG("id", vhost->id) << __SRS_JFIELD_CONT
                << __SRS_JFIELD_STR("name", vhost->vhost)
            << __SRS_JOBJECT_END;
    }
    ss << __SRS_JARRAY_END;

    return ret;
}

int SrsStatistic::dumps_streams(stringstream& ss)
{
    int ret = ERROR_SUCCESS;
    
    ss << __SRS_JARRAY_START;
    bool first = true;
    std::map<std::string, SrsStatisticStream*>::iterator it;
    for (it = streams.begin(); it != streams.end(); it++) {
        SrsStatisticStream* stream = it->second;
        if (first) {
            first = false;
        } else {
            ss << __SRS_JFIELD_CONT;
        }

        ss << __SRS_JOBJECT_START
                << __SRS_JFIELD_ORG("id", stream->id) << __SRS_JFIELD_CONT
                << __SRS_JFIELD_STR("name", stream->stream) << __SRS_JFIELD_CONT
                << __SRS_JFIELD_ORG("vhost", stream->vhost->id) << __SRS_JFIELD_CONT
                << __SRS_JFIELD_ORG("clients", stream->clients)
            << __SRS_JOBJECT_END;
    }
    ss << __SRS_JARRAY_END;
    
    return ret;
}
