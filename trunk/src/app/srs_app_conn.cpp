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

#include <srs_app_conn.hpp>

using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_utility.hpp>

IConnectionManager::IConnectionManager()
{
}

IConnectionManager::~IConnectionManager()
{
}

SrsConnection::SrsConnection(IConnectionManager* cm, st_netfd_t c, string cip)
{
    id = 0;
    manager = cm;
    stfd = c;
    ip = cip;
    disposed = false;
    expired = false;
    create_time = srs_get_system_time_ms();

    skt = new SrsStSocket();
    kbps = new SrsKbps();
    kbps->set_io(skt, skt);

    // the client thread should reap itself, 
    // so we never use joinable.
    // TODO: FIXME: maybe other thread need to stop it.
    // @see: https://github.com/ossrs/srs/issues/78
    pthread = new SrsOneCycleThread("conn", this);
}

SrsConnection::~SrsConnection()
{
    dispose();

    srs_freep(kbps);
    srs_freep(skt);
    srs_freep(pthread);
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
    if (disposed) {
        return;
    }
    
    disposed = true;
    
    /**
     * when delete the connection, stop the connection,
     * close the underlayer socket, delete the thread.
     */
    srs_close_stfd(stfd);
}

int SrsConnection::start()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = skt->initialize(stfd)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return pthread->start();
}

int SrsConnection::cycle()
{
    int ret = ERROR_SUCCESS;
    
    _srs_context->generate_id();
    id = _srs_context->get_id();
    
    int oret = ret = do_cycle();
    
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
        srs_warn("client disconnect peer. oret=%d, ret=%d", oret, ret);
    }

    return ERROR_SUCCESS;
}

void SrsConnection::on_thread_stop()
{
    // TODO: FIXME: never remove itself, use isolate thread to do cleanup.
    manager->remove(this);
}

int SrsConnection::srs_id()
{
    return id;
}

void SrsConnection::expire()
{
    expired = true;
}


