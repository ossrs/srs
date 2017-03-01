/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

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

#include <srs_app_st.hpp>

#include <string>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_log.hpp>

namespace internal
{
    ISrsThreadHandler::ISrsThreadHandler()
    {
    }
    
    ISrsThreadHandler::~ISrsThreadHandler()
    {
    }
    
    void ISrsThreadHandler::on_thread_start()
    {
    }
    
    int ISrsThreadHandler::on_before_cycle()
    {
        int ret = ERROR_SUCCESS;
        return ret;
    }
    
    int ISrsThreadHandler::on_end_cycle()
    {
        int ret = ERROR_SUCCESS;
        return ret;
    }
    
    void ISrsThreadHandler::on_thread_stop()
    {
    }
    
    SrsThread::SrsThread(const char* n, ISrsThreadHandler* h, int64_t ims, bool j)
    {
        name = n;
        handler = h;
        cims = ims;
        
        trd = NULL;
        loop = false;
        context_id = -1;
        joinable = j;
    }
    
    SrsThread::~SrsThread()
    {
        stop();
    }
    
    int SrsThread::cid()
    {
        return context_id;
    }
    
    int SrsThread::start()
    {
        int ret = ERROR_SUCCESS;
        
        if(trd) {
            srs_info("thread %s already running.", _name);
            return ret;
        }
        
        loop = true;
        
        if((trd = st_thread_create(pfn, this, (joinable? 1:0), 0)) == NULL){
            ret = ERROR_ST_CREATE_CYCLE_THREAD;
            srs_error("st_thread_create failed. ret=%d", ret);
            return ret;
        }
        
        return ret;
    }
    
    void SrsThread::stop()
    {
        if (!trd) {
            return;
        }
        
        // notify the cycle to stop loop.
        loop = false;
        
        // the interrupt will cause the socket to read/write error,
        // which will terminate the cycle thread.
        st_thread_interrupt(trd);
        
        // when joinable, wait util quit.
        if (joinable) {
            // wait the thread to exit.
            int ret = st_thread_join(trd, NULL);
            srs_assert(ret == ERROR_SUCCESS);
        }
        
        trd = NULL;
    }
    
    bool SrsThread::can_loop()
    {
        return loop;
    }
    
    void SrsThread::stop_loop()
    {
        loop = false;
    }
    
    void SrsThread::cycle()
    {
        int ret = ERROR_SUCCESS;
        
        // TODO: FIXME: it's better for user to specifies the cid,
        //      because sometimes we need to merge cid, for example,
        //      the publish thread should use the same cid of connection.
        _srs_context->generate_id();
        srs_info("thread %s cycle start", name);
        context_id = _srs_context->get_id();
        
        srs_assert(handler);
        handler->on_thread_start();
        
        while (loop) {
            if ((ret = handler->on_before_cycle()) != ERROR_SUCCESS) {
                srs_warn("thread %s on before cycle failed, ignored and retry, ret=%d", name, ret);
                goto failed;
            }
            srs_info("thread %s on before cycle success", _name);
            
            if ((ret = handler->cycle()) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret) && !srs_is_system_control_error(ret)) {
                    srs_warn("thread %s cycle failed, ignored and retry, ret=%d", name, ret);
                }
                goto failed;
            }
            srs_info("thread %s cycle success", _name);
            
            if ((ret = handler->on_end_cycle()) != ERROR_SUCCESS) {
                srs_warn("thread %s on end cycle failed, ignored and retry, ret=%d", name, ret);
                goto failed;
            }
            srs_info("thread %s on end cycle success", _name);
            
        failed:
            if (!loop) {
                break;
            }
            
            // Should never use no timeout, just ignore it.
            // to improve performance, donot sleep when interval is zero.
            // @see: https://github.com/ossrs/srs/issues/237
            if (cims != 0 && cims != SRS_CONSTS_NO_TMMS) {
                st_usleep(cims * 1000);
            }
        }
        
        srs_info("thread %s cycle finished", name);
        // @remark in this callback, user may delete this, so never use this->xxx anymore.
        handler->on_thread_stop();
    }
    
    void* SrsThread::pfn(void* arg)
    {
        SrsThread* obj = (SrsThread*)arg;
        srs_assert(obj);
        
        obj->cycle();
        
        // delete cid for valgrind to detect memory leak.
        SrsThreadContext* ctx = dynamic_cast<SrsThreadContext*>(_srs_context);
        if (ctx) {
            ctx->clear_cid();
        }
        
        st_thread_exit(NULL);
        
        return NULL;
    }
}

SrsStSocket::SrsStSocket(st_netfd_t client_stfd)
{
    stfd = client_stfd;
    stm = rtm = SRS_CONSTS_NO_TMMS;
    rbytes = sbytes = 0;
}

SrsStSocket::~SrsStSocket()
{
}

bool SrsStSocket::is_never_timeout(int64_t tm)
{
    return tm == SRS_CONSTS_NO_TMMS;
}

void SrsStSocket::set_recv_timeout(int64_t tm)
{
    rtm = tm;
}

int64_t SrsStSocket::get_recv_timeout()
{
    return rtm;
}

void SrsStSocket::set_send_timeout(int64_t tm)
{
    stm = tm;
}

int64_t SrsStSocket::get_send_timeout()
{
    return stm;
}

int64_t SrsStSocket::get_recv_bytes()
{
    return rbytes;
}

int64_t SrsStSocket::get_send_bytes()
{
    return sbytes;
}

int SrsStSocket::read(void* buf, size_t size, ssize_t* nread)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_read;
    if (rtm == SRS_CONSTS_NO_TMMS) {
        nb_read = st_read(stfd, buf, size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_read = st_read(stfd, buf, size, rtm * 1000);
    }
    
    if (nread) {
        *nread = nb_read;
    }
    
    // On success a non-negative integer indicating the number of bytes actually read is returned
    // (a value of 0 means the network connection is closed or end of file is reached).
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_read <= 0) {
        // @see https://github.com/ossrs/srs/issues/200
        if (nb_read < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        if (nb_read == 0) {
            errno = ECONNRESET;
        }
        
        return ERROR_SOCKET_READ;
    }
    
    rbytes += nb_read;
    
    return ret;
}

int SrsStSocket::read_fully(void* buf, size_t size, ssize_t* nread)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_read;
    if (rtm == SRS_CONSTS_NO_TMMS) {
        nb_read = st_read_fully(stfd, buf, size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_read = st_read_fully(stfd, buf, size, rtm * 1000);
    }
    
    if (nread) {
        *nread = nb_read;
    }
    
    // On success a non-negative integer indicating the number of bytes actually read is returned
    // (a value less than nbyte means the network connection is closed or end of file is reached)
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_read != (ssize_t)size) {
        // @see https://github.com/ossrs/srs/issues/200
        if (nb_read < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        if (nb_read >= 0) {
            errno = ECONNRESET;
        }
        
        return ERROR_SOCKET_READ_FULLY;
    }
    
    rbytes += nb_read;
    
    return ret;
}

int SrsStSocket::write(void* buf, size_t size, ssize_t* nwrite)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_write;
    if (stm == SRS_CONSTS_NO_TMMS) {
        nb_write = st_write(stfd, buf, size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_write = st_write(stfd, buf, size, stm * 1000);
    }
    
    if (nwrite) {
        *nwrite = nb_write;
    }
    
    // On success a non-negative integer equal to nbyte is returned.
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_write <= 0) {
        // @see https://github.com/ossrs/srs/issues/200
        if (nb_write < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        return ERROR_SOCKET_WRITE;
    }
    
    sbytes += nb_write;
    
    return ret;
}

int SrsStSocket::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_write;
    if (stm == SRS_CONSTS_NO_TMMS) {
        nb_write = st_writev(stfd, iov, iov_size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_write = st_writev(stfd, iov, iov_size, stm * 1000);
    }
    
    if (nwrite) {
        *nwrite = nb_write;
    }
    
    // On success a non-negative integer equal to nbyte is returned.
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_write <= 0) {
        // @see https://github.com/ossrs/srs/issues/200
        if (nb_write < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        return ERROR_SOCKET_WRITE;
    }
    
    sbytes += nb_write;
    
    return ret;
}

SrsTcpClient::SrsTcpClient(string h, int p, int64_t tm)
{
    io = NULL;
    stfd = NULL;
    
    host = h;
    port = p;
    timeout = tm;
}

SrsTcpClient::~SrsTcpClient()
{
    close();
}

int SrsTcpClient::connect()
{
    int ret = ERROR_SUCCESS;
    
    close();
    
    srs_assert(stfd == NULL);
    if ((ret = srs_socket_connect(host, port, timeout, &stfd)) != ERROR_SUCCESS) {
        srs_error("connect tcp://%s:%d failed, to=%"PRId64"ms. ret=%d", host.c_str(), port, timeout, ret);
        return ret;
    }
    
    srs_assert(io == NULL);
    io = new SrsStSocket(stfd);
    
    return ret;
}

void SrsTcpClient::close()
{
    // Ignore when already closed.
    if (!io) {
        return;
    }
    
    srs_freep(io);
    srs_close_stfd(stfd);
}

bool SrsTcpClient::is_never_timeout(int64_t tm)
{
    return io->is_never_timeout(tm);
}

void SrsTcpClient::set_recv_timeout(int64_t tm)
{
    io->set_recv_timeout(tm);
}

int64_t SrsTcpClient::get_recv_timeout()
{
    return io->get_recv_timeout();
}

void SrsTcpClient::set_send_timeout(int64_t tm)
{
    io->set_send_timeout(tm);
}

int64_t SrsTcpClient::get_send_timeout()
{
    return io->get_send_timeout();
}

int64_t SrsTcpClient::get_recv_bytes()
{
    return io->get_recv_bytes();
}

int64_t SrsTcpClient::get_send_bytes()
{
    return io->get_send_bytes();
}

int SrsTcpClient::read(void* buf, size_t size, ssize_t* nread)
{
    return io->read(buf, size, nread);
}

int SrsTcpClient::read_fully(void* buf, size_t size, ssize_t* nread)
{
    return io->read_fully(buf, size, nread);
}

int SrsTcpClient::write(void* buf, size_t size, ssize_t* nwrite)
{
    return io->write(buf, size, nwrite);
}

int SrsTcpClient::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    return io->writev(iov, iov_size, nwrite);
}

#ifdef __linux__
#include <sys/epoll.h>

bool srs_st_epoll_is_supported(void)
{
    struct epoll_event ev;

    ev.events = EPOLLIN;
    ev.data.ptr = NULL;
    /* Guaranteed to fail */
    epoll_ctl(-1, EPOLL_CTL_ADD, -1, &ev);

    return (errno != ENOSYS);
}
#endif

int srs_st_init()
{
    int ret = ERROR_SUCCESS;
    
#ifdef __linux__
    // check epoll, some old linux donot support epoll.
    // @see https://github.com/ossrs/srs/issues/162
    if (!srs_st_epoll_is_supported()) {
        ret = ERROR_ST_SET_EPOLL;
        srs_error("epoll required on Linux. ret=%d", ret);
        return ret;
    }
#endif
    
    // Select the best event system available on the OS. In Linux this is
    // epoll(). On BSD it will be kqueue.
    if (st_set_eventsys(ST_EVENTSYS_ALT) == -1) {
        ret = ERROR_ST_SET_EPOLL;
        srs_error("st_set_eventsys use %s failed. ret=%d", st_get_eventsys_name(), ret);
        return ret;
    }
    srs_info("st_set_eventsys to %s", st_get_eventsys_name());

    if(st_init() != 0){
        ret = ERROR_ST_INITIALIZE;
        srs_error("st_init failed. ret=%d", ret);
        return ret;
    }
    srs_trace("st_init success, use %s", st_get_eventsys_name());
    
    return ret;
}

void srs_close_stfd(st_netfd_t& stfd)
{
    if (stfd) {
        // we must ensure the close is ok.
        int err = st_netfd_close(stfd);
        srs_assert(err != -1);
        stfd = NULL;
    }
}

