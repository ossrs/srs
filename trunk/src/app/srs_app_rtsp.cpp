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

#include <srs_app_rtsp.hpp>

#include <algorithm>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_rtsp_stack.hpp>
#include <srs_app_st_socket.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>

#ifdef SRS_AUTO_STREAM_CASTER

SrsRtpConn::SrsRtpConn(SrsRtspConn* r, int p)
{
    rtsp = r;
    _port = p;
    listener = new SrsUdpListener(this, p);
}

SrsRtpConn::~SrsRtpConn()
{
    srs_freep(listener);
}

int SrsRtpConn::port()
{
    return _port;
}

int SrsRtpConn::listen()
{
    return listener->listen();
}

int SrsRtpConn::on_udp_packet(sockaddr_in* from, char* buf, int nb_buf)
{
    int ret = ERROR_SUCCESS;
    return ret;
}

SrsRtspConn::SrsRtspConn(SrsRtspCaster* c, st_netfd_t fd, std::string o)
{
    output = o;

    session = "";
    video_rtp = NULL;
    audio_rtp = NULL;

    caster = c;
    stfd = fd;
    skt = new SrsStSocket(fd);
    rtsp = new SrsRtspStack(skt);
    trd = new SrsThread("rtsp", this, 0, false);
}

SrsRtspConn::~SrsRtspConn()
{
    srs_close_stfd(stfd);
    trd->stop();

    srs_freep(video_rtp);
    srs_freep(audio_rtp);

    srs_freep(trd);
    srs_freep(skt);
    srs_freep(rtsp);
}

int SrsRtspConn::serve()
{
    return trd->start();
}

int SrsRtspConn::do_cycle()
{
    int ret = ERROR_SUCCESS;

    // retrieve ip of client.
    std::string ip = srs_get_peer_ip(st_netfd_fileno(stfd));
    srs_trace("rtsp: serve %s", ip.c_str());

    // consume all rtsp messages.
    for (;;) {
        SrsRtspRequest* req = NULL;
        if ((ret = rtsp->recv_message(&req)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("rtsp: recv request failed. ret=%d", ret);
            }
            return ret;
        }
        SrsAutoFree(SrsRtspRequest, req);
        srs_info("rtsp: got rtsp request");

        if (req->is_options()) {
            SrsRtspOptionsResponse* res = new SrsRtspOptionsResponse(req->seq);
            res->session = session;
            if ((ret = rtsp->send_message(res)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("rtsp: send OPTIONS response failed. ret=%d", ret);
                }
                return ret;
            }
        } else if (req->is_announce()) {
            srs_assert(req->sdp);
            video_id = req->sdp->video_stream_id;
            audio_id = req->sdp->audio_stream_id;
            sps = req->sdp->video_sps;
            pps = req->sdp->video_pps;
            asc = req->sdp->audio_sh;
            srs_trace("rtsp: video(#%s, %s), audio(#%s, %s, %sHZ %schannels)", 
                req->sdp->video_stream_id.c_str(), req->sdp->video_codec.c_str(),
                req->sdp->audio_stream_id.c_str(), req->sdp->audio_codec.c_str(), 
                req->sdp->audio_sample_rate.c_str(), req->sdp->audio_channel.c_str()
            );

            SrsRtspResponse* res = new SrsRtspResponse(req->seq);
            res->session = session;
            if ((ret = rtsp->send_message(res)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("rtsp: send ANNOUNCE response failed. ret=%d", ret);
                }
                return ret;
            }
        } else if (req->is_setup()) {
            srs_assert(req->transport);
            int lpm = 0;
            if ((ret = caster->alloc_port(&lpm)) != ERROR_SUCCESS) {
                srs_error("rtsp: alloc port failed. ret=%d", ret);
                return ret;
            }

            SrsRtpConn* rtp = NULL;
            if (req->stream_id == video_id) {
                srs_freep(video_rtp);
                rtp = video_rtp = new SrsRtpConn(this, lpm);
            } else {
                srs_freep(audio_rtp);
                rtp = audio_rtp = new SrsRtpConn(this, lpm);
            }
            if ((ret = rtp->listen()) != ERROR_SUCCESS) {
                srs_error("rtsp: rtp listen at port=%d failed. ret=%d", lpm, ret);
                return ret;
            }
            srs_trace("rtsp: rtp listen at port=%d ok.", lpm);

            // create session.
            if (session.empty()) {
                session = "O9EaZ4bf"; // TODO: FIXME: generate session id.
            }

            SrsRtspSetupResponse* res = new SrsRtspSetupResponse(req->seq);
            res->client_port_min = req->transport->client_port_min;
            res->client_port_max = req->transport->client_port_max;
            res->local_port_min = lpm;
            res->local_port_max = lpm + 1;
            res->session = session;
            if ((ret = rtsp->send_message(res)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("rtsp: send SETUP response failed. ret=%d", ret);
                }
                return ret;
            }
        } else if (req->is_record()) {
            SrsRtspResponse* res = new SrsRtspResponse(req->seq);
            res->session = session;
            if ((ret = rtsp->send_message(res)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("rtsp: send SETUP response failed. ret=%d", ret);
                }
                return ret;
            }
        }
    }

    return ret;
}

int SrsRtspConn::cycle()
{
    // serve the rtsp client.
    int ret = do_cycle();
    
    // if socket io error, set to closed.
    if (srs_is_client_gracefully_close(ret)) {
        ret = ERROR_SOCKET_CLOSED;
    }
    
    // success.
    if (ret == ERROR_SUCCESS) {
        srs_trace("client finished.");
    }
    
    // client close peer.
    if (ret == ERROR_SOCKET_CLOSED) {
        srs_warn("client disconnect peer. ret=%d", ret);
    }

    // terminate thread in the thread cycle itself.
    trd->stop_loop();

    return ERROR_SUCCESS;
}

void SrsRtspConn::on_thread_stop()
{
    if (video_rtp) {
        caster->free_port(video_rtp->port(), video_rtp->port() + 1);
    }

    if (audio_rtp) {
        caster->free_port(audio_rtp->port(), audio_rtp->port() + 1);
    }

    caster->remove(this);
}

SrsRtspCaster::SrsRtspCaster(SrsConfDirective* c)
{
    // TODO: FIXME: support reload.
    output = _srs_config->get_stream_caster_output(c);
    local_port_min = _srs_config->get_stream_caster_rtp_port_min(c);
    local_port_max = _srs_config->get_stream_caster_rtp_port_max(c);
}

SrsRtspCaster::~SrsRtspCaster()
{
    std::vector<SrsRtspConn*>::iterator it;
    for (it = clients.begin(); it != clients.end(); ++it) {
        SrsRtspConn* conn = *it;
        srs_freep(conn);
    }
    clients.clear();
    used_ports.clear();
}

int SrsRtspCaster::alloc_port(int* pport)
{
    int ret = ERROR_SUCCESS;

    // use a pair of port.
    for (int i = local_port_min; i < local_port_max - 1; i += 2) {
        if (!used_ports[i]) {
            used_ports[i] = true;
            used_ports[i + 1] = true;
            *pport = i;
            break;
        }
    }
    srs_info("rtsp: alloc port=%d-%d", *pport, *pport + 1);

    return ret;
}

void SrsRtspCaster::free_port(int lpmin, int lpmax)
{
    for (int i = lpmin; i < lpmax; i++) {
        used_ports[i] = false;
    }
    srs_trace("rtsp: free rtp port=%d-%d", lpmin, lpmax);
}

int SrsRtspCaster::on_tcp_client(st_netfd_t stfd)
{
    int ret = ERROR_SUCCESS;

    SrsRtspConn* conn = new SrsRtspConn(this, stfd, output);

    if ((ret = conn->serve()) != ERROR_SUCCESS) {
        srs_error("rtsp: serve client failed. ret=%d", ret);
        srs_freep(conn);
        return ret;
    }

    clients.push_back(conn);
    srs_info("rtsp: start thread to serve client.");

    return ret;
}

void SrsRtspCaster::remove(SrsRtspConn* conn)
{
    std::vector<SrsRtspConn*>::iterator it = find(clients.begin(), clients.end(), conn);
    if (it != clients.end()) {
        clients.erase(it);
    }
    srs_info("rtsp: remove connection from caster.");

    srs_freep(conn);
}

#endif

