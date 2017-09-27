/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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


