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

#ifndef SRS_APP_GB28181_HPP
#define SRS_APP_GB28181_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>
#include <map>

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_server.hpp>
#include <srs_service_st.hpp>

class SrsStSocket;
class SrsConfDirective;
class Srs2SRtpPacket;
class SrsRequest;
class SrsStSocket;
class SrsRtmpClient;
class SrsRawH264Stream;
class SrsRawAacStream;
struct SrsRawAacStreamCodec;
class SrsSharedPtrMessage;
class SrsAudioFrame;
class SrsSimpleStream;
class SrsSimpleBufferX;
class SrsPithyPrint;
class SrsSimpleRtmpClient;
class SrsListener;
class SrsLifeGuardThread;
class Srs28181Listener;
class Srs28181UdpStreamListener;
class Srs28181TcpStreamListener;
class Srs28181TcpStreamConn;
class Srs28181StreamCore;

// A gb28181 stream server
class Srs28181StreamServer
{
private:
    std::string output;
    int local_port_min;
    int local_port_max;
    // The key: port, value: whether used.
    std::map<int, bool> used_ports;
    private:
    SrsCoroutine * trd;
private:
    // TODO: will expand for multi-listeners
    std::vector<Srs28181Listener*> listeners;
public:
    Srs28181StreamServer();
    Srs28181StreamServer(SrsConfDirective* c);
    virtual ~Srs28181StreamServer();
public:
    // create a  28181 stream listener
    virtual srs_error_t create_listener(SrsListenerType type, int& ltn_port, std::string& suuid);
    // release a listener
    virtual void release_listener(Srs28181Listener * ltn);
    // Alloc a rtp port from local ports pool.
    // @param pport output the rtp port.
    virtual srs_error_t alloc_port(int* pport);
    // Free the alloced rtp port.
    virtual void free_port(int lpmin, int lpmax);

public:
    virtual void remove();
};

// A base listener
class Srs28181Listener
{
protected:
    std::string ip;
    int port;
public:
    Srs28181Listener();
    virtual ~Srs28181Listener();
public:
    //virtual SrsListenerType listen_type();
    virtual srs_error_t listen(std::string i, int p) = 0;
};

// A TCP listener
class Srs28181TcpStreamListener : public Srs28181Listener, public ISrsTcpHandler
{
private:
    SrsTcpListener* listener;
    //ISrsTcpHandler* caster;

    std::vector<Srs28181TcpStreamConn*> clients;
public:
    Srs28181TcpStreamListener();
    //Srs28181TcpStreamListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c);
    virtual ~Srs28181TcpStreamListener();
public:
    virtual srs_error_t listen(std::string i, int p);
// Interface ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(srs_netfd_t stfd);
    virtual srs_error_t remove_conn(Srs28181TcpStreamConn* c);
};


// A ST-coroutine is a lightweight thread, just like the goroutine.
// But the goroutine maybe run on different thread, while ST-coroutine only
// run in single thread, because it use setjmp and longjmp, so it may cause
// problem in multiple threads. For SRS, we only use single thread module,
// like NGINX to get very high performance, with asynchronous and non-blocking
// sockets.

/* 
* SrsOneCycleCoroutine has these features:
* 1.will exit thread if it returns from cycle function
* 2.no pull function 
* 3.can destory itself in cycle
*
*/

class SrsOneCycleCoroutine: public ISrsCoroutineHandler
{
private:
    std::string name;
    ISrsCoroutineHandler* handler;
private:
    srs_thread_t trd;
    int context;
    srs_error_t trd_err;
private:
    bool started;
    bool interrupted;
    bool disposed;
    // Cycle done, no need to interrupt it.
    bool cycle_done;
public:
    // Create a thread with name n and handler h.
    // @remark User can specify a cid for thread to use, or we will allocate a new one.
    SrsOneCycleCoroutine(std::string n, ISrsCoroutineHandler* h, int cid = 0);
    virtual ~SrsOneCycleCoroutine();
public:
    // Start the thread.
    // @remark Should never start it when stopped or terminated.
    virtual srs_error_t start();
    // Interrupt the thread then wait to terminated.
    // @remark If user want to notify thread to quit async, for example if there are
    //      many threads to stop like the encoder, use the interrupt to notify all threads
    //      to terminate then use stop to wait for each to terminate.
    virtual void stop();
    // Interrupt the thread and notify it to terminate, it will be wakeup if it's blocked
    // in some IO operations, such as st_read or st_write, then it will found should quit,
    // finally the thread should terminated normally, user can use the stop to join it.
    virtual void interrupt();

    // Get the context id of thread.
    virtual int cid();
private:
    virtual srs_error_t cycle();
    static void* pfn(void* arg);
};

// Bind a udp port, start a thread to recv packet and handler it.
class SrsLiveUdpListener : public ISrsCoroutineHandler
{
private:
    srs_netfd_t lfd;
    SrsOneCycleCoroutine* trd;
private:
    char* buf;
    int nb_buf;
private:
    Srs28181UdpStreamListener* handler;
    std::string ip;
    int port;
private:
    //srs_cond_t cond;
    uint64_t nb_packet_;
public:
    SrsLiveUdpListener(Srs28181UdpStreamListener* h, std::string i, int p);
    virtual ~SrsLiveUdpListener();
public:
    virtual int fd();
    virtual srs_netfd_t stfd();
    //virtual srs_error_t wait(srs_utime_t tm);
public:
    uint64_t nb_packet();
public:
    virtual srs_error_t listen();
public:
    virtual srs_error_t cycle();
};

// A guard thread
class SrsLifeGuardThread : public SrsOneCycleCoroutine
{
private:
    srs_cond_t lgcond;
public:
    SrsLifeGuardThread(std::string n, ISrsCoroutineHandler* h, int cid = 0);
    virtual ~SrsLifeGuardThread();
public:
    virtual void stop();
public:
    virtual void awake();
    virtual void wait(srs_utime_t tm);
};

// 28181 udp stream linstener
class Srs28181UdpStreamListener : public Srs28181Listener, public ISrsUdpHandler, public ISrsCoroutineHandler
{
protected:
    SrsLiveUdpListener* listener;
    Srs28181StreamCore* streamcore;
private:
    SrsLifeGuardThread* lifeguard;
    uint64_t nb_packet;
    bool workdone;
public:
    Srs28181UdpStreamListener(Srs28181StreamServer * srv, std::string suuid);
    virtual ~Srs28181UdpStreamListener();
private:
    Srs28181StreamServer * server;
public:
    virtual srs_error_t cycle();
    virtual void interrupt();
public:
    virtual srs_error_t listen(std::string i, int p);
    virtual srs_error_t on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf);
};


// The audio cache, audio is grouped by frames.
struct Srs28181AudioCache
{
    int64_t dts;
    SrsAudioFrame* audio;
    //SrsSimpleStream* payload;
    // TODO: may merge with 28181 someday
    SrsSimpleBufferX* payload;
    
    Srs28181AudioCache();
    virtual ~Srs28181AudioCache();
};

// The time jitter correct for rtsp.
class Srs28181Jitter
{
private:
    int64_t previous_timestamp;
    int64_t pts;
    int delta;
public:
    Srs28181Jitter();
    virtual ~Srs28181Jitter();
public:
    virtual int64_t timestamp();
    virtual srs_error_t correct(int64_t& ts);
};

// 28181 stream core 
class Srs28181StreamCore
{
private:
    std::string output;
    std::string output_template;
    std::string target_tcUrl;//rtsp_tcUrl;
    std::string stream_name;//rtsp_stream;
    SrsPithyPrint* pprint;
private:
    std::string session;
    // video stream.
    int video_id;
    std::string video_codec;
    // audio stream.
    int audio_id;
    std::string audio_codec;
    int audio_sample_rate;
    int audio_channel;
private:
    ////SrsRequest* req;
    SrsSimpleRtmpClient* sdk;
    Srs28181Jitter* vjitter;
    Srs28181Jitter* ajitter;
private:
    SrsRawH264Stream* avc;
    std::string h264_sps;
    std::string h264_pps;
	bool h264_sps_changed;
	bool h264_pps_changed;
	bool h264_sps_pps_sent;
private:
    SrsRawAacStream* aac;
    SrsRawAacStreamCodec* acodec;
    std::string aac_specific_config;
    Srs28181AudioCache* acache;
private:
    // this param group using on rtp packet decode 
    
    int stream_id;

    Srs2SRtpPacket* cache_;
public:
    Srs28181StreamCore(std::string suuid);
    virtual ~Srs28181StreamCore();

public:
    // decode rtp
    virtual int decode_packet(char* buf, int nb_buf);

// internal methods
public:
    virtual srs_error_t on_stream_packet(Srs2SRtpPacket* pkt, int stream_id);
    virtual srs_error_t on_stream_video(Srs2SRtpPacket* pkt, int64_t dts, int64_t pts);
private:
    virtual srs_error_t on_rtp_video(Srs2SRtpPacket* pkt, int64_t dts, int64_t pts);
    virtual srs_error_t on_rtp_audio(Srs2SRtpPacket* pkt, int64_t dts);
    virtual srs_error_t kickoff_audio_cache(Srs2SRtpPacket* pkt, int64_t dts);
private:
    virtual srs_error_t write_sequence_header();
    virtual srs_error_t write_h264_sps_pps(uint32_t dts, uint32_t pts);
    virtual srs_error_t write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts);
    virtual srs_error_t write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts);
    virtual srs_error_t rtmp_write_packet(char type, uint32_t timestamp, char* data, int size);
private:
    // Connect to RTMP server.
    virtual srs_error_t connect();
    // Close the connection to RTMP server.
    virtual void close();
};

// TODO: will rewrite blow codes for TCP mode in future
// The 28181 tcp stream connection 
class Srs28181TcpStreamConn : public ISrsCoroutineHandler
{
private:
    std::string output;
    std::string output_template;
    std::string target_tcUrl;
    std::string stream_name;
    SrsPithyPrint* pprint;
public:
    Srs28181TcpStreamConn(Srs28181TcpStreamListener* l, srs_netfd_t fd, std::string o);
    virtual ~Srs28181TcpStreamConn();

public:
    srs_error_t init();

public:
    virtual srs_error_t cycle();

};
#endif