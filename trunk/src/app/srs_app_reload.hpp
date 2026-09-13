//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RELOAD_HPP
#define SRS_APP_RELOAD_HPP

#include <srs_core.hpp>

#include <string>

// The main purpose of reload is for quick application during performance optimization.
// So, we only need to keep a basic framework.
class ISrsReloadHandler
{
public:
    ISrsReloadHandler();
    virtual ~ISrsReloadHandler();

public:
    virtual srs_error_t on_reload_vhost_chunk_size(std::string vhost);
};

#endif
