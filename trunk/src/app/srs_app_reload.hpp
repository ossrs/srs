//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RELOAD_HPP
#define SRS_APP_RELOAD_HPP

#include <srs_core.hpp>

#include <string>

// Reload is kept mainly to apply config changes quickly while tuning performance,
// so only a basic framework remains.
class ISrsReloadHandler
{
public:
    ISrsReloadHandler();
    virtual ~ISrsReloadHandler();

public:
    virtual srs_error_t on_reload_vhost_chunk_size(std::string vhost);
};

#endif
