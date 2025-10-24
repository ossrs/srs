//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_SERVICE_HPP
#define SRS_UTEST_SERVICE_HPP

/*
#include <srs_utest_manual_service.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_listener.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_resource.hpp>
#include <srs_protocol_conn.hpp>

class MockSrsConnection : public ISrsConnection
{
public:
    // Whether switch the coroutine context when free the object, for special case test.
    bool do_switch;

public:
    MockSrsConnection();
    virtual ~MockSrsConnection();
    // Interface ISrsConnection.
public:
    virtual const SrsContextId &get_id();
    virtual std::string desc();
    virtual std::string remote_ip();
};

class MockTcpHandler : public ISrsTcpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_netfd_t fd;

public:
    MockTcpHandler();
    virtual ~MockTcpHandler();

public:
    virtual srs_error_t on_tcp_client(ISrsListener *listener, srs_netfd_t stfd);
};

class MockOnCycleThread : public ISrsCoroutineHandler
{
public:
    SrsSTCoroutine trd;
    srs_cond_t cond;
    MockOnCycleThread();
    virtual ~MockOnCycleThread();
    virtual srs_error_t cycle();
    srs_error_t start();
    void stop();
};

class MockOnCycleThread2 : public ISrsCoroutineHandler
{
public:
    SrsSTCoroutine trd;
    srs_mutex_t lock;
    MockOnCycleThread2();
    virtual ~MockOnCycleThread2();
    virtual srs_error_t cycle();
    srs_error_t start();
    void stop();
};

class MockOnCycleThread3 : public ISrsCoroutineHandler
{
public:
    SrsSTCoroutine trd;
    srs_netfd_t fd;
    MockOnCycleThread3();
    virtual ~MockOnCycleThread3();
    virtual srs_error_t cycle();
    virtual srs_error_t start(std::string ip, int port);
    virtual srs_error_t do_cycle(srs_netfd_t cfd);
    void stop();
};

class MockConnectionManager : public ISrsResourceManager
{
public:
    MockConnectionManager();
    virtual ~MockConnectionManager();
    virtual srs_error_t start();
    virtual bool empty();
    virtual size_t size();
    virtual void add(ISrsResource *conn, bool *exists = NULL);
    virtual void add_with_id(const std::string &id, ISrsResource *conn);
    virtual void add_with_fast_id(uint64_t id, ISrsResource *conn);
    virtual void add_with_name(const std::string &name, ISrsResource *conn);
    virtual ISrsResource *at(int index);
    virtual ISrsResource *find_by_id(std::string id);
    virtual ISrsResource *find_by_fast_id(uint64_t id);
    virtual ISrsResource *find_by_name(std::string name);
    virtual void remove(ISrsResource *c);
    virtual void subscribe(ISrsDisposingHandler *h);
    virtual void unsubscribe(ISrsDisposingHandler *h);
    virtual void dispose();
};

class MockStopSelfThread : public ISrsCoroutineHandler
{
public:
    int r0;
    int r1;
    SrsFastCoroutine trd;
    MockStopSelfThread();
    virtual ~MockStopSelfThread();
    srs_error_t start();
    void stop();
    virtual srs_error_t cycle();
};

class MockAsyncReaderThread : public ISrsCoroutineHandler
{
public:
    SrsFastCoroutine trd;
    srs_netfd_t fd;
    MockAsyncReaderThread(srs_netfd_t v);
    virtual ~MockAsyncReaderThread();
    srs_error_t start();
    void stop();
    virtual srs_error_t cycle();
};

#endif
