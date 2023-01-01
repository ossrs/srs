//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
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

SrsLazyObject::SrsLazyObject()
{
    gc_ref_ = 0;
}

SrsLazyObject::~SrsLazyObject()
{
}

void SrsLazyObject::gc_use()
{
    gc_ref_++;
}

void SrsLazyObject::gc_dispose()
{
    gc_ref_--;
}

int32_t SrsLazyObject::gc_ref()
{
    return gc_ref_;
}

ISrsLazyGc::ISrsLazyGc()
{
}

ISrsLazyGc::~ISrsLazyGc()
{
}

