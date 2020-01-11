#include "srt_server.hpp"
#include <thread>
#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_config.hpp>

srt_server::srt_server(unsigned short port):listen_port(port)
{

}

srt_server::~srt_server()
{

}

int srt_server::start()
{
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
    return;
}

void srt_server::on_work()
{
    srs_trace("srt server is working port(%d)", listen_port);
    while (run_flag)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
