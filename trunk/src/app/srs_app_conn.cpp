/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
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

#include <srs_app_conn.hpp>

#include <netinet/tcp.h>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_utility.hpp>

SrsConnection::SrsConnection(IConnectionManager* cm, srs_netfd_t c, string cip)
{
    manager = cm;
    stfd = c;
    ip = cip;
    create_time = srs_get_system_time_ms();
    
    skt = new SrsStSocket();
    kbps = new SrsKbps();
    kbps->set_io(skt, skt);
    
    trd = new SrsSTCoroutine("conn", this);
}

SrsConnection::~SrsConnection()
{
    dispose();
    
    srs_freep(kbps);
    srs_freep(skt);
    srs_freep(trd);
    
    srs_close_stfd(stfd);
}

void SrsConnection::resample()
{
    kbps->resample();
}

int64_t SrsConnection::get_send_bytes_delta()
{
    return kbps->get_send_bytes_delta();
}

int64_t SrsConnection::get_recv_bytes_delta()
{
    return kbps->get_recv_bytes_delta();
}

void SrsConnection::cleanup()
{
    kbps->cleanup();
}

void SrsConnection::dispose()
{
    trd->interrupt();
}

srs_error_t SrsConnection::start()
{
    srs_error_t err = srs_success;
    
    if ((err = skt->initialize(stfd)) != srs_success) {
        return srs_error_wrap(err, "init socket");
    }
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }
    
    return err;
}

srs_error_t SrsConnection::set_tcp_nodelay(bool v)
{
    srs_error_t err = srs_success;
    
    int r0 = 0;
    socklen_t nb_v = sizeof(int);
    int fd = srs_netfd_fileno(stfd);
    
    int ov = 0;
    if ((r0 = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &ov, &nb_v)) != 0) {
        return srs_error_new(ERROR_SOCKET_NO_NODELAY, "getsockopt fd=%d, r0=%d", fd, r0);
    }
    
#ifndef SRS_PERF_TCP_NODELAY
    srs_warn("ignore TCP_NODELAY, fd=%d, ov=%d", fd, ov);
    return err;
#endif
    
    int iv = (v? 1:0);
    if ((r0 = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &iv, nb_v)) != 0) {
        return srs_error_new(ERROR_SOCKET_NO_NODELAY, "setsockopt fd=%d, r0=%v", fd, r0);
    }
    if ((r0 = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &iv, &nb_v)) != 0) {
        return srs_error_new(ERROR_SOCKET_NO_NODELAY, "getsockopt fd=%d, r0=%d", fd, r0);
    }
    
    srs_trace("set fd=%d TCP_NODELAY %d=>%d", fd, ov, iv);
    
    return err;
}

srs_error_t SrsConnection::set_socket_buffer(int buffer_ms)
{
    srs_error_t err = srs_success;
    
    int r0 = 0;
    int fd = srs_netfd_fileno(stfd);
    socklen_t nb_v = sizeof(int);
    
    int ov = 0;
    if ((r0 = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &ov, &nb_v)) != 0) {
        return srs_error_new(ERROR_SOCKET_SNDBUF, "getsockopt fd=%d, r0=%d", fd, r0);
    }
    
#ifndef SRS_PERF_MW_SO_SNDBUF
    srs_warn("ignore SO_SNDBUF, fd=%d, ov=%d", fd, ov);
    return err;
#endif
    
    // the bytes:
    //      4KB=4096, 8KB=8192, 16KB=16384, 32KB=32768, 64KB=65536,
    //      128KB=131072, 256KB=262144, 512KB=524288
    // the buffer should set to sleep*kbps/8,
    // for example, your system delivery stream in 1000kbps,
    // sleep 800ms for small bytes, the buffer should set to:
    //      800*1000/8=100000B(about 128KB).
    // other examples:
    //      2000*3000/8=750000B(about 732KB).
    //      2000*5000/8=1250000B(about 1220KB).
    int kbps = 4000;
    int iv = buffer_ms * kbps / 8;
    
    // socket send buffer, system will double it.
    iv = iv / 2;
    
    // override the send buffer by macro.
#ifdef SRS_PERF_SO_SNDBUF_SIZE
    iv = SRS_PERF_SO_SNDBUF_SIZE / 2;
#endif
    
    // set the socket send buffer when required larger buffer
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &iv, nb_v) < 0) {
        return srs_error_new(ERROR_SOCKET_SNDBUF, "setsockopt fd=%d, r0=%v", fd, r0);
    }
    if ((r0 = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &iv, &nb_v)) != 0) {
        return srs_error_new(ERROR_SOCKET_SNDBUF, "getsockopt fd=%d, r0=%d", fd, r0);
    }
    
    srs_trace("set fd=%d, SO_SNDBUF=%d=>%d, buffer=%dms", fd, ov, iv, buffer_ms);
    
    return err;
}

srs_error_t SrsConnection::cycle()
{
    srs_error_t err = do_cycle();
    
    // Notify manager to remove it.
    manager->remove(this);
    
    // success.
    if (err == srs_success) {
        srs_trace("client finished.");
        return err;
    }
    
    // client close peer.
    // TODO: FIXME: Only reset the error when client closed it.
    if (srs_is_client_gracefully_close(srs_error_code(err))) {
        srs_warn("client disconnect peer. ret=%d", srs_error_code(err));
    } else {
        srs_error("connect error %s", srs_error_desc(err).c_str());
    }
    
    srs_freep(err);
    return srs_success;
}

int SrsConnection::srs_id()
{
    return trd->cid();
}

void SrsConnection::expire()
{
    trd->interrupt();
}


