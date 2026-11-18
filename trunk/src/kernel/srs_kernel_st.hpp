//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_ST_HPP
#define SRS_KERNEL_ST_HPP

#include <srs_core.hpp>

class ISrsCoroutineHandler
{
public:
    ISrsCoroutineHandler();
    virtual ~ISrsCoroutineHandler();

public:
    // The coroutine ends when this returns. A cycle with its own loop must check pull() in each
    // round; a stop or interrupt wakes it from a blocking ST call such as st_read().
    virtual srs_error_t cycle() = 0;
};

class ISrsStartable
{
public:
    ISrsStartable();
    virtual ~ISrsStartable();

public:
    virtual srs_error_t start() = 0;
};

class ISrsInterruptable
{
public:
    ISrsInterruptable();
    virtual ~ISrsInterruptable();

public:
    virtual void interrupt() = 0;
    virtual srs_error_t pull() = 0;
};

class ISrsContextIdSetter
{
public:
    ISrsContextIdSetter();
    virtual ~ISrsContextIdSetter();

public:
    virtual void set_cid(const SrsContextId &cid) = 0;
};

class ISrsContextIdGetter
{
public:
    ISrsContextIdGetter();
    virtual ~ISrsContextIdGetter();

public:
    virtual const SrsContextId &cid() = 0;
};

class ISrsStoppable
{
public:
    ISrsStoppable();
    virtual ~ISrsStoppable();

public:
    virtual void stop() = 0;
};

class ISrsCoroutine : public ISrsStoppable, public ISrsStartable, public ISrsInterruptable, public ISrsContextIdSetter, public ISrsContextIdGetter
{
public:
    ISrsCoroutine();
    virtual ~ISrsCoroutine();
};

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
