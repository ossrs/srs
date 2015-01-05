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

#ifndef SRS_APP_RTMP_CONN_HPP
#define SRS_APP_RTMP_CONN_HPP

/*
#include <srs_app_rtmp_conn.hpp>
*/

#include <srs_core.hpp>

#include <srs_app_st.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_reload.hpp>

class SrsRtmpServer;
class SrsRequest;
class SrsResponse;
class SrsSource;
class SrsRefer;
class SrsConsumer;
class SrsCommonMessage;
class SrsStSocket;
#ifdef SRS_AUTO_HTTP_CALLBACK    
class SrsHttpHooks;
#endif
class SrsBandwidth;
class SrsKbps;
class SrsRtmpClient;
class SrsSharedPtrMessage;
class SrsQueueRecvThread;
class SrsPublishRecvThread;
class SrsSecurity;

/**
* the client provides the main logic control for RTMP clients.
*/
class SrsRtmpConn : public virtual SrsConnection, public virtual ISrsReloadHandler
{
    // for the thread to directly access any field of connection.
    friend class SrsPublishRecvThread;
private:
    SrsRequest* req;
    SrsResponse* res;
    SrsStSocket* skt;
    SrsRtmpServer* rtmp;
    SrsRefer* refer;
    SrsBandwidth* bandwidth;
    SrsSecurity* security;
    // elapse duration in ms
    // for live play duration, for instance, rtmpdump to record.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/47
    int64_t duration;
    SrsKbps* kbps;
    // the MR(merged-write) sleep time in ms.
    int mw_sleep;
    // the MR(merged-write) only enabled for play.
    int mw_enabled;
    // for realtime
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/257
    bool realtime;
public:
    SrsRtmpConn(SrsServer* srs_server, st_netfd_t client_stfd);
    virtual ~SrsRtmpConn();
public:
    virtual void kbps_resample();
protected:
    virtual int do_cycle();
// interface ISrsReloadHandler
public:
    virtual int on_reload_vhost_removed(std::string vhost);
    virtual int on_reload_vhost_mw(std::string vhost);
    virtual int on_reload_vhost_realtime(std::string vhost);
// interface IKbpsDelta
public:
    virtual int64_t get_send_bytes_delta();
    virtual int64_t get_recv_bytes_delta();
private:
    // when valid and connected to vhost/app, service the client.
    virtual int service_cycle();
    // stream(play/publish) service cycle, identify client first.
    virtual int stream_service_cycle();
    virtual int check_vhost();
    virtual int playing(SrsSource* source);
    virtual int do_playing(SrsSource* source, SrsQueueRecvThread* trd);
    virtual int fmle_publishing(SrsSource* source);
    virtual int flash_publishing(SrsSource* source);
    virtual int do_publishing(SrsSource* source, SrsPublishRecvThread* trd);
    virtual int handle_publish_message(SrsSource* source, SrsCommonMessage* msg, bool is_fmle, bool vhost_is_edge);
    virtual int process_publish_message(SrsSource* source, SrsCommonMessage* msg, bool vhost_is_edge);
    virtual int process_play_control_msg(SrsConsumer* consumer, SrsCommonMessage* msg);
    virtual void change_mw_sleep(int sleep_ms);
private:
    virtual int check_edge_token_traverse_auth();
    virtual int connect_server(int origin_index, st_netfd_t* pstsock);
    virtual int do_token_traverse_auth(SrsRtmpClient* client);
private:
    virtual int http_hooks_on_connect();
    virtual void http_hooks_on_close();
    virtual int http_hooks_on_publish();
    virtual void http_hooks_on_unpublish();
    virtual int http_hooks_on_play();
    virtual void http_hooks_on_stop();
};

#endif

