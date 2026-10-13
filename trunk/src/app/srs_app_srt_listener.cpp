//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_srt_listener.hpp>

using namespace std;

#include <srs_app_factory.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_protocol_st.hpp>

// How long the listener waits after an accept that fails, before it accepts again.
#define SRS_SRT_LISTENER_ERROR_INTERVAL (10 * SRS_UTIME_MILLISECONDS)

ISrsSrtHandler::ISrsSrtHandler()
{
}

ISrsSrtHandler::~ISrsSrtHandler()
{
}

ISrsSrtListener::ISrsSrtListener()
{
}

ISrsSrtListener::~ISrsSrtListener()
{
}

SrsSrtListener::SrsSrtListener(ISrsSrtHandler *h, std::string i, int p)
{
    handler_ = h;
    ip_ = i;
    port_ = p;

    lfd_ = srs_srt_socket_invalid();
    srt_skt_ = NULL;

    trd_ = new SrsDummyCoroutine();

    srt_eventloop_ = _srt_eventloop;
    app_factory_ = _srs_app_factory;
}

SrsSrtListener::~SrsSrtListener()
{
    srs_freep(trd_);
    srs_freep(srt_skt_);
    // TODO: FIXME: Handle error.
    srs_srt_close(lfd_);

    srt_eventloop_ = NULL;
    app_factory_ = NULL;
}

int SrsSrtListener::fd()
{
    return lfd_;
}

srs_error_t SrsSrtListener::create_socket()
{
    srs_error_t err = srs_success;
    if ((err = srs_srt_socket(&lfd_)) != srs_success) {
        return srs_error_wrap(err, "create_socket");
    }
    return err;
}

srs_error_t SrsSrtListener::listen()
{
    srs_error_t err = srs_success;

    if ((err = srs_srt_listen(lfd_, ip_, port_)) != srs_success) {
        return srs_error_wrap(err, "srs_srt_listen");
    }

    srt_skt_ = new SrsSrtSocket(srt_eventloop_->poller(), lfd_);
    // Accept never timeout.
    srt_skt_->set_recv_timeout(SRS_UTIME_NO_TIMEOUT);
    srt_skt_->set_send_timeout(SRS_UTIME_NO_TIMEOUT);

    srs_freep(trd_);
    // No context id, so the coroutine generates a new one when it starts.
    trd_ = app_factory_->create_coroutine("srt_listener", this, SrsContextId());
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start coroutine");
    }

    return err;
}

srs_error_t SrsSrtListener::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "srt listener");
        }

        // An accept that fails, such as of a caller that closed while it was queued, must not end the
        // listener, or no caller connects to the port again. Wait a little after it, so an error that
        // repeats doesn't spin.
        if ((err = do_cycle()) != srs_success) {
            srs_warn("srt listener: Ignore error, %s", srs_error_desc(err).c_str());
            srs_freep(err);
            srs_usleep(SRS_SRT_LISTENER_ERROR_INTERVAL);
        }
    }

    return err;
}

srs_error_t SrsSrtListener::do_cycle()
{
    srs_error_t err = srs_success;

    srs_srt_t client_srt_fd = srs_srt_socket_invalid();
    if ((err = srt_skt_->accept(&client_srt_fd)) != srs_success) {
        return srs_error_wrap(err, "srt accept");
    }

    // TODO: FIXME: print some log and client srt options.

    if ((err = handler_->on_srt_client(client_srt_fd)) != srs_success) {
        return srs_error_wrap(err, "handle srt fd=%d", client_srt_fd);
    }

    return err;
}
