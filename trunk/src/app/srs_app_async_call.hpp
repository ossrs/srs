//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_ASYNC_CALL_HPP
#define SRS_APP_ASYNC_CALL_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_app_st.hpp>

// HTTP hooks switch coroutines, so they run in their own coroutine: for example,
// while DVR calls a hook, the receive coroutine may get a video and pass it to DVR again.
// The caller is never blocked by the call.
class ISrsAsyncCallTask
{
public:
    ISrsAsyncCallTask();
    virtual ~ISrsAsyncCallTask();

public:
    virtual srs_error_t call() = 0;
    virtual std::string to_string() = 0;
};

class ISrsAsyncCallWorker
{
public:
    ISrsAsyncCallWorker();
    virtual ~ISrsAsyncCallWorker();

public:
    virtual srs_error_t execute(ISrsAsyncCallTask *t) = 0;
    virtual srs_error_t start() = 0;
    virtual void stop() = 0;
};

class SrsAsyncCallWorker : public ISrsCoroutineHandler, public ISrsAsyncCallWorker
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    std::vector<ISrsAsyncCallTask *> tasks_;
    srs_cond_t wait_;
    srs_mutex_t lock_;

public:
    SrsAsyncCallWorker();
    virtual ~SrsAsyncCallWorker();

public:
    virtual srs_error_t execute(ISrsAsyncCallTask *t);
    virtual int count();

public:
    virtual srs_error_t start();
    virtual void stop();

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void flush_tasks();
};

#endif
