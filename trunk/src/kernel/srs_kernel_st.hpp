//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_ST_HPP
#define SRS_KERNEL_ST_HPP

#include <srs_core.hpp>

// Each ST-coroutine must implements this interface,
// to do the cycle job and handle some events.
//
// Thread do a job then terminated normally, it's a SrsOneCycleThread:
//      class SrsOneCycleThread : public ISrsCoroutineHandler {
//          public: ISrsCoroutine trd;
//          public: virtual srs_error_t cycle() {
//              // Do something, then return this cycle and thread terminated normally.
//          }
//      };
//
// Thread has its inside loop, such as the RTMP receive thread:
//      class SrsReceiveThread : public ISrsCoroutineHandler {
//          public: ISrsCoroutine* trd;
//          public: virtual srs_error_t cycle() {
//              while (true) {
//                  // Check whether thread interrupted.
//                  if ((err = trd->pull()) != srs_success) {
//                      return err;
//                  }
//                  // Do something, such as st_read() packets, it'll be wakeup
//                  // when user stop or interrupt the thread.
//              }
//          }
//      };
class ISrsCoroutineHandler
{
public:
    ISrsCoroutineHandler();
    virtual ~ISrsCoroutineHandler();

public:
    // Do the work. The ST-coroutine will terminated normally if it returned.
    // @remark If the cycle has its own loop, it must check the thread pull.
    virtual srs_error_t cycle() = 0;
};

// Start the object, generally a coroutine.
class ISrsStartable
{
public:
    ISrsStartable();
    virtual ~ISrsStartable();

public:
    virtual srs_error_t start() = 0;
};

// Allow user to interrupt the coroutine, for example, to stop it.
class ISrsInterruptable
{
public:
    ISrsInterruptable();
    virtual ~ISrsInterruptable();

public:
    virtual void interrupt() = 0;
    virtual srs_error_t pull() = 0;
};

// Get the context id.
class ISrsContextIdSetter
{
public:
    ISrsContextIdSetter();
    virtual ~ISrsContextIdSetter();

public:
    virtual void set_cid(const SrsContextId &cid) = 0;
};

// Set the context id.
class ISrsContextIdGetter
{
public:
    ISrsContextIdGetter();
    virtual ~ISrsContextIdGetter();

public:
    virtual const SrsContextId &cid() = 0;
};

// Stop the object, generally a coroutine.
class ISrsStoppable
{
public:
    ISrsStoppable();
    virtual ~ISrsStoppable();

public:
    virtual void stop() = 0;
};

// The coroutine object.
class ISrsCoroutine : public ISrsStoppable, public ISrsStartable, public ISrsInterruptable, public ISrsContextIdSetter, public ISrsContextIdGetter
{
public:
    ISrsCoroutine();
    virtual ~ISrsCoroutine();
};

// The condition variable interface.
class ISrsCond
{
public:
    ISrsCond();
    virtual ~ISrsCond();

public:
    virtual int wait() = 0;
    virtual int timedwait(srs_utime_t timeout) = 0;
    virtual int signal() = 0;
    virtual int broadcast() = 0;
};

#endif
