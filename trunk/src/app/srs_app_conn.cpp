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
#include <srs_app_log.hpp>

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
    p_disposing_ = NULL;
    removing_ = false;
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

    // Restore the handler from unsubscribing handlers.
    vector<ISrsDisposingHandler*>::iterator it;
    if ((it = std::find(unsubs_.begin(), unsubs_.end(), h)) != unsubs_.end()) {
        unsubs_.erase(it);
    }
}

void SrsResourceManager::unsubscribe(ISrsDisposingHandler* h)
{
    vector<ISrsDisposingHandler*>::iterator it = find(handlers_.begin(), handlers_.end(), h);
    if (it != handlers_.end()) {
        handlers_.erase(it);
    }

    // Put it to the unsubscribing handlers.
    if (std::find(unsubs_.begin(), unsubs_.end(), h) == unsubs_.end()) {
        unsubs_.push_back(h);
    }
}

void SrsResourceManager::remove(ISrsResource* c)
{
    removing_ = true;
    do_remove(c);
    removing_ = false;
}

void SrsResourceManager::do_remove(ISrsResource* c)
{
    SrsContextRestore(cid_);
    if (verbose_) {
        _srs_context->set_id(c->get_id());
        srs_trace("before dispose resource(%s), zombies=%d", c->desc().c_str(), (int)zombies_.size());
    }

    // Only notify when not removed(in zombies_).
    vector<ISrsResource*>::iterator it = std::find(zombies_.begin(), zombies_.end(), c);
    if (it != zombies_.end()) {
        return;
    }

    // Also ignore when we are disposing it.
    if (p_disposing_) {
        it = std::find(p_disposing_->begin(), p_disposing_->end(), c);
        if (it != p_disposing_->end()) {
            return;
        }
    }

    // Push to zombies, we will free it in another coroutine.
    zombies_.push_back(c);

    // We should copy all handlers, because it may change during callback.
    vector<ISrsDisposingHandler*> handlers = handlers_;

    // Notify other handlers to handle the before-dispose event.
    for (int i = 0; i < (int)handlers.size(); i++) {
        ISrsDisposingHandler* h = handlers.at(i);

        // Ignore if handler is unsubscribing.
        if (!unsubs_.empty() && std::find(unsubs_.begin(), unsubs_.end(), h) != unsubs_.end()) {
            srs_warn2(TAG_RESOURCE_UNSUB, "ignore before-dispose for %p", h);
            continue;
        }

        h->on_before_dispose(c);
    }

    // Notify the coroutine to free it.
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

    // Clear all unsubscribing handlers, if not removing any resource.
    if (!removing_ && !unsubs_.empty()) {
        vector<ISrsDisposingHandler*>().swap(unsubs_);
    }

    do_clear();

    // Reset it for it points to a local object.
    p_disposing_ = NULL;
}

void SrsResourceManager::do_clear()
{
    // To prevent thread switch when delete connection,
    // we copy all connections then free one by one.
    vector<ISrsResource*> copy;
    copy.swap(zombies_);
    p_disposing_ = &copy;

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

    // We should copy all handlers, because it may change during callback.
    vector<ISrsDisposingHandler*> handlers = handlers_;

    // Notify other handlers to handle the disposing event.
    for (int i = 0; i < (int)handlers.size(); i++) {
        ISrsDisposingHandler* h = handlers.at(i);

        // Ignore if handler is unsubscribing.
        if (!unsubs_.empty() && std::find(unsubs_.begin(), unsubs_.end(), h) != unsubs_.end()) {
            srs_warn2(TAG_RESOURCE_UNSUB, "ignore disposing for %p", h);
            continue;
        }

        h->on_disposing(c);
    }

    srs_freep(c);
}

ISrsExpire::ISrsExpire()
{
}

ISrsExpire::~ISrsExpire()
{
}

ISrsStartableConneciton::ISrsStartableConneciton()
{
}

ISrsStartableConneciton::~ISrsStartableConneciton()
{
}

SrsTcpConnection::SrsTcpConnection(srs_netfd_t c)
{
    stfd = c;
    skt = new SrsStSocket();
}

SrsTcpConnection::~SrsTcpConnection()
{
    srs_freep(skt);
    srs_close_stfd(stfd);
}

srs_error_t SrsTcpConnection::initialize()
{
    srs_error_t err = srs_success;

    if ((err = skt->initialize(stfd)) != srs_success) {
        return srs_error_wrap(err, "init socket");
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

void SrsTcpConnection::set_recv_timeout(srs_utime_t tm)
{
    skt->set_recv_timeout(tm);
}

srs_utime_t SrsTcpConnection::get_recv_timeout()
{
    return skt->get_recv_timeout();
}

srs_error_t SrsTcpConnection::read_fully(void* buf, size_t size, ssize_t* nread)
{
    return skt->read_fully(buf, size, nread);
}

int64_t SrsTcpConnection::get_recv_bytes()
{
    return skt->get_recv_bytes();
}

int64_t SrsTcpConnection::get_send_bytes()
{
    return skt->get_send_bytes();
}

srs_error_t SrsTcpConnection::read(void* buf, size_t size, ssize_t* nread)
{
    return skt->read(buf, size, nread);
}

void SrsTcpConnection::set_send_timeout(srs_utime_t tm)
{
    skt->set_send_timeout(tm);
}

srs_utime_t SrsTcpConnection::get_send_timeout()
{
    return skt->get_send_timeout();
}

srs_error_t SrsTcpConnection::write(void* buf, size_t size, ssize_t* nwrite)
{
    return skt->write(buf, size, nwrite);
}

srs_error_t SrsTcpConnection::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    return skt->writev(iov, iov_size, nwrite);
}

