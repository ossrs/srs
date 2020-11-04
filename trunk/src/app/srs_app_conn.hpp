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

#ifndef SRS_APP_CONN_HPP
#define SRS_APP_CONN_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>
#include <map>

#include <srs_app_st.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_app_reload.hpp>
#include <srs_service_conn.hpp>

class SrsWallClock;

// Hooks for connection manager, to handle the event when disposing connections.
class ISrsDisposingHandler
{
public:
    ISrsDisposingHandler();
    virtual ~ISrsDisposingHandler();
public:
    // When before disposing resource, trigger when manager.remove(c), sync API.
    virtual void on_before_dispose(ISrsResource* c) = 0;
    // When disposing resource, async API, c is freed after it.
    virtual void on_disposing(ISrsResource* c) = 0;
};

// The resource manager remove resource and delete it asynchronously.
class SrsResourceManager : virtual public ISrsCoroutineHandler, virtual public ISrsResourceManager
{
private:
    std::string label_;
    SrsContextId cid_;
    bool verbose_;
private:
    SrsCoroutine* trd;
    srs_cond_t cond;
    // Callback handlers.
    std::vector<ISrsDisposingHandler*> handlers_;
    // Unsubscribing handlers, skip it for notifying.
    std::vector<ISrsDisposingHandler*> unsubs_;
    // Whether we are removing resources.
    bool removing_;
    // The zombie connections, we will delete it asynchronously.
    std::vector<ISrsResource*> zombies_;
    std::vector<ISrsResource*>* p_disposing_;
private:
    // The connections without any id.
    std::vector<ISrsResource*> conns_;
    // The connections with resource id.
    std::map<std::string, ISrsResource*> conns_id_;
    // The connections with resource name.
    std::map<std::string, ISrsResource*> conns_name_;
public:
    SrsResourceManager(const std::string& label, bool verbose = false);
    virtual ~SrsResourceManager();
public:
    srs_error_t start();
    bool empty();
    size_t size();
// Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();
public:
    void add(ISrsResource* conn);
    void add_with_id(const std::string& id, ISrsResource* conn);
    void add_with_name(const std::string& name, ISrsResource* conn);
    ISrsResource* at(int index);
    ISrsResource* find_by_id(std::string id);
    ISrsResource* find_by_name(std::string name);
public:
    void subscribe(ISrsDisposingHandler* h);
    void unsubscribe(ISrsDisposingHandler* h);
// Interface ISrsResourceManager
public:
    virtual void remove(ISrsResource* c);
private:
    void do_remove(ISrsResource* c);
    void clear();
    void do_clear();
    void dispose(ISrsResource* c);
};

// Interface for connection that is startable.
class ISrsStartableConneciton : virtual public ISrsConnection
    , virtual public ISrsStartable, virtual public ISrsKbpsDelta
{
public:
    ISrsStartableConneciton();
    virtual ~ISrsStartableConneciton();
};

// The basic connection of SRS, for TCP based protocols,
// all connections accept from listener must extends from this base class,
// server will add the connection to manager, and delete it when remove.
class SrsTcpConnection : virtual public ISrsStartableConneciton
    , virtual public ISrsReloadHandler, virtual public ISrsCoroutineHandler
{
protected:
    // Each connection start a green thread,
    // when thread stop, the connection will be delete by server.
    SrsCoroutine* trd;
    // The manager object to manage the connection.
    ISrsResourceManager* manager;
    // The underlayer st fd handler.
    srs_netfd_t stfd;
    // The ip and port of client.
    std::string ip;
    int port;
    // The underlayer socket.
    SrsStSocket* skt;
    // The connection total kbps.
    // not only the rtmp or http connection, all type of connection are
    // need to statistic the kbps of io.
    // The SrsStatistic will use it indirectly to statistic the bytes delta of current connection.
    SrsKbps* kbps;
    SrsWallClock* clk;
    // The create time in milliseconds.
    // for current connection to log self create time and calculate the living time.
    int64_t create_time;
public:
    SrsTcpConnection(ISrsResourceManager* cm, srs_netfd_t c, std::string cip, int cport);
    virtual ~SrsTcpConnection();
// Interface ISrsKbpsDelta
public:
    virtual void remark(int64_t* in, int64_t* out);
public:
    // To dipose the connection.
    virtual void dispose();
// Interface ISrsStartable
public:
    // Start the client green thread.
    // when server get a client from listener,
    // 1. server will create an concrete connection(for instance, RTMP connection),
    // 2. then add connection to its connection manager,
    // 3. start the client thread by invoke this start()
    // when client cycle thread stop, invoke the on_thread_stop(), which will use server
    // To remove the client by server->remove(this).
    virtual srs_error_t start();
public:
    // Set socket option TCP_NODELAY.
    virtual srs_error_t set_tcp_nodelay(bool v);
    // Set socket option SO_SNDBUF in srs_utime_t.
    virtual srs_error_t set_socket_buffer(srs_utime_t buffer_v);
// Interface ISrsOneCycleThreadHandler
public:
    // The thread cycle function,
    // when serve connection completed, terminate the loop which will terminate the thread,
    // thread will invoke the on_thread_stop() when it terminated.
    virtual srs_error_t cycle();
// Interface ISrsConnection.
public:
    virtual std::string remote_ip();
    virtual const SrsContextId& get_id();
public:
    // Set connection to expired.
    virtual void expire();
protected:
    // For concrete connection to do the cycle.
    virtual srs_error_t do_cycle() = 0;
};

#endif
