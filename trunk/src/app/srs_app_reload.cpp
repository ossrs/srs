//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_reload.hpp>

using namespace std;

#include <srs_kernel_error.hpp>

ISrsReloadHandler::ISrsReloadHandler()
{
}

ISrsReloadHandler::~ISrsReloadHandler()
{
}

// LCOV_EXCL_START
srs_error_t ISrsReloadHandler::on_reload_vhost_chunk_size(string /*vhost*/)
{
    return srs_success;
}
// LCOV_EXCL_STOP
