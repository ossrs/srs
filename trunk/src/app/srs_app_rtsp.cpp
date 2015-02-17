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

ISrsRtspHandler::ISrsRtspHandler()
{
}

ISrsRtspHandler::~ISrsRtspHandler()
{
}

SrsRtspConn::SrsRtspConn(SrsRtspCaster* c, st_netfd_t fd, std::string o, int lpmin, int lpmax)
{
    output = o;
    local_port_min = lpmin;
    local_port_max = lpmax;

    session = "O9EaZ4bf"; // TODO: FIXME: generate session id.

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
            if ((ret = rtsp->send_message(new SrsRtspOptionsResponse(req->seq))) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("rtsp: send OPTIONS response failed. ret=%d", ret);
                }
                return ret;
            }
        } else if (req->is_announce()) {
            srs_assert(req->sdp);
            sps = req->sdp->video_sps;
            pps = req->sdp->video_pps;
            asc = req->sdp->audio_sh;
            srs_trace("rtsp: video(#%s, %s), audio(#%s, %s, %sHZ %schannels)", 
                req->sdp->video_stream_id.c_str(), req->sdp->video_codec.c_str(),
                req->sdp->audio_stream_id.c_str(), req->sdp->audio_codec.c_str(), 
                req->sdp->audio_sample_rate.c_str(), req->sdp->audio_channel.c_str()
            );
            if ((ret = rtsp->send_message(new SrsRtspResponse(req->seq))) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("rtsp: send ANNOUNCE response failed. ret=%d", ret);
                }
                return ret;
            }
        } else if (req->is_setup()) {
            srs_assert(req->transport);
            SrsRtspSetupResponse* res = new SrsRtspSetupResponse(req->seq);
            res->client_port_min = req->transport->client_port_min;
            res->client_port_max = req->transport->client_port_max;
            res->local_port_min = local_port_min;
            res->local_port_max = local_port_max;
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
}

int SrsRtspCaster::serve_client(st_netfd_t stfd)
{
    int ret = ERROR_SUCCESS;

    SrsRtspConn* conn = new SrsRtspConn(
        this, stfd, 
        output, local_port_min, local_port_max
    );

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

