//
// Copyright (c) 2013-2022 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_protocol_conn.hpp>

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

ISrsLazyResource::ISrsLazyResource()
{
    gc_ref_ = 0;
    gc_creator_wrapper_ = NULL;
}

ISrsLazyResource::~ISrsLazyResource()
{
}

ISrsLazyResource* ISrsLazyResource::gc_use(ISrsResource* wrapper)
{
    srs_assert(wrapper);
    if (std::find(gc_wrappers_.begin(), gc_wrappers_.end(), wrapper) == gc_wrappers_.end()) {
        gc_wrappers_.push_back(wrapper);
    }

    gc_ref_++;
    return this;
}

ISrsLazyResource* ISrsLazyResource::gc_dispose(ISrsResource* wrapper)
{
    srs_assert(wrapper);
    vector<ISrsResource*>::iterator it = std::find(gc_wrappers_.begin(), gc_wrappers_.end(), wrapper);
    if (it != gc_wrappers_.end()) {
        it = gc_wrappers_.erase(it);
    }

    gc_ref_--;
    return this;
}

int32_t ISrsLazyResource::gc_ref()
{
    return gc_ref_;
}

void ISrsLazyResource::gc_set_creator_wrapper(ISrsResource* wrapper)
{
    gc_creator_wrapper_ = wrapper;
}

ISrsResource* ISrsLazyResource::gc_creator_wrapper()
{
    return gc_creator_wrapper_;
}

ISrsResource* ISrsLazyResource::gc_available_wrapper()
{
    return gc_wrappers_.empty() ? NULL : gc_wrappers_.front();
}

ISrsLazyGc::ISrsLazyGc()
{
}

ISrsLazyGc::~ISrsLazyGc()
{
}

