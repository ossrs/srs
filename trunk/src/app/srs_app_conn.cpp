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

#include <srs_app_conn.hpp>

#include <netinet/tcp.h>
#include <algorithm>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_service_log.hpp>

ISrsDisposingHandler::ISrsDisposingHandler()
{
}

ISrsDisposingHandler::~ISrsDisposingHandler()
{
}

SrsResourceManager::SrsResourceManager(const std::string& label, bool verbose)
{
    verbose_ = verbose;
    label_ = label;
    cond = srs_cond_new();
    trd = NULL;
}

SrsResourceManager::~SrsResourceManager()
{
    if (trd) {
        srs_cond_signal(cond);
        trd->stop();

        srs_freep(trd);
        srs_cond_destroy(cond);
    }

    clear();
}

srs_error_t SrsResourceManager::start()
{
    srs_error_t err = srs_success;

    cid_ = _srs_context->generate_id();
    trd = new SrsSTCoroutine("manager", this, cid_);

    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "conn manager");
    }

    return err;
}

bool SrsResourceManager::empty()
{
    return conns_.empty();
}

size_t SrsResourceManager::size()
{
    return conns_.size();
}

srs_error_t SrsResourceManager::cycle()
{
    srs_error_t err = srs_success;

    srs_trace("%s connection manager run", label_.c_str());

    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "conn manager");
        }

        // Clear all zombies, because we may switch context and lost signal
        // when we clear zombie connection.
        while (!zombies_.empty()) {
            clear();
        }

        srs_cond_wait(cond);
    }

    return err;
}

void SrsResourceManager::add(ISrsResource* conn)
{
    if (std::find(conns_.begin(), conns_.end(), conn) == conns_.end()) {
        conns_.push_back(conn);
    }
}

void SrsResourceManager::add_with_id(const std::string& id, ISrsResource* conn)
{
    add(conn);
    conns_id_[id] = conn;
}

void SrsResourceManager::add_with_name(const std::string& name, ISrsResource* conn)
{
    add(conn);
    conns_name_[name] = conn;
}

ISrsResource* SrsResourceManager::at(int index)
{
    return (index < (int)conns_.size())? conns_.at(index) : NULL;
}

ISrsResource* SrsResourceManager::find_by_id(std::string id)
{
    map<string, ISrsResource*>::iterator it = conns_id_.find(id);
    return (it != conns_id_.end())? it->second : NULL;
}

ISrsResource* SrsResourceManager::find_by_name(std::string name)
{
    map<string, ISrsResource*>::iterator it = conns_name_.find(name);
    return (it != conns_name_.end())? it->second : NULL;
}

void SrsResourceManager::subscribe(ISrsDisposingHandler* h)
{
    if (std::find(handlers_.begin(), handlers_.end(), h) == handlers_.end()) {
        handlers_.push_back(h);
    }
}

void SrsResourceManager::unsubscribe(ISrsDisposingHandler* h)
{
    vector<ISrsDisposingHandler*>::iterator it = find(handlers_.begin(), handlers_.end(), h);
    if (it != handlers_.end()) {
        handlers_.erase(it);
    }
}

void SrsResourceManager::remove(ISrsResource* c)
{
    SrsContextRestore(cid_);
    if (verbose_) {
        _srs_context->set_id(c->get_id());
        srs_trace("before dispose resource(%s), zombies=%d", c->desc().c_str(), (int)zombies_.size());
    }

    if (std::find(zombies_.begin(), zombies_.end(), c) == zombies_.end()) {
        zombies_.push_back(c);
    }

    for (int i = 0; i < (int)handlers_.size(); i++) {
        ISrsDisposingHandler* h = handlers_.at(i);
        h->on_before_dispose(c);
    }

    srs_cond_signal(cond);
}

void SrsResourceManager::clear()
{
    if (zombies_.empty()) {
        return;
    }

    SrsContextRestore(cid_);
    if (verbose_) {
        srs_trace("clear zombies=%d connections", (int)zombies_.size());
    }

    // To prevent thread switch when delete connection,
    // we copy all connections then free one by one.
    vector<ISrsResource*> copy;
    copy.swap(zombies_);

    vector<ISrsResource*>::iterator it;
    for (it = copy.begin(); it != copy.end(); ++it) {
        ISrsResource* conn = *it;

        if (verbose_) {
            _srs_context->set_id(conn->get_id());
            srs_trace("disposing resource(%s), zombies=%d/%d", conn->desc().c_str(),
                (int)copy.size(), (int)zombies_.size());
        }

        dispose(conn);
    }
}

void SrsResourceManager::dispose(ISrsResource* c)
{
    for (map<string, ISrsResource*>::iterator it = conns_name_.begin(); it != conns_name_.end();) {
        if (c != it->second) {
            ++it;
        } else {
            // Use C++98 style: https://stackoverflow.com/a/4636230
            conns_name_.erase(it++);
        }
    }

    for (map<string, ISrsResource*>::iterator it = conns_id_.begin(); it != conns_id_.end();) {
        if (c != it->second) {
            ++it;
        } else {
            // Use C++98 style: https://stackoverflow.com/a/4636230
            conns_id_.erase(it++);
        }
    }

    vector<ISrsResource*>::iterator it = std::find(conns_.begin(), conns_.end(), c);
    if (it != conns_.end()) {
        conns_.erase(it);
    }

    for (int i = 0; i < (int)handlers_.size(); i++) {
        ISrsDisposingHandler* h = handlers_.at(i);
        h->on_disposing(c);
    }

    srs_freep(c);
}

SrsTcpConnection::SrsTcpConnection(ISrsResourceManager* cm, srs_netfd_t c, string cip, int cport)
{
    manager = cm;
    stfd = c;
    ip = cip;
    port = cport;
    create_time = srsu2ms(srs_get_system_time());
    
    skt = new SrsStSocket();
    clk = new SrsWallClock();
    kbps = new SrsKbps(clk);
    kbps->set_io(skt, skt);
    
    trd = new SrsSTCoroutine("conn", this);
}

SrsTcpConnection::~SrsTcpConnection()
{
    dispose();
    
    srs_freep(kbps);
    srs_freep(clk);
    srs_freep(skt);
    srs_freep(trd);
    
    srs_close_stfd(stfd);
}

void SrsTcpConnection::remark(int64_t* in, int64_t* out)
{
    kbps->remark(in, out);
}

void SrsTcpConnection::dispose()
{
    trd->interrupt();
}

srs_error_t SrsTcpConnection::start()
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

srs_error_t SrsTcpConnection::set_tcp_nodelay(bool v)
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
        return srs_error_new(ERROR_SOCKET_NO_NODELAY, "setsockopt fd=%d, r0=%d", fd, r0);
    }
    if ((r0 = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &iv, &nb_v)) != 0) {
        return srs_error_new(ERROR_SOCKET_NO_NODELAY, "getsockopt fd=%d, r0=%d", fd, r0);
    }
    
    srs_trace("set fd=%d TCP_NODELAY %d=>%d", fd, ov, iv);
    
    return err;
}

srs_error_t SrsTcpConnection::set_socket_buffer(srs_utime_t buffer_v)
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
    int iv = srsu2ms(buffer_v) * kbps / 8;
    
    // socket send buffer, system will double it.
    iv = iv / 2;
    
    // override the send buffer by macro.
#ifdef SRS_PERF_SO_SNDBUF_SIZE
    iv = SRS_PERF_SO_SNDBUF_SIZE / 2;
#endif
    
    // set the socket send buffer when required larger buffer
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &iv, nb_v) < 0) {
        return srs_error_new(ERROR_SOCKET_SNDBUF, "setsockopt fd=%d, r0=%d", fd, r0);
    }
    if ((r0 = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &iv, &nb_v)) != 0) {
        return srs_error_new(ERROR_SOCKET_SNDBUF, "getsockopt fd=%d, r0=%d", fd, r0);
    }
    
    srs_trace("set fd=%d, SO_SNDBUF=%d=>%d, buffer=%dms", fd, ov, iv, srsu2ms(buffer_v));
    
    return err;
}

srs_error_t SrsTcpConnection::cycle()
{
    srs_error_t err = do_cycle();
    
    // Notify manager to remove it.
    manager->remove(this);
    
    // success.
    if (err == srs_success) {
        srs_trace("client finished.");
        return err;
    }

    // It maybe success with message.
    if (srs_error_code(err) == ERROR_SUCCESS) {
        srs_trace("client finished%s.", srs_error_summary(err).c_str());
        srs_freep(err);
        return err;
    }
    
    // client close peer.
    // TODO: FIXME: Only reset the error when client closed it.
    if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. ret=%d", srs_error_code(err));
    } else if (srs_is_server_gracefully_close(err)) {
        srs_warn("server disconnect. ret=%d", srs_error_code(err));
    } else {
        srs_error("serve error %s", srs_error_desc(err).c_str());
    }
    
    srs_freep(err);
    return srs_success;
}

SrsContextId SrsTcpConnection::srs_id()
{
    return trd->cid();
}

string SrsTcpConnection::remote_ip()
{
    return ip;
}

const SrsContextId& SrsTcpConnection::get_id()
{
    return trd->cid();
}

void SrsTcpConnection::expire()
{
    trd->interrupt();
}


