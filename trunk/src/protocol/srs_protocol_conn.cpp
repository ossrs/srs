//
// Copyright (c) 2013-2022 The SRS Authors
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
    gc_creator_wrapper_ = NULL;
}

SrsLazyObject::~SrsLazyObject()
{
}

SrsLazyObject* SrsLazyObject::gc_use()
{
    gc_ref_++;
    return this;
}

SrsLazyObject* SrsLazyObject::gc_dispose()
{
    gc_ref_--;
    return this;
}

int32_t SrsLazyObject::gc_ref()
{
    return gc_ref_;
}

void SrsLazyObject::gc_set_creator_wrapper(ISrsResource* wrapper)
{
    gc_creator_wrapper_ = wrapper;
}

ISrsResource* SrsLazyObject::gc_creator_wrapper()
{
    return gc_creator_wrapper_;
}

ISrsLazyGc::ISrsLazyGc()
{
}

ISrsLazyGc::~ISrsLazyGc()
{
}

