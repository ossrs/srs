#include "srt_server.hpp"
#include "srt_handle.hpp"
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

srt_server::srt_server(unsigned short port):listen_port(port)
    ,server_socket(-1)
{
    handle_ptr = std::make_shared<srt_handle>();
}

srt_server::~srt_server()
{

}

int srt_server::init_srt() {
    if (server_socket != -1) {
        return -1;
    }

    server_socket = srt_create_socket();
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(listen_port);

    sockaddr* psa = (sockaddr*)&sa;

    int ret = srt_bind(server_socket, psa, sizeof(sa));
    if ( ret == SRT_ERROR )
    {
        srt_close(server_socket);
        srs_error("srt bind error: %d", ret);
        return -1;
    }

    ret = srt_listen(server_socket, 5);
    if (ret == SRT_ERROR)
    {
        srt_close(server_socket);
        srs_error("srt listen error: %d", ret);
        return -2;
    }

    _pollid = srt_epoll_create();
    if (_pollid < -1) {
        srs_error("srt server srt_epoll_create error, port=%d", listen_port);
        return -1;
    }

    int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
    ret = srt_epoll_add_usock(_pollid, server_socket, &events);
    if (ret < 0) {
        srs_error("srt server run add epoll error:%d", ret);
        return ret;
    }

    srs_trace("srt server listen port=%d, server_fd=%d", listen_port, server_socket);
    
    return 0;
}

int srt_server::start()
{
    int ret;

    if ((ret = init_srt()) < 0) {
        return ret;
    }
    ret = handle_ptr->start();
    if (ret < 0) {
        return ret;
    }

    run_flag = true;
    srs_trace("srt server is starting... port(%d)", listen_port);
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

    handle_ptr->stop();
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
                srs_trace("srt streamid(%s) error, fd:%d", streamid.c_str(), conn_fd);
                srt_close(conn_fd);
                return;
            }
            SRT_CONN_PTR srt_conn_ptr = std::make_shared<srt_conn>(conn_fd, streamid);

            std::string vhost_str = srt_conn_ptr->get_vhost();
            srs_trace("new srt connection streamid:%s, fd:%d, vhost:%s", 
                streamid.c_str(), conn_fd, vhost_str.c_str());
            SrsConfDirective* vhost_p = _srs_config->get_vhost(vhost_str, true);
            if (!vhost_p) {
                srs_trace("srt streamid(%s): no vhost %s, fd:%d", 
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
                srs_trace("stream mode error, it shoulde be m=push or m=pull, streamid:%s",
                    srt_conn_ptr->get_streamid().c_str());
                srt_conn_ptr->close();
                return;
            }
            request_message_t msg = {srt_conn_ptr, conn_event};
            handle_ptr->insert_message_queue(msg);
            break;
        }
        case SRTS_CONNECTED:
        {
            srs_trace("srt connected: socket=%d, mode:%s", input_fd, dscr.c_str());
            break;
        }
        case SRTS_BROKEN:
        {
            srt_epoll_remove_usock(_pollid, input_fd);
            srt_close(input_fd);
            srs_warn("srt close: socket=%d", input_fd);
            break;
        }
        default:
        {
            srs_error("srt server unkown status:%d", status);
        }
    }
}

void srt_server::on_work()
{
    const unsigned int SRT_FD_MAX = 100;
    srs_trace("srt server is working port(%d)", listen_port);
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
        srs_trace("srt server epoll get: ret=%d, rfd_num=%d, wfd_num=%d", 
            ret, rfd_num, wfd_num);

        for (int index = 0; index < rfd_num; index++) {
            SRT_SOCKSTATUS status = srt_getsockstate(read_fds[index]);
            srt_handle_connection(status, read_fds[index], "read fd");
        }
        
        for (int index = 0; index < wfd_num; index++) {
            SRT_SOCKSTATUS status = srt_getsockstate(write_fds[index]);
            srt_handle_connection(status, read_fds[index], "write fd");
        }
    }
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

    if(_srs_config->get_srt_enabled()) {
        srs_trace("srt server is enabled...");
        unsigned short srt_port = _srs_config->get_srt_listen_port();
        srs_trace("srt server listen port:%d", srt_port);
        err = srt2rtmp::get_instance()->init();
        if (err != srs_success) {
            srs_error_wrap(err, "srt start srt2rtmp error");
            return err;
        }

        srt_ptr = std::make_shared<srt_server>(srt_port);
        if (!srt_ptr) {
            srs_error_wrap(err, "srt listen %d", srt_port);
        }
    } else {
        srs_trace("srt server is disabled...");
    }

    return err;
}

srs_error_t SrtServerAdapter::run()
{
    srs_error_t err = srs_success;

    if(_srs_config->get_srt_enabled()) {
        srt_ptr->start();
    }

    return err;
}
