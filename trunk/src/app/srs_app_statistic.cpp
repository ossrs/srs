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

#include <srs_rtmp_sdk.hpp>
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
    
    has_video = false;
    vcodec = SrsCodecVideoReserved;
    avc_profile = 0;
    avc_level = 0;
    
    has_audio = false;
    acodec = SrsCodecAudioReserved1;
    asample_rate = SrsCodecAudioSampleRateReserved;
    asound_type = SrsCodecAudioSoundTypeReserved;
    aac_object = SrsAacObjectTypeReserved;
}

SrsStatisticStream::~SrsStatisticStream()
{
}

void SrsStatisticStream::close()
{
    has_video = false;
    has_audio = false;
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

int SrsStatistic::on_video_info(SrsRequest* req, 
    SrsCodecVideo vcodec, u_int8_t avc_profile, u_int8_t avc_level
) {
    int ret = ERROR_SUCCESS;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);

    stream->has_video = true;
    stream->vcodec = vcodec;
    stream->avc_profile = avc_profile;
    stream->avc_level = avc_level;
    
    return ret;
}

int SrsStatistic::on_audio_info(SrsRequest* req,
    SrsCodecAudio acodec, SrsCodecAudioSampleRate asample_rate, SrsCodecAudioSoundType asound_type,
    SrsAacObjectType aac_object
) {
    int ret = ERROR_SUCCESS;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);

    stream->has_audio = true;
    stream->acodec = acodec;
    stream->asample_rate = asample_rate;
    stream->asound_type = asound_type;
    stream->aac_object = aac_object;
    
    return ret;
}

void SrsStatistic::on_stream_close(SrsRequest* req)
{
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);

    stream->close();
}

int SrsStatistic::on_client(int id, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);

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

void SrsStatistic::on_disconnect(int id)
{
    std::map<int, SrsStatisticClient*>::iterator it;
    it = clients.find(id);
    if (it != clients.end()) {
        SrsStatisticClient* client = it->second;
        srs_freep(client);
        clients.erase(it);
    }
}

int64_t SrsStatistic::server_id()
{
    return _server_id;
}

int SrsStatistic::dumps_vhosts(stringstream& ss)
{
    int ret = ERROR_SUCCESS;

    ss << __SRS_JARRAY_START;
    std::map<std::string, SrsStatisticVhost*>::iterator it;
    for (it = vhosts.begin(); it != vhosts.end(); it++) {
        SrsStatisticVhost* vhost = it->second;
        if (it != vhosts.begin()) {
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
    std::map<std::string, SrsStatisticStream*>::iterator it;
    for (it = streams.begin(); it != streams.end(); it++) {
        SrsStatisticStream* stream = it->second;
        if (it != streams.begin()) {
            ss << __SRS_JFIELD_CONT;
        }

        int client_num = 0;
        std::map<int, SrsStatisticClient*>::iterator it_client;
        for (it_client = clients.begin(); it_client != clients.end(); it_client++) {
            SrsStatisticClient* client = it_client->second;
            if (client->stream == stream) {
                client_num++;
            }
        }

        ss << __SRS_JOBJECT_START
                << __SRS_JFIELD_ORG("id", stream->id) << __SRS_JFIELD_CONT
                << __SRS_JFIELD_STR("name", stream->stream) << __SRS_JFIELD_CONT
                << __SRS_JFIELD_ORG("vhost", stream->vhost->id) << __SRS_JFIELD_CONT
                << __SRS_JFIELD_ORG("clients", client_num) << __SRS_JFIELD_CONT;
                
        if (!stream->has_video) {
            ss  << __SRS_JFIELD_NULL("video") << __SRS_JFIELD_CONT;
        } else {
            ss  << __SRS_JFIELD_NAME("video")
                    << __SRS_JOBJECT_START
                        << __SRS_JFIELD_STR("codec", srs_codec_video2str(stream->vcodec)) << __SRS_JFIELD_CONT
                        << __SRS_JFIELD_ORG("profile", (int)stream->avc_profile) << __SRS_JFIELD_CONT
                        << __SRS_JFIELD_ORG("level", (int)stream->avc_level)
                    << __SRS_JOBJECT_END
                << __SRS_JFIELD_CONT;
        }
                
        if (!stream->has_audio) {
            ss  << __SRS_JFIELD_NULL("audio");
        } else {
            ss  << __SRS_JFIELD_NAME("audio")
                    << __SRS_JOBJECT_START
                        << __SRS_JFIELD_STR("codec", srs_codec_audio2str(stream->acodec)) << __SRS_JFIELD_CONT
                        << __SRS_JFIELD_ORG("sample_rate", (int)flv_sample_rates[stream->asample_rate]) << __SRS_JFIELD_CONT
                        << __SRS_JFIELD_ORG("channel", (int)stream->asound_type + 1) << __SRS_JFIELD_CONT
                        << __SRS_JFIELD_STR("profile", srs_codec_aac_object2str(stream->aac_object))
                    << __SRS_JOBJECT_END;
        }
        
        ss << __SRS_JOBJECT_END;
    }
    ss << __SRS_JARRAY_END;
    
    return ret;
}

SrsStatisticVhost* SrsStatistic::create_vhost(SrsRequest* req)
{
    SrsStatisticVhost* vhost = NULL;
    
    // create vhost if not exists.
    if (vhosts.find(req->vhost) == vhosts.end()) {
        vhost = new SrsStatisticVhost();
        vhost->vhost = req->vhost;
        vhosts[req->vhost] = vhost;
        return vhost;
    }

    vhost = vhosts[req->vhost];
    
    return vhost;
}

SrsStatisticStream* SrsStatistic::create_stream(SrsStatisticVhost* vhost, SrsRequest* req)
{
    std::string url = req->get_stream_url();
    
    SrsStatisticStream* stream = NULL;
    
    // create stream if not exists.
    if (streams.find(url) == streams.end()) {
        stream = new SrsStatisticStream();
        stream->vhost = vhost;
        stream->stream = req->stream;
        stream->url = url;
        streams[url] = stream;
        return stream;
    }
    
    stream = streams[url];
    
    return stream;
}

