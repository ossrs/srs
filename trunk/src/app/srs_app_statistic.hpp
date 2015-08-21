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

#ifndef SRS_APP_STATISTIC_HPP
#define SRS_APP_STATISTIC_HPP

/*
#include <srs_app_statistic.hpp>
*/

#include <srs_core.hpp>

#include <map>
#include <string>

#include <srs_kernel_codec.hpp>

#define STATISTIC_STREAM_STATUS_PUBLISHING    "publishing"
#define STATISTIC_STREAM_STATUS_IDLING        "idling"

class SrsKbps;
class SrsRequest;
class SrsConnection;

struct SrsStatisticVhost
{
public:
    int64_t id;
    std::string vhost;
    int nb_clients;
public:
    /**
    * vhost total kbps.
    */
    SrsKbps* kbps;
public:
    SrsStatisticVhost();
    virtual ~SrsStatisticVhost();
public:
    virtual int dumps(std::stringstream& ss);
};

struct SrsStatisticStream
{
public:
    int64_t id;
    SrsStatisticVhost* vhost;
    std::string app;
    std::string stream;
    std::string url;
    std::string status;
    int nb_clients;
public:
    /**
    * stream total kbps.
    */
    SrsKbps* kbps;
public:
    bool has_video;
    SrsCodecVideo vcodec;
    // profile_idc, H.264-AVC-ISO_IEC_14496-10.pdf, page 45.
    SrsAvcProfile avc_profile;
    // level_idc, H.264-AVC-ISO_IEC_14496-10.pdf, page 45.
    SrsAvcLevel avc_level;
public:
    bool has_audio;
    SrsCodecAudio acodec;
    SrsCodecAudioSampleRate asample_rate;
    SrsCodecAudioSoundType asound_type;
    /**
    * audio specified
    * audioObjectType, in 1.6.2.1 AudioSpecificConfig, page 33,
    * 1.5.1.1 Audio object type definition, page 23,
    *           in aac-mp4a-format-ISO_IEC_14496-3+2001.pdf.
    */
    SrsAacObjectType aac_object;
public:
    SrsStatisticStream();
    virtual ~SrsStatisticStream();
public:
    virtual int dumps(std::stringstream& ss);
public:
    /**
    * publish the stream.
    */
    virtual void publish();
    /**
    * close the stream.
    */
    virtual void close();
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
    // key: stream url, value: stream object.
    std::map<std::string, SrsStatisticStream*> streams;
    // key: client id, value: stream object.
    std::map<int, SrsStatisticClient*> clients;
    // server total kbps.
    SrsKbps* kbps;
private:
    SrsStatistic();
    virtual ~SrsStatistic();
public:
    static SrsStatistic* instance();
public:
    virtual SrsStatisticStream* find_stream(int stream_id);
    /**
    * when got video info for stream.
    */
    virtual int on_video_info(SrsRequest* req, 
        SrsCodecVideo vcodec, SrsAvcProfile avc_profile, SrsAvcLevel avc_level
    );
    /**
    * when got audio info for stream.
    */
    virtual int on_audio_info(SrsRequest* req,
        SrsCodecAudio acodec, SrsCodecAudioSampleRate asample_rate, SrsCodecAudioSoundType asound_type,
        SrsAacObjectType aac_object
    );
    /**
    * when publish stream.
    */
    virtual void on_stream_publish(SrsRequest* req);
    /**
    * when close stream.
    */
    virtual void on_stream_close(SrsRequest* req);
public:
    /**
     * when got a client to publish/play stream,
     * @param id, the client srs id.
     * @param req, the client request object.
     */
    virtual int on_client(int id, SrsRequest* req);
    /**
     * client disconnect
     * @remark the on_disconnect always call, while the on_client is call when
     *      only got the request object, so the client specified by id maybe not
     *      exists in stat.
     */
    virtual void on_disconnect(int id);
    /**
    * sample the kbps, add delta bytes of conn.
    * use kbps_sample() to get all result of kbps stat.
    */
    // TODO: FIXME: the add delta must use IKbpsDelta interface instead.
    virtual void kbps_add_delta(SrsConnection* conn);
    /**
    * calc the result for all kbps.
    * @return the server kbps.
    */
    virtual SrsKbps* kbps_sample();
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
private:
    virtual SrsStatisticVhost* create_vhost(SrsRequest* req);
    virtual SrsStatisticStream* create_stream(SrsStatisticVhost* vhost, SrsRequest* req);
};

#endif
