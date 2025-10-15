//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_RESOURCE_HPP
#define SRS_KERNEL_RESOURCE_HPP

#include <srs_core.hpp>

#include <srs_core_autofree.hpp>
#include <srs_kernel_st.hpp>

#include <map>
#include <string>
#include <vector>

class ISrsResource;
class ISrsCond;

// Hooks for connection manager, to handle the event when disposing connections.
class ISrsDisposingHandler
{
public:
    ISrsDisposingHandler();
    virtual ~ISrsDisposingHandler();

public:
    // When before disposing resource, trigger when manager.remove(c), sync API.
    // @remark Recommend to unref c, after this, no other objects refs to c.
    virtual void on_before_dispose(ISrsResource *c) = 0;
    // When disposing resource, async API, c is freed after it.
    // @remark Recommend to stop any thread/timer of c, after this, fields of c is able
    // to be deleted in any order.
    virtual void on_disposing(ISrsResource *c) = 0;
};

// The item to identify the fast id object.
class SrsResourceFastIdItem
{
public:
    // If available, use the resource in item.
    bool available_;
    // How many resource have the same fast-id, which contribute a collision.
    int nn_collisions_;
    // The first fast-id of resources.
    uint64_t fast_id_;
    // The first resource object.
    ISrsResource *impl_;

public:
    SrsResourceFastIdItem()
    {
        available_ = false;
        nn_collisions_ = 0;
        fast_id_ = 0;
        impl_ = NULL;
    }
};

// The resource managed by ISrsResourceManager.
class ISrsResource
{
public:
    ISrsResource();
    virtual ~ISrsResource();

public:
    // Get the context id of connection.
    virtual const SrsContextId &get_id() = 0;

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
    // Start the resource manager.
    virtual srs_error_t start() = 0;
    // Check if the resource manager is empty.
    virtual bool empty() = 0;
    // Get the number of resources.
    virtual size_t size() = 0;

public:
    // Add a resource to the manager.
    virtual void add(ISrsResource *conn, bool *exists = NULL) = 0;
    // Add a resource with string id to the manager.
    virtual void add_with_id(const std::string &id, ISrsResource *conn) = 0;
    // Add a resource with fast(int) id to the manager.
    virtual void add_with_fast_id(uint64_t id, ISrsResource *conn) = 0;
    // Get resource at specified index.
    virtual ISrsResource *at(int index) = 0;
    // Find resource by string id.
    virtual ISrsResource *find_by_id(std::string id) = 0;
    // Find resource by fast(int) id.
    virtual ISrsResource *find_by_fast_id(uint64_t id) = 0;
    // Find resource by name.
    virtual ISrsResource *find_by_name(std::string name) = 0;
    // Add a resource with name to the manager.
    virtual void add_with_name(const std::string &name, ISrsResource *conn) = 0;

public:
    // Remove then free the specified connection. Note that the manager always free c resource,
    // in the same coroutine or another coroutine. Some manager may support add c to a map, it
    // should always free it even if it's in the map.
    virtual void remove(ISrsResource *c) = 0;

public:
    // Subscribe the handler to be notified when before-dispose and disposing.
    virtual void subscribe(ISrsDisposingHandler *h) = 0;
    virtual void unsubscribe(ISrsDisposingHandler *h) = 0;
};

// The resource manager remove resource and delete it asynchronously.
class SrsResourceManager : public ISrsCoroutineHandler, public ISrsResourceManager
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string label_;
    SrsContextId cid_;
    bool verbose_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    ISrsCond *cond_;
    // Callback handlers.
    std::vector<ISrsDisposingHandler *> handlers_;
    // Unsubscribing handlers, skip it for notifying.
    std::vector<ISrsDisposingHandler *> unsubs_;
    // Whether we are removing resources.
    bool removing_;
    // The zombie connections, we will delete it asynchronously.
    std::vector<ISrsResource *> zombies_;
    std::vector<ISrsResource *> *p_disposing_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The connections without any id.
    std::vector<ISrsResource *>
        conns_;
    // The connections with resource id.
    std::map<std::string, ISrsResource *> conns_id_;
    // The connections with resource fast(int) id.
    std::map<uint64_t, ISrsResource *> conns_fast_id_;
    // The level-0 fast cache for fast id.
    int nn_level0_cache_;
    SrsResourceFastIdItem *conns_level0_cache_;
    // The connections with resource name.
    std::map<std::string, ISrsResource *> conns_name_;

public:
    SrsResourceManager(const std::string &label, bool verbose = false);
    virtual ~SrsResourceManager();

public:
    srs_error_t start();
    bool empty();
    size_t size();
    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();

public:
    void add(ISrsResource *conn, bool *exists = NULL);
    void add_with_id(const std::string &id, ISrsResource *conn);
    void add_with_fast_id(uint64_t id, ISrsResource *conn);
    void add_with_name(const std::string &name, ISrsResource *conn);
    ISrsResource *at(int index);
    ISrsResource *find_by_id(std::string id);
    ISrsResource *find_by_fast_id(uint64_t id);
    ISrsResource *find_by_name(std::string name);

public:
    void subscribe(ISrsDisposingHandler *h);
    void unsubscribe(ISrsDisposingHandler *h);
    // Interface ISrsResourceManager
public:
    virtual void remove(ISrsResource *c);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    void do_remove(ISrsResource *c);
    void check_remove(ISrsResource *c, bool &in_zombie, bool &in_disposing);
    void clear();
    void do_clear();
    void dispose(ISrsResource *c);
};

// This class implements the ISrsResource interface using a smart pointer, allowing the Manager to delete this
// smart pointer resource, such as by implementing delayed release.
//
// It embeds an SrsSharedPtr to provide the same interface, but it is not an inheritance relationship. Its usage
// is identical to SrsSharedPtr, but they cannot replace each other. They are not related and cannot be converted
// to one another.
//
// Note that we don't need to implement the move constructor and move assignment operator, because we directly
// use SrsSharedPtr as instance member, so we can only copy it.
//
// Usage:
//      SrsSharedResource<MyClass>* ptr = new SrsSharedResource<MyClass>(new MyClass());
//      (*ptr)->do_something();
//
//      ISrsResourceManager* manager = ...;
//      manager->remove(ptr);
template <typename T>
class SrsSharedResource : public ISrsResource
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsSharedPtr<T> ptr_;

public:
    SrsSharedResource(T *ptr = NULL) : ptr_(ptr)
    {
    }
    SrsSharedResource(const SrsSharedResource<T> &cp) : ptr_(cp.ptr_)
    {
    }
    virtual ~SrsSharedResource()
    {
    }

public:
    // Get the object.
    T *get()
    {
        return ptr_.get();
    }
    // Overload the -> operator.
    T *operator->()
    {
        return ptr_.operator->();
    }
    // The assign operator.
    SrsSharedResource<T> &operator=(const SrsSharedResource<T> &cp)
    {
        if (this != &cp) {
            ptr_ = cp.ptr_;
        }
        return *this;
    }

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Overload the * operator.
    T &
    operator*()
    {
        return ptr_.operator*();
    }
    // Overload the bool operator.
    operator bool() const
    {
        return ptr_.operator bool();
    }
    // Interface ISrsResource
public:
    virtual const SrsContextId &get_id()
    {
        return ptr_->get_id();
    }
    virtual std::string desc()
    {
        return ptr_->desc();
    }
};

// Manager for RTC connections.
extern SrsResourceManager *_srs_conn_manager;

#endif
