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
#include <srs_rtmp_stack.hpp>

class SrsKbps;
class SrsRequest;
class SrsConnection;
class SrsAmf0Object;
class SrsAmf0StrictArray;

struct SrsStatisticVhost
{
public:
    int64_t id;
    std::string vhost;
    int nb_streams;
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
    virtual int dumps(SrsAmf0Object* obj);
};

struct SrsStatisticStream
{
public:
    int64_t id;
    SrsStatisticVhost* vhost;
    std::string app;
    std::string stream;
    std::string url;
    bool active;
    int connection_cid;
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
    virtual int dumps(SrsAmf0Object* obj);
public:
    /**
    * publish the stream.
    */
    virtual void publish(int cid);
    /**
    * close the stream.
    */
    virtual void close();
};

struct SrsStatisticClient
{
public:
    SrsStatisticStream* stream;
    SrsConnection* conn;
    SrsRequest* req;
    SrsRtmpConnType type;
    int id;
    int64_t create;
public:
    SrsStatisticClient();
    virtual ~SrsStatisticClient();
public:
    virtual int dumps(SrsAmf0Object* obj);
};

class SrsStatistic
{
private:
    static SrsStatistic *_instance;
    // the id to identify the sever.
    int64_t _server_id;
private:
    // key: vhost id, value: vhost object.
    std::map<int64_t, SrsStatisticVhost*> vhosts;
    // key: vhost url, value: vhost Object.
    // @remark a fast index for vhosts.
    std::map<std::string, SrsStatisticVhost*> rvhosts;
private:
    // key: stream id, value: stream Object.
    std::map<int64_t, SrsStatisticStream*> streams;
    // key: stream url, value: stream Object.
    // @remark a fast index for streams.
    std::map<std::string, SrsStatisticStream*> rstreams;
private:
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
    virtual SrsStatisticVhost* find_vhost(int vid);
    virtual SrsStatisticVhost* find_vhost(std::string name);
    virtual SrsStatisticStream* find_stream(int sid);
    virtual SrsStatisticClient* find_client(int cid);
public:
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
     * @param req the request object of publish connection.
     * @param cid the cid of publish connection.
     */
    virtual void on_stream_publish(SrsRequest* req, int cid);
    /**
    * when close stream.
    */
    virtual void on_stream_close(SrsRequest* req);
public:
    /**
     * when got a client to publish/play stream,
     * @param id, the client srs id.
     * @param req, the client request object.
     * @param conn, the physical absract connection object.
     * @param type, the type of connection.
     */
    virtual int on_client(int id, SrsRequest* req, SrsConnection* conn, SrsRtmpConnType type);
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
    * dumps the vhosts to amf0 array.
    */
    virtual int dumps_vhosts(SrsAmf0StrictArray* arr);
    /**
    * dumps the streams to amf0 array.
    */
    virtual int dumps_streams(SrsAmf0StrictArray* arr);
    /**
     * dumps the clients to amf0 array
     * @param start the start index, from 0.
     * @param count the max count of clients to dump.
     */
    virtual int dumps_clients(SrsAmf0StrictArray* arr, int start, int count);
private:
    virtual SrsStatisticVhost* create_vhost(SrsRequest* req);
    virtual SrsStatisticStream* create_stream(SrsStatisticVhost* vhost, SrsRequest* req);
};

#endif
