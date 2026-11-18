//
// Copyright (c) 2013-2026 The SRS Authors
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
class ISrsKernelFactory;

class ISrsDisposingHandler
{
public:
    ISrsDisposingHandler();
    virtual ~ISrsDisposingHandler();

public:
    // Called inside manager.remove(c). Drop the references to c here, so no other object refers to it after.
    virtual void on_before_dispose(ISrsResource *c) = 0;
    // Called later in the manager's coroutine, and c is freed after it. Stop the threads and timers of c
    // here, so its fields can be freed in any order.
    virtual void on_disposing(ISrsResource *c) = 0;
};

// A slot of the level-0 cache. It holds the first fast id that takes it, until that id is removed; another
// fast id of the same slot is found in the map.
class SrsResourceFastIdItem
{
public:
    bool available_;
    uint64_t fast_id_;
    ISrsResource *impl_;

public:
    SrsResourceFastIdItem()
    {
        available_ = false;
        fast_id_ = 0;
        impl_ = NULL;
    }
};

class ISrsResource
{
public:
    ISrsResource();
    virtual ~ISrsResource();

public:
    virtual const SrsContextId &get_id() = 0;

public:
    virtual std::string desc();
};

class ISrsResourceManager
{
public:
    ISrsResourceManager();
    virtual ~ISrsResourceManager();

public:
    virtual srs_error_t start() = 0;
    virtual bool empty() = 0;
    virtual size_t size() = 0;

public:
    virtual void add(ISrsResource *conn, bool *exists = NULL) = 0;
    virtual void add_with_id(const std::string &id, ISrsResource *conn) = 0;
    virtual void add_with_fast_id(uint64_t id, ISrsResource *conn) = 0;
    virtual ISrsResource *at(int index) = 0;
    virtual ISrsResource *find_by_id(std::string id) = 0;
    virtual ISrsResource *find_by_fast_id(uint64_t id) = 0;
    virtual ISrsResource *find_by_name(std::string name) = 0;
    virtual void add_with_name(const std::string &name, ISrsResource *conn) = 0;

public:
    // The manager always frees c, in this coroutine or another one, even when c was added to one of its maps.
    virtual void remove(ISrsResource *c) = 0;

public:
    virtual void subscribe(ISrsDisposingHandler *h) = 0;
    virtual void unsubscribe(ISrsDisposingHandler *h) = 0;
};

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
    ISrsKernelFactory *factory_;
    std::vector<ISrsDisposingHandler *> handlers_;
    std::vector<ISrsDisposingHandler *> unsubs_;
    bool removing_;
    std::vector<ISrsResource *> zombies_;
    std::vector<ISrsResource *> *p_disposing_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<ISrsResource *>
        conns_;
    std::map<std::string, ISrsResource *> conns_id_;
    std::map<uint64_t, ISrsResource *> conns_fast_id_;
    int nn_level0_cache_;
    SrsResourceFastIdItem *conns_level0_cache_;
    std::map<std::string, ISrsResource *> conns_name_;

public:
    SrsResourceManager(const std::string &label, bool verbose = false);
    virtual ~SrsResourceManager();

public:
    void assemble(); // Creates the members, to avoid calling functions in the constructor.

public:
    srs_error_t start();
    bool empty();
    size_t size();

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

// Lets a resource manager free a shared pointer, such as for a delayed release. It is not an SrsSharedPtr
// and cannot be converted to one. It has no move constructor or move assignment, since it only copies its
// SrsSharedPtr member.
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
    T *get()
    {
        return ptr_.get();
    }
    T *operator->()
    {
        return ptr_.operator->();
    }
    SrsSharedResource<T> &operator=(const SrsSharedResource<T> &cp)
    {
        if (this != &cp) {
            ptr_ = cp.ptr_;
        }
        return *this;
    }

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    T &
    operator*()
    {
        return ptr_.operator*();
    }
    operator bool() const
    {
        return ptr_.operator bool();
    }

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

extern SrsResourceManager *_srs_conn_manager;

#endif
