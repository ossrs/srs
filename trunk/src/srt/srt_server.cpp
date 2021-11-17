//
// Copyright (c) 2013-2021 Runner365
//
// SPDX-License-Identifier: MIT
//

#include "srt_server.hpp"
#include "srt_handle.hpp"
#include "srt_log.hpp"
#include <srt/udt.h>
#include <thread>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdexcept>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_config.hpp>
#include <srs_app_utility.hpp>

srt_server::srt_server(unsigned short port):_listen_port(port)
    ,_server_socket(-1)
{
}

srt_server::~srt_server()
{

}

int srt_server::init_srt_parameter() {
    const int DEF_RECV_LATENCY = 120;
    const int DEF_PEER_LATENCY = 0;

    int opt_len = sizeof(int);

    if (_server_socket == -1) {
        return -1;
    }
    int maxbw = _srs_config->get_srto_maxbw();
    srt_setsockopt(_server_socket, 0, SRTO_MAXBW, &maxbw, opt_len);
    int mss = _srs_config->get_srto_mss();
    srt_setsockopt(_server_socket, 0, SRTO_MSS, &mss, opt_len);

    bool tlpkdrop = _srs_config->get_srto_tlpkdrop();
    int tlpkdrop_i = tlpkdrop ? 1 : 0;
    srt_setsockopt(_server_socket, 0, SRTO_TLPKTDROP, &tlpkdrop_i, opt_len);

    int connection_timeout = _srs_config->get_srto_conntimeout();
    srt_setsockopt(_server_socket, 0, SRTO_CONNTIMEO, &connection_timeout, opt_len);
    
    int send_buff = _srs_config->get_srto_sendbuf();
    srt_setsockopt(_server_socket, 0, SRTO_SNDBUF, &send_buff, opt_len);
    int recv_buff = _srs_config->get_srto_recvbuf();
    srt_setsockopt(_server_socket, 0, SRTO_RCVBUF, &recv_buff, opt_len);
    int payload_size = _srs_config->get_srto_payloadsize();
    srt_setsockopt(_server_socket, 0, SRTO_PAYLOADSIZE, &payload_size, opt_len);

    int latency = _srs_config->get_srto_latency();
    if (DEF_RECV_LATENCY != latency) {
        srt_setsockopt(_server_socket, 0, SRTO_LATENCY, &latency, opt_len);
    }
    
    int recv_latency = _srs_config->get_srto_recv_latency();
    if (DEF_RECV_LATENCY != recv_latency) {
        srt_setsockopt(_server_socket, 0, SRTO_RCVLATENCY, &recv_latency, opt_len);
    }
    
    int peer_latency = _srs_config->get_srto_peer_latency();
    if (DEF_PEER_LATENCY != peer_latency) {
        srt_setsockopt(_server_socket, 0, SRTO_PEERLATENCY, &recv_latency, opt_len);
    }
    
    srt_log_trace("init srt parameter, maxbw:%d, mss:%d, tlpkdrop:%d, connect timeout:%d, \
send buff:%d, recv buff:%d, payload size:%d, latency:%d, recv latency:%d, peer latency:%d",
        maxbw, mss, tlpkdrop, connection_timeout, send_buff, recv_buff, payload_size,
        latency, recv_latency, peer_latency);
    return 0;
}

void srt_server::init_srt_log() {
    SrsLogLevel level = srs_get_log_level(_srs_config->get_log_level());
    switch (level) {
        case SrsLogLevelInfo:
        {
            set_srt_log_level(SRT_LOGGER_INFO_LEVEL);
            break;
        }
        case SrsLogLevelTrace:
        {
            set_srt_log_level(SRT_LOGGER_TRACE_LEVEL);
            break;
        }
        case SrsLogLevelWarn:
        {
            set_srt_log_level(SRT_LOGGER_WARN_LEVEL);
            break;
        }
        case SrsLogLevelError:
        {
            set_srt_log_level(SRT_LOGGER_ERROR_LEVEL);
            break;
        }
        default:
        {
            set_srt_log_level(SRT_LOGGER_TRACE_LEVEL);
        }
    }
    return;
}

int srt_server::init_srt() {
    if (_server_socket != -1) {
        return -1;
    }

    init_srt_log();
    
    _server_socket = srt_create_socket();
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(_listen_port);

    sockaddr* psa = (sockaddr*)&sa;

    int ret = srt_bind(_server_socket, psa, sizeof(sa));
    if ( ret == SRT_ERROR )
    {
        srt_close(_server_socket);
        srt_log_error("srt bind error: %d", ret);
        return -1;
    }

    ret = srt_listen(_server_socket, 5);
    if (ret == SRT_ERROR)
    {
        srt_close(_server_socket);
        srt_log_error("srt listen error: %d", ret);
        return -2;
    }

    init_srt_parameter();

    _pollid = srt_epoll_create();
    if (_pollid < -1) {
        srt_log_error("srt server srt_epoll_create error, port=%d", _listen_port);
        return -1;
    }
    _handle_ptr = std::make_shared<srt_handle>(_pollid);

    int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
    ret = srt_epoll_add_usock(_pollid, _server_socket, &events);
    if (ret < 0) {
        srt_log_error("srt server run add epoll error:%d", ret);
        return ret;
    }

    srt_log_trace("srt server listen port=%d, server_fd=%d", _listen_port, _server_socket);
    
    return 0;
}

int srt_server::start()
{
    int ret;

    if ((ret = init_srt()) < 0) {
        return ret;
    }

    run_flag = true;
    srt_log_trace("srt server is starting... port(%d)", _listen_port);
    thread_run_ptr = std::make_shared<std::thread>(&srt_server::on_work, this);
    return 0;
}

void srt_server::stop()
{
    run_flag = false;
    if (!thread_run_ptr) {
        return;
    }
    thread_run_ptr->join();

    return;
}

void srt_server::srt_handle_connection(SRT_SOCKSTATUS status, SRTSOCKET input_fd, const std::string& dscr) {
    SRTSOCKET conn_fd = -1;
    sockaddr_in scl;
    int sclen = sizeof(scl);
    int conn_event;// = SRT_EPOLL_IN |SRT_EPOLL_OUT| SRT_EPOLL_ERR;
    
    switch(status) {
        case SRTS_LISTENING:
        {
            conn_fd = srt_accept(input_fd, (sockaddr*)&scl, &sclen);
            if (conn_fd == -1) {
                return;
            }
            //add new srt connect into srt handle
            std::string streamid = UDT::getstreamid(conn_fd);
            if (!is_streamid_valid(streamid)) {
                srt_log_trace("srt streamid(%s) error, fd:%d", streamid.c_str(), conn_fd);
                srt_close(conn_fd);
                return;
            }
            SRT_CONN_PTR srt_conn_ptr = std::make_shared<srt_conn>(conn_fd, streamid);

            std::string vhost_str = srt_conn_ptr->get_vhost();
            srt_log_trace("new srt connection streamid:%s, fd:%d, vhost:%s", 
                streamid.c_str(), conn_fd, vhost_str.c_str());
            SrsConfDirective* vhost_p = _srs_config->get_vhost(vhost_str, true);
            if (!vhost_p) {
                srt_log_trace("srt streamid(%s): no vhost %s, fd:%d", 
                    streamid.c_str(), vhost_str.c_str(), conn_fd);
                srt_conn_ptr->close();
                return;
            }
            if (srt_conn_ptr->get_mode() == PULL_SRT_MODE) {
                //add SRT_EPOLL_IN for information notify
                conn_event = SRT_EPOLL_IN | SRT_EPOLL_ERR;//not inlucde SRT_EPOLL_OUT for save cpu
            } else if (srt_conn_ptr->get_mode() == PUSH_SRT_MODE) {
                conn_event = SRT_EPOLL_IN | SRT_EPOLL_ERR;
            } else {
                srt_log_trace("stream mode error, it should be m=push or m=pull, streamid:%s",
                    srt_conn_ptr->get_streamid().c_str());
                srt_conn_ptr->close();
                return;
            }
            
            _handle_ptr->add_newconn(srt_conn_ptr, conn_event);
            break;
        }
        case SRTS_CONNECTED:
        {
            srt_log_trace("srt connected: socket=%d, mode:%s", input_fd, dscr.c_str());
            break;
        }
        case SRTS_BROKEN:
        {
            srt_epoll_remove_usock(_pollid, input_fd);
            srt_close(input_fd);
            srt_log_warn("srt close: socket=%d", input_fd);
            break;
        }
        default:
        {
            srt_log_error("srt server unkown status:%d", status);
        }
    }
}

void srt_server::srt_handle_data(SRT_SOCKSTATUS status, SRTSOCKET input_fd, const std::string& dscr) {
    _handle_ptr->handle_srt_socket(status, input_fd);
    return;
}

void srt_server::on_work()
{
    const unsigned int SRT_FD_MAX = 100;
    srt_log_trace("srt server is working port(%d)", _listen_port);
    while (run_flag)
    {
        SRTSOCKET read_fds[SRT_FD_MAX];
        SRTSOCKET write_fds[SRT_FD_MAX];
        int rfd_num = SRT_FD_MAX;
        int wfd_num = SRT_FD_MAX;

        int ret = srt_epoll_wait(_pollid, read_fds, &rfd_num, write_fds, &wfd_num, -1,
                        nullptr, nullptr, nullptr, nullptr);
        if (ret < 0) {
            continue;
        }
        _handle_ptr->check_alive();

        for (int index = 0; index < rfd_num; index++) {
            SRT_SOCKSTATUS status = srt_getsockstate(read_fds[index]);
            if (_server_socket == read_fds[index]) {
                srt_handle_connection(status, read_fds[index], "read fd");
            } else {
                srt_handle_data(status, read_fds[index], "read fd");
            }
        }
        
        for (int index = 0; index < wfd_num; index++) {
            SRT_SOCKSTATUS status = srt_getsockstate(write_fds[index]);
            if (_server_socket == write_fds[index]) {
                srt_handle_connection(status, write_fds[index], "write fd");
            } else {
                srt_handle_data(status, write_fds[index], "write fd");
            }
        }
    }

    // New API at 2020-01-28, >1.4.1
    // @see https://github.com/Haivision/srt/commit/b8c70ec801a56bea151ecce9c09c4ebb720c2f68#diff-fb66028e8746fea578788532533a296bR786
#if (SRT_VERSION_MAJOR<<24 | SRT_VERSION_MINOR<<16 | SRT_VERSION_PATCH<<8) > 0x01040100
    srt_epoll_clear_usocks(_pollid);
#endif
}

SrtServerAdapter::SrtServerAdapter()
{
}

SrtServerAdapter::~SrtServerAdapter()
{
}

srs_error_t SrtServerAdapter::initialize()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: We could fork processes here, because here only ST is initialized.

    return err;
}

srs_error_t SrtServerAdapter::run(SrsWaitGroup* wg)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: We could start a coroutine to dispatch SRT task to processes.

    if(_srs_config->get_srt_enabled()) {
        srt_log_trace("srt server is enabled...");
        unsigned short srt_port = _srs_config->get_srt_listen_port();
        srt_log_trace("srt server listen port:%d", srt_port);
        err = srt2rtmp::get_instance()->init();
        if (err != srs_success) {
            return srs_error_wrap(err, "srt start srt2rtmp error");
        }

        srt_ptr = std::make_shared<srt_server>(srt_port);
        if (!srt_ptr) {
            return srs_error_wrap(err, "srt listen %d", srt_port);
        }
    } else {
        srt_log_trace("srt server is disabled...");
    }

    if(_srs_config->get_srt_enabled()) {
        srt_ptr->start();
    }

    return err;
}

void SrtServerAdapter::stop()
{
    // TODO: FIXME: If forked processes, we should do cleanup.
}
