//
// Copyright (c) 2013-2022 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_PROTOCOL_CONN_HPP
#define SRS_PROTOCOL_CONN_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

// The resource managed by ISrsResourceManager.
class ISrsResource
{
public:
    ISrsResource();
    virtual ~ISrsResource();
public:
    // Get the context id of connection.
    virtual const SrsContextId& get_id() = 0;
public:
    // The resource description, optional.
    virtual std::string desc();
};

// The manager for resource.
class ISrsResourceManager
{
public:
    ISrsResourceManager();
    virtual ~ISrsResourceManager();
public:
    // Remove then free the specified connection.
    virtual void remove(ISrsResource* c) = 0;
};

// The connection interface for all HTTP/RTMP/RTSP object.
class ISrsConnection : public ISrsResource
{
public:
    ISrsConnection();
    virtual ~ISrsConnection();
public:
    // Get remote ip address.
    virtual std::string remote_ip() = 0;
};

// Lazy-sweep resource, never sweep util all wrappers are freed.
// See https://github.com/ossrs/srs/issues/3176#lazy-sweep
class ISrsLazyResource : public ISrsResource
{
private:
    // The reference count of resource, 0 is no wrapper and safe to sweep.
    int32_t gc_ref_;
    // The creator wrapper, which created this resource. Note that it might be disposed and the pointer is NULL, so be
    // careful and make sure to check it before use it.
    ISrsResource* gc_creator_wrapper_;
    // All available wrappers.
    std::vector<ISrsResource*> gc_wrappers_;
public:
    ISrsLazyResource();
    virtual ~ISrsLazyResource();
public:
    // For wrapper to use this resource.
    virtual ISrsLazyResource* gc_use(ISrsResource* wrapper);
    // For wrapper to dispose this resource.
    virtual ISrsLazyResource* gc_dispose(ISrsResource* wrapper);
    // The current reference count of resource.
    virtual int32_t gc_ref();
public:
    // Set the creator wrapper, from which resource clone wrapper.
    virtual void gc_set_creator_wrapper(ISrsResource* wrapper);
    // Get the first available wrapper. NULL if the creator wrapper disposed.
    virtual ISrsResource* gc_creator_wrapper();
    // Get the first available wrapper. NULL if all wrappers disposed.
    // It should be equal to the gc_creator_wrapper() if creator wrapper not disposed.
    virtual ISrsResource* gc_available_wrapper();
};

// The lazy-sweep GC, wait for a long time to dispose resource even when resource is disposable.
// See https://github.com/ossrs/srs/issues/3176#lazy-sweep
class ISrsLazyGc
{
public:
    ISrsLazyGc();
    virtual ~ISrsLazyGc();
public:
    // Remove then free the specified resource.
    virtual void remove(ISrsLazyResource* c) = 0;
};

#endif

