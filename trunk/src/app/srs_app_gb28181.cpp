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

#include <srs_app_gb28181.hpp>

#include <algorithm>
#include <unistd.h>
#include <sys/time.h>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_gb28181_stack.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_file.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_raw_avc.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_format.hpp>
#include <st.h>

// default stream recv timeout
static srs_utime_t DEFAULT_28181_STREAM_TIMEOUT = 10 * SRS_UTIME_SECONDS;

Srs28181AudioCache::Srs28181AudioCache()
{
    dts = 0;
    audio = NULL;
    payload = NULL;
}

Srs28181AudioCache::~Srs28181AudioCache()
{
    srs_freep(audio);
    srs_freep(payload);
}

Srs28181Jitter::Srs28181Jitter()
{
    delta = 0;
    previous_timestamp = 0;
    pts = 0;
}

Srs28181Jitter::~Srs28181Jitter()
{
}

int64_t Srs28181Jitter::timestamp()
{
    return pts;
}

srs_error_t Srs28181Jitter::correct(int64_t &ts)
{
    srs_error_t err = srs_success;

    if (previous_timestamp == 0)
    {
        previous_timestamp = ts;
    }

    delta = srs_max(0, (int)(ts - previous_timestamp));
    if (delta > 90000)
    {
        delta = 0;
    }

    previous_timestamp = ts;

    ts = pts + delta;
    pts = ts;

    return err;
}

Srs28181StreamServer::Srs28181StreamServer()
{
    // set some default values 
    output = "output_rtmp_url";
    rtmp_port = "";
    local_port_min = 55000;
    local_port_max = 65000;
    port_offset = 0;
    ender = new SrsStreamEnder();
}

Srs28181StreamServer::~Srs28181StreamServer()
{
    srs_info("28181- server: deconstruction");

    srs_freep(ender);

    std::vector<Srs28181Listener *>::iterator it;
    for (it = listeners.begin(); it != listeners.end(); ++it)
    {
        Srs28181Listener *ltn = *it;
        srs_freep(ltn);
    }

    listeners.clear();
    used_ports.clear();
}

srs_error_t Srs28181StreamServer::init()
{
    srs_error_t err = srs_success;
    if ((err = ender->start()) != srs_success) {
        return srs_error_wrap(err, "start ender");
    }

    if(_srs_config->get_listens().size()==0){
        return srs_error_new(13029,"no rtmp port");
    }

    local_port_min = _srs_config->get_2ss_listen_port_min();
    local_port_max = _srs_config->get_2ss_listen_port_max();
    if(local_port_min==0 || local_port_max==0){
        srs_warn("28181 stream listen port configuraion is invalied, we will use default setting! %d-%d",
            local_port_min,local_port_max);
        local_port_min = 55000;
        local_port_max = 65000;
    }

    srs_trace("28181-stream-server - will use port between[%d-%d]",local_port_min,local_port_max);    
    rtmp_port = _srs_config->get_listens().front();
    return err;
}

uint32_t randomNumberSeed() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  const uint32_t kPrime1 = 61631;
  const uint32_t kPrime2 = 64997;
  const uint32_t kPrime3 = 111857;
  return kPrime1 * static_cast<uint32_t>(getpid())
       + kPrime2 * static_cast<uint32_t>(tv.tv_sec)
       + kPrime3 * static_cast<uint32_t>(tv.tv_usec);
}

srs_error_t Srs28181StreamServer::create_listener(SrsListenerType type, int &ltn_port, std::string &suuid)
{
    srs_error_t err = srs_success;

    // TODO: besson:may use uuid in future
    std::string rd = "";
    srand(randomNumberSeed());
    for (int i = 0; i < 32; i++)
    {
        rd = rd + char(rand() % 10 + '0');
    }
    suuid = rd;

    Srs28181Listener *ltn = NULL;
    if (type == SrsListener28181UdpStream)
    {
        ltn = new Srs28181UdpStreamListener(this, suuid, rtmp_port);
    }
    else if (SrsListener28181UdpStream)
    {
        ltn = new Srs28181TcpStreamListener();
    }
    else
    {
        return srs_error_new(13026, "28181 listener creation");
    }

    int port = 0;
    alloc_port(&port);
    ltn_port = port;

    srs_trace("28181-stream-server: start a new listener[0x%x] on %s-%d stream_uuid:%s",ltn,
              srs_any_address_for_listener().c_str(), port, suuid.c_str());

    if ((err = ltn->listen(srs_any_address_for_listener(), port)) != srs_success)
    {
        free_port(port, port + 2);
        srs_freep(ltn);
        return srs_error_wrap(err, "28181 listener creation");
    }

    listeners.push_back(ltn);

    return err;
}

void Srs28181StreamServer::release_listener(Srs28181Listener *ltn)
{
    std::vector<Srs28181Listener *>::iterator it = find(listeners.begin(), listeners.end(), ltn);
    if (it != listeners.end())
    {
        int p = ltn->get_port();
        free_port(p,p+2);

        listeners.erase(it);
        ender->remove(ltn);
    }

    srs_trace("28181-stream-server: release listener:0x%x", ltn);
}

srs_error_t Srs28181StreamServer::alloc_port(int *pport)
{
    srs_error_t err = srs_success;

    // use a pair of port.
    for (int i = local_port_min+port_offset; i < local_port_max - 1; i += 2)
    {
        if (!used_ports[i])
        {
            used_ports[i] = true;
            used_ports[i + 1] = true;
            *pport = i;

            port_offset += 2;
            if(port_offset >= local_port_max){
                port_offset = 0;
            }

            break;
        }
    }

    srs_info("28181 tcp stream: alloc port=%d-%d offset=%d", *pport, *pport + 1, port_offset);

    return err;
}

void Srs28181StreamServer::free_port(int lpmin, int lpmax)
{
    for (int i = lpmin; i < lpmax; i++)
    {
        used_ports[i] = false;
    }
    srs_trace("28181stream: free rtp port=%d-%d offset:%d", lpmin, lpmax-1, port_offset);
}

SrsStreamEnder::SrsStreamEnder()
{
    cond = srs_cond_new();
    trd = new SrsSTCoroutine("ender", this);
}

SrsStreamEnder::~SrsStreamEnder()
{
    srs_freep(trd);
    srs_cond_destroy(cond);
    
    release();
}

srs_error_t SrsStreamEnder::start()
{
    srs_error_t err = srs_success;
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine manager");
    }
    
    return err;
}

srs_error_t SrsStreamEnder::cycle()
{
    srs_error_t err = srs_success;
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "coroutine ender");
        }
        
        srs_cond_wait(cond);
        release();
    }
    
    return err;
}

void SrsStreamEnder::remove(Srs28181Listener* o)
{
    group.push_back(o);
    srs_cond_signal(cond);
    
    srs_trace("stream ender: will remove listener:0x%x", o);
}

void SrsStreamEnder::release()
{
    // To prevent thread switch when delete ST,
    // we copy all STs then free one by one.
    vector<Srs28181Listener*> copy = group;
    group.clear();
    
    vector<Srs28181Listener*>::iterator it;
    for (it = copy.begin(); it != copy.end(); ++it) {
        Srs28181Listener* one = *it;
        srs_freep(one);
    }
}


Srs28181Listener::Srs28181Listener()
{
    ip="";
    port=0;
}

Srs28181Listener::~Srs28181Listener()
{

}

int Srs28181Listener::get_port()
{
    return port;
}

Srs28181TcpStreamListener::Srs28181TcpStreamListener()
{
    listener = NULL;
}

Srs28181TcpStreamListener::~Srs28181TcpStreamListener()
{
    std::vector<Srs28181TcpStreamConn *>::iterator it;
    for (it = clients.begin(); it != clients.end(); ++it)
    {
        Srs28181TcpStreamConn *conn = *it;
        srs_freep(conn);
    }
    clients.clear();

    //srs_freep(caster);
    srs_freep(listener);
}

srs_error_t Srs28181TcpStreamListener::listen(string i, int p)
{
    srs_error_t err = srs_success;

    std::string ip = i;
    int port = p;

    srs_freep(listener);
    listener = new SrsTcpListener(this, ip, port);

    if ((err = listener->listen()) != srs_success)
    {
        return srs_error_wrap(err, "28181 listen %s:%d", ip.c_str(), port);
    }

    //string v = srs_listener_type2string(type);
    srs_trace("listen at tcp://%s:%d, fd=%d", ip.c_str(), port, listener->fd());

    return err;
}

srs_error_t Srs28181TcpStreamListener::on_tcp_client(srs_netfd_t stfd)
{
    srs_error_t err = srs_success;

    if (clients.size() >= 1)
    {
        return srs_error_wrap(err, "only allow one src!");
    }

    std::string output = "output_temple";
    Srs28181TcpStreamConn *conn = new Srs28181TcpStreamConn(this, stfd, output);
    srs_trace("28181- listener(0x%x): accept a new connection(0x%x)", this, conn);

    if ((err = conn->init()) != srs_success)
    {
        srs_freep(conn);
        return srs_error_wrap(err, "28181 stream conn init");
    }
    clients.push_back(conn);

    return err;
}

srs_error_t Srs28181TcpStreamListener::remove_conn(Srs28181TcpStreamConn *c)
{
    srs_error_t err = srs_success;
    //srs_error_new(ERROR_THREAD_DISPOSED, "disposed");

    std::vector<Srs28181TcpStreamConn *>::iterator it = find(clients.begin(), clients.end(), c);
    if (it != clients.end())
    {
        clients.erase(it);
    }
    srs_info("28181 - listener: remove connection.");

    return err;
}

SrsLiveUdpListener::SrsLiveUdpListener(Srs28181UdpStreamListener *h, string i, int p)
{
    handler = h;
    ip = i;
    port = p;
    lfd = NULL;

    nb_buf = 1024 * 4;
    buf = new char[nb_buf];
    nb_packet_ = 0;

    trd = NULL;
}

SrsLiveUdpListener::~SrsLiveUdpListener()
{
    trd->stop();
    srs_freep(trd);
    srs_close_stfd(lfd);
    srs_freepa(buf);
}

int SrsLiveUdpListener::fd()
{
    return srs_netfd_fileno(lfd);
}

srs_netfd_t SrsLiveUdpListener::stfd()
{
    return lfd;
}

uint64_t SrsLiveUdpListener::nb_packet()
{
    return nb_packet_;
}

srs_error_t SrsLiveUdpListener::listen()
{
    srs_error_t err = srs_success;

    if ((err = srs_udp_listen(ip, port, &lfd)) != srs_success)
    {
        return srs_error_wrap(err, "listen %s:%d", ip.c_str(), port);
    }

    srs_freep(trd);
    //trd = new SrsOneCycleCoroutine("udp", this);
    trd = new SrsSTCoroutine("udp", this);
    if ((err = trd->start()) != srs_success)
    {
        return srs_error_wrap(err, "start thread");
    }

    return err;
}

srs_error_t SrsLiveUdpListener::cycle()
{
    srs_error_t err = srs_success;

    while (true)
    {

        int nread = 0;
        sockaddr_storage from;
        int nb_from = sizeof(from);
        if ((nread = srs_recvfrom(lfd, buf, nb_buf, (sockaddr *)&from, &nb_from, SRS_UTIME_NO_TIMEOUT)) <= 0)
        {
            return srs_error_new(ERROR_SOCKET_READ, "udp read, nread=%d", nread);
        }

        if ((err = handler->on_udp_packet((const sockaddr *)&from, nb_from, buf, nread)) != srs_success)
        {
            return srs_error_wrap(err, "handle packet %d bytes", nread);
        }

        nb_packet_++;
        handler->interrupt();
    }

    return err;
}

SrsLifeGuardThread::SrsLifeGuardThread(std::string n, ISrsCoroutineHandler *h, int cid) : SrsSTCoroutine(n, h, cid)
{
    lgcond = srs_cond_new();
}

SrsLifeGuardThread::~SrsLifeGuardThread()
{
    srs_cond_destroy(lgcond);
}

void SrsLifeGuardThread::wait(srs_utime_t tm)
{
    srs_cond_timedwait(lgcond, tm);
}

void SrsLifeGuardThread::awake()
{
    srs_cond_signal(lgcond);
}

Srs28181UdpStreamListener::Srs28181UdpStreamListener(Srs28181StreamServer *srv, std::string suuid, std::string port)
{
    server = srv;
    listener = NULL;
    streamcore = new Srs28181StreamCore(suuid, port);
    
    nb_packet = 0;
    lifeguard = new SrsLifeGuardThread("28181-udp-listener", this);
    if ((lifeguard->start()) != srs_success)
    {
        srs_freep(lifeguard);
        srs_error("28181-udp-listener - lifeguard start failed!");
    }
}

Srs28181UdpStreamListener::~Srs28181UdpStreamListener()
{
    srs_freep(listener);
    srs_freep(streamcore);
    srs_freep(lifeguard);
}

srs_error_t Srs28181UdpStreamListener::cycle()
{
    srs_error_t err = srs_success;

    while (true)
    {
        if (lifeguard == NULL)
        {
            srs_error("28181-udp-listener - lifeguard is NULL. We must release this listener!");
            server->release_listener(this);
            return srs_error_new(13029, "28181-udp-listener lifeguard is not invalid");
        }

         if ((err = lifeguard->pull()) != srs_success) {
            return srs_error_wrap(err, "udp listener");
        }

        lifeguard->wait(DEFAULT_28181_STREAM_TIMEOUT);

        if (listener->nb_packet() <= nb_packet)
        {
            srs_warn("28181-udp-listener - recv timeout. we will release this listener[%d-%d]",
                     DEFAULT_28181_STREAM_TIMEOUT, nb_packet);

            server->release_listener(this);
            return srs_error_new(13027, "28181-udp-listener recv timeout");
        }
        nb_packet = listener->nb_packet();
    }

    return err;
}

void Srs28181UdpStreamListener::interrupt()
{
    lifeguard->awake();
}

srs_error_t Srs28181UdpStreamListener::listen(string i, int p)
{
    srs_error_t err = srs_success;

    ip = i;
    port = p;

    srs_freep(listener);
    listener = new SrsLiveUdpListener(this, ip, port);

    if ((err = listener->listen()) != srs_success)
    {
        return srs_error_wrap(err, "listen %s:%d", ip.c_str(), port);
    }

    // notify the handler the fd changed.
    if ((err = on_stfd_change(listener->stfd())) != srs_success)
    {
        return srs_error_wrap(err, "notify fd change failed");
    }

    srs_trace("listen 28181 udp stream at udp://%s:%d, fd=%d", ip.c_str(), port, listener->fd());

    return err;
}

srs_error_t Srs28181UdpStreamListener::on_udp_packet(const sockaddr *from, const int fromlen, char *buf, int nb_buf)
{
    srs_error_t err = srs_success;

    // TODO: will modify return value in future
    // default 28181 stream decoder
    int ret = streamcore->decode_packet(buf, nb_buf);
    if (ret != 0)
    {
        return srs_error_new(ret, "process 28181 udp stream");
    }

    return err;
}

Srs28181StreamCore::Srs28181StreamCore(std::string sid, std::string port)
{
    // TODO: besson: may rewrite stream address formation in future
    target_tcUrl = "rtmp://127.0.0.1:7935/live/" + sid; //"rtmp://127.0.0.1:" + "7935" + "/live/test";
    target_tcUrl = "rtmp://127.0.0.1:"+port+"/live/" + sid; 
    output_template = "rtmp://127.0.0.1:"+port+"/[app]/[stream]";

    session = "";
    // TODO: set stream_id when connected
    stream_id = 50125;
    video_id = stream_id;

    h264_sps = "";
    h264_pps = "";
    h264_sps_changed = false;
    h264_pps_changed = false;
    h264_sps_pps_sent = false;
    cache_ = NULL;

    sdk = NULL;
    vjitter = new Srs28181Jitter();
    ajitter = new Srs28181Jitter();

    avc = new SrsRawH264Stream();
    aac = new SrsRawAacStream();
    acodec = new SrsRawAacStreamCodec();
    acache = new Srs28181AudioCache();
    pprint = SrsPithyPrint::create_caster();
}

Srs28181StreamCore::~Srs28181StreamCore()
{
    close();
    srs_freep(sdk);

    srs_freep(vjitter);
    srs_freep(ajitter);
    srs_freep(acodec);
    srs_freep(acache);
    srs_freep(pprint);
}

srs_error_t Srs28181StreamCore::on_stream_packet(Srs2SRtpPacket *pkt, int stream_id)
{
    srs_error_t err = srs_success;

    // ensure rtmp connected.
    if ((err = connect()) != srs_success)
    {
        return srs_error_wrap(err, "connect");
    }

    if (stream_id == video_id)
    {
        // tbn is ts tbn.
        int64_t pts = pkt->timestamp;
        if ((err = vjitter->correct(pts)) != srs_success)
        {
            return srs_error_wrap(err, "jitter");
        }

        // TODO: FIXME: set dts to pts, please finger out the right dts.
        int64_t dts = pts;

        return on_stream_video(pkt, dts, pts);
    }
    else
    {
        // tbn is ts tbn.
        int64_t pts = pkt->timestamp;
        if ((err = ajitter->correct(pts)) != srs_success)
        {
            return srs_error_wrap(err, "jitter");
        }

        return on_rtp_audio(pkt, pts);
    }

    return err;
}

srs_error_t Srs28181StreamCore::on_stream_video(Srs2SRtpPacket *pkt, int64_t dts, int64_t pts)
{
    srs_error_t err = srs_success;

    if (pkt->tgtstream->length() <= 0)
    {
        srs_trace("28181streamcore - empty stream, will continue");
        return err;
    }

    SrsBuffer stream(pkt->tgtstream->bytes(), pkt->tgtstream->length());

    // send each frame.
    while (!stream.empty())
    {
        char *frame = NULL;
        int frame_size = 0;

        if ((err = avc->annexb_demux(&stream, &frame, &frame_size)) != srs_success)
        {
            srs_warn("28181streamcore - waring: no nalu in buffer.[%d]", srs_error_code(err));
            return srs_success;
        }

        // for highly reliable. Only give notification but exit
        if (frame_size <= 0)
        {
            srs_warn("h264 stream: frame_size <=0, and continue for next loop!");
            continue;
        }

        // ignore others.
        // 5bits, 7.3.1 NAL unit syntax,
        // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
        //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame, 9: AUD
        SrsAvcNaluType nut = (SrsAvcNaluType)(frame[0] & 0x1f);
        if (nut != SrsAvcNaluTypeSPS && nut != SrsAvcNaluTypePPS //&& nut != SrsAvcNaluTypeSEI
            && nut != SrsAvcNaluTypeIDR && nut != SrsAvcNaluTypeNonIDR && nut != SrsAvcNaluTypeAccessUnitDelimiter)
        {
            continue;
        }

        // for sps
        if (avc->is_sps(frame, frame_size))
        {
            std::string sps = "";
            if ((err = avc->sps_demux(frame, frame_size, sps)) != srs_success)
            {
                srs_error("h264-ps: invalied sps in dts=%d", dts);
                continue;
            }

            if(sps.length() < 4){
                srs_warn("h264-ps stream: sps length < 4 !");
                continue;
            }

            if (h264_sps != sps)
            {
                h264_sps = sps;
                h264_sps_changed = true;
                h264_sps_pps_sent = false;
                srs_trace("h264-ps stream: set SPS frame size=%d, dts=%d", frame_size, dts);
            }
        }

        // for pps
        if (avc->is_pps(frame, frame_size))
        {
            std::string pps = "";
            if ((err = avc->pps_demux(frame, frame_size, pps)) != srs_success)
            {
                srs_error("h264-ps: invalied sps in dts=%d", dts);
                continue;
            }

            if (h264_pps != pps)
            {
                h264_pps = pps;
                h264_pps_changed = true;
                h264_sps_pps_sent = false;
                srs_trace("h264-ps stream: set PPS frame size=%d, dts=%d", frame_size, dts);
            }
        }

        // set sps/pps
        if (h264_sps_changed && h264_pps_changed)
        {
            h264_sps_changed = false;
            h264_pps_changed = false;
            h264_sps_pps_sent = true;

            if ((err = write_h264_sps_pps(dts / 90, pts / 90)) != srs_success)
            {
                srs_error("h264-ps stream: Re-write SPS-PPS Wrong! frame size=%d, dts=%d", frame_size, dts);
                return srs_error_wrap(err, "re-write sps-pps failed");
            }
            srs_warn("h264-ps stream: Re-write SPS-PPS Successful! frame size=%d, dts=%d", frame_size, dts);
        }

        if (h264_sps_pps_sent && nut != SrsAvcNaluTypeSPS && nut != SrsAvcNaluTypePPS)
        {
            if ((err = kickoff_audio_cache(pkt, dts)) != srs_success)
            {
                srs_warn("h264-ps stream: kickoff audio cache dts=%d", dts);
                return srs_error_wrap(err, "killoff audio cache failed");
            }

            // ibp frame.
            // TODO: FIXME: we should group all frames to a rtmp/flv message from one ts message.
            srs_info("h264-ps stream: demux avc ibp frame size=%d, dts=%d", frame_size, dts);
            if ((err = write_h264_ipb_frame(frame, frame_size, dts / 90, pts / 90)) != srs_success)
            {
                return srs_error_wrap(err, "write ibp failed");
            }
        }
    } //while send frame

    return err;
}

srs_error_t Srs28181StreamCore::on_rtp_video(Srs2SRtpPacket *pkt, int64_t dts, int64_t pts)
{
    srs_error_t err = srs_success;

    if ((err = kickoff_audio_cache(pkt, dts)) != srs_success)
    {
        return srs_error_wrap(err, "kickoff audio cache");
    }

    char *bytes = pkt->payload->bytes();
    int length = pkt->payload->length();
    uint32_t fdts = (uint32_t)(dts / 90);
    uint32_t fpts = (uint32_t)(pts / 90);
    if ((err = write_h264_ipb_frame(bytes, length, fdts, fpts)) != srs_success)
    {
        return srs_error_wrap(err, "write ibp frame");
    }

    return err;
}

srs_error_t Srs28181StreamCore::on_rtp_audio(Srs2SRtpPacket *pkt, int64_t dts)
{
    srs_error_t err = srs_success;

    if ((err = kickoff_audio_cache(pkt, dts)) != srs_success)
    {
        return srs_error_wrap(err, "kickoff audio cache");
    }

    // cache current audio to kickoff.
    acache->dts = dts;
    acache->audio = pkt->audio;
    acache->payload = pkt->payload;

    pkt->audio = NULL;
    pkt->payload = NULL;

    return err;
}

srs_error_t Srs28181StreamCore::kickoff_audio_cache(Srs2SRtpPacket *pkt, int64_t dts)
{
    srs_error_t err = srs_success;

    // nothing to kick off.
    if (!acache->payload)
    {
        return err;
    }

    if (dts - acache->dts > 0 && acache->audio->nb_samples > 0)
    {
        int64_t delta = (dts - acache->dts) / acache->audio->nb_samples;
        for (int i = 0; i < acache->audio->nb_samples; i++)
        {
            char *frame = acache->audio->samples[i].bytes;
            int nb_frame = acache->audio->samples[i].size;
            int64_t timestamp = (acache->dts + delta * i) / 90;
            acodec->aac_packet_type = 1;
            if ((err = write_audio_raw_frame(frame, nb_frame, acodec, (uint32_t)timestamp)) != srs_success)
            {
                return srs_error_wrap(err, "write audio raw frame");
            }
        }
    }

    acache->dts = 0;
    srs_freep(acache->audio);
    srs_freep(acache->payload);

    return err;
}

// TODO: besson will modify return type in future
int Srs28181StreamCore::decode_packet(char *buf, int nb_buf)
{
    int ret = 0;
    int status;

    pprint->elapse();

    if (true)
    {
        SrsBuffer stream(buf, nb_buf);

        Srs2SRtpPacket pkt;
        if ((ret = pkt.decode(&stream)) != ERROR_SUCCESS)
        {
            srs_error("28181: decode rtp packet failed. ret=%d", ret);
            return ret;
        }

        if (pkt.chunked)
        {
            if (!cache_)
            {
                cache_ = new Srs2SRtpPacket();
            }
            cache_->copy(&pkt);
            cache_->payload->append(pkt.payload->bytes(), pkt.payload->length());

            // besson: correct rtp decode bug
            if (!cache_->completed )
            {
                return ret;
            }
        }
        else
        {
            srs_freep(cache_);
            cache_ = new Srs2SRtpPacket();
            cache_->reap(&pkt);
        }
    }

    if (pprint->can_print())
    {
        srs_trace("<- " SRS_CONSTS_LOG_STREAM_CASTER "  rtp #%d %dB, age=%d, vt=%d/%u, sts=%u/%u/%#x, paylod=%dB, chunked=%d",
                  stream_id, nb_buf, pprint->age(), cache_->version, cache_->payload_type, cache_->sequence_number, cache_->timestamp, cache_->ssrc,
                  cache_->payload->length(), cache_->chunked);
    }

    // always free it.
    SrsAutoFree(Srs2SRtpPacket, cache_);

    cache_->decode_stream();

    srs_error_t err = srs_success;
    if ((err = on_stream_packet(cache_, stream_id)) != srs_success)
    {
        srs_error("28181: process rtp packet failed. ret=%d", err->error_code(err));
        return -1;
    }

    return ret;
}

srs_error_t Srs28181StreamCore::write_sequence_header()
{
    srs_error_t err = srs_success;

    // use the current dts.
    int64_t dts = vjitter->timestamp() / 90;

    // send video sps/pps
    if ((err = write_h264_sps_pps((uint32_t)dts, (uint32_t)dts)) != srs_success)
    {
        return srs_error_wrap(err, "write sps/pps");
    }

    // generate audio sh by audio specific config.
    if (true)
    {
        std::string sh = aac_specific_config;

        SrsFormat *format = new SrsFormat();
        SrsAutoFree(SrsFormat, format);

        if ((err = format->on_aac_sequence_header((char *)sh.c_str(), (int)sh.length())) != srs_success)
        {
            return srs_error_wrap(err, "on aac sequence header");
        }

        SrsAudioCodecConfig *dec = format->acodec;

        acodec->sound_format = SrsAudioCodecIdAAC;
        acodec->sound_type = (dec->aac_channels == 2) ? SrsAudioChannelsStereo : SrsAudioChannelsMono;
        acodec->sound_size = SrsAudioSampleBits16bit;
        acodec->aac_packet_type = 0;

        static int srs_aac_srates[] = {
            96000, 88200, 64000, 48000,
            44100, 32000, 24000, 22050,
            16000, 12000, 11025, 8000,
            7350, 0, 0, 0};
        switch (srs_aac_srates[dec->aac_sample_rate])
        {
        case 11025:
            acodec->sound_rate = SrsAudioSampleRate11025;
            break;
        case 22050:
            acodec->sound_rate = SrsAudioSampleRate22050;
            break;
        case 44100:
            acodec->sound_rate = SrsAudioSampleRate44100;
            break;
        default:
            break;
        };

        if ((err = write_audio_raw_frame((char *)sh.data(), (int)sh.length(), acodec, (uint32_t)dts)) != srs_success)
        {
            return srs_error_wrap(err, "write audio raw frame");
        }
    }

    return err;
}

srs_error_t Srs28181StreamCore::write_h264_sps_pps(uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;

    // h264 raw to h264 packet.
    std::string sh;
    if ((err = avc->mux_sequence_header(h264_sps, h264_pps, dts, pts, sh)) != srs_success)
    {
        return srs_error_wrap(err, "mux sequence header");
    }

    // h264 packet to flv packet.
    int8_t frame_type = SrsVideoAvcFrameTypeKeyFrame;
    int8_t avc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;
    char *flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success)
    {
        return srs_error_wrap(err, "mux avc to flv");
    }

    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    if ((err = rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv)) != srs_success)
    {
        return srs_error_wrap(err, "write packet");
    }

    return err;
}

srs_error_t Srs28181StreamCore::write_h264_ipb_frame(char *frame, int frame_size, uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;

    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);

    // for IDR frame, the frame is keyframe.
    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;
    if (nal_unit_type == SrsAvcNaluTypeIDR)
    {
        frame_type = SrsVideoAvcFrameTypeKeyFrame;
    }

    std::string ibp;
    if ((err = avc->mux_ipb_frame(frame, frame_size, ibp)) != srs_success)
    {
        return srs_error_wrap(err, "mux ibp frame");
    }

    int8_t avc_packet_type = SrsVideoAvcFrameTraitNALU;
    char *flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(ibp, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success)
    {
        return srs_error_wrap(err, "mux avc to flv");
    }

    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    return rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv);
}

srs_error_t Srs28181StreamCore::write_audio_raw_frame(char *frame, int frame_size, SrsRawAacStreamCodec *codec, uint32_t dts)
{
    srs_error_t err = srs_success;

    char *data = NULL;
    int size = 0;
    if ((err = aac->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success)
    {
        return srs_error_wrap(err, "mux aac to flv");
    }

    return rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
}

srs_error_t Srs28181StreamCore::rtmp_write_packet(char type, uint32_t timestamp, char *data, int size)
{
    srs_error_t err = srs_success;

    if ((err = connect()) != srs_success)
    {
        return srs_error_wrap(err, "connect");
    }

    SrsSharedPtrMessage *msg = NULL;

    if ((err = srs_rtmp_create_msg(type, timestamp, data, size, sdk->sid(), &msg)) != srs_success)
    {
        return srs_error_wrap(err, "create message");
    }
    srs_assert(msg);

    // send out encoded msg.
    if ((err = sdk->send_and_free_message(msg)) != srs_success)
    {
        close();
        return srs_error_wrap(err, "write message");
    }

    return err;
}

srs_error_t Srs28181StreamCore::connect()
{
    srs_error_t err = srs_success;

    // Ignore when connected.
    if (sdk)
    {
        return err;
    }

    // generate rtmp url to connect to.
    std::string url;
    //if (!req) {
    if (target_tcUrl != "")
    {
        std::string schema, host, vhost, app, param;
        int port;
        srs_discovery_tc_url(target_tcUrl, schema, host, vhost, app, stream_name, port, param);

        // generate output by template.
        std::string output = output_template;
        output = srs_string_replace(output, "[app]", app);
        output = srs_string_replace(output, "[stream]", stream_name);
        url = output;
    }

    // Fix Me: MUST use identified url in future
    url = target_tcUrl;

    srs_trace("28181 stream - target_tcurl:%s,stream_name:%s, url:%s",
              target_tcUrl.c_str(), stream_name.c_str(), url.c_str());

    // connect host.
    srs_utime_t cto = SRS_CONSTS_RTMP_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_PULSE;
    sdk = new SrsSimpleRtmpClient(url, cto, sto);

    if ((err = sdk->connect()) != srs_success)
    {
        close();
        return srs_error_wrap(err, "connect %s failed, cto=%dms, sto=%dms.", url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }

    // publish.
    if ((err = sdk->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success)
    {
        close();
        return srs_error_wrap(err, "publish %s failed", url.c_str());
    }

    return err;
}

void Srs28181StreamCore::close()
{
    h264_sps_changed = false;
    h264_pps_changed = false;
    h264_sps_pps_sent = false;
    srs_freep(sdk);
}

Srs28181TcpStreamConn::Srs28181TcpStreamConn(Srs28181TcpStreamListener *l, srs_netfd_t fd, std::string o)
{
}

Srs28181TcpStreamConn::~Srs28181TcpStreamConn()
{
}

srs_error_t Srs28181TcpStreamConn::init()
{
    srs_error_t err = srs_success;

    return err;
}

srs_error_t Srs28181TcpStreamConn::cycle()
{
    srs_error_t err = srs_success;

    return err;
}
