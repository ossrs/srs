//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_conn.hpp>

#include <srs_kernel_error.hpp>

#include <algorithm>
using namespace std;

ISrsResource::ISrsResource()
{
}

ISrsResource::~ISrsResource()
{
}

std::string ISrsResource::desc()
{
    return "Resource";
}

ISrsResourceManager::ISrsResourceManager()
{
}

ISrsResourceManager::~ISrsResourceManager()
{
}

ISrsConnection::ISrsConnection()
{
}

ISrsConnection::~ISrsConnection()
{
}

