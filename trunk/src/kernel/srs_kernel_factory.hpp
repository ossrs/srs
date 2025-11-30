//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_FACTORY_HPP
#define SRS_KERNEL_FACTORY_HPP

#include <srs_core.hpp>

#include <srs_core_time.hpp>
#include <srs_kernel_st.hpp>

#include <string>

// The config for kernel and protocol objects.
class ISrsConfig
{
public:
    ISrsConfig();
    virtual ~ISrsConfig();

public:
    virtual srs_utime_t get_pithy_print() = 0;
    virtual std::string get_default_app_name() = 0;
    // Get the default mode for short streamid format.
    // @return "publish" or "request", default is "request".
    virtual std::string get_srt_default_mode() = 0;
};

// The factory to create kernel objects.
class ISrsKernelFactory
{
public:
    ISrsKernelFactory();
    virtual ~ISrsKernelFactory();

public:
    virtual ISrsCoroutine *create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid) = 0;
    virtual ISrsTime *create_time() = 0;
    virtual ISrsConfig *create_config() = 0;
    virtual ISrsCond *create_cond() = 0;
};

extern ISrsKernelFactory *_srs_kernel_factory;

#endif
