//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_CORE_AUTO_FREE_HPP
#define SRS_CORE_AUTO_FREE_HPP

#include <srs_core.hpp>

// Unique ptr smart pointer, only support unique ptr, with limited APIs and features,
// see https://github.com/ossrs/srs/issues/4551
//
// Usage:
//      SrsUniquePtr<MyClass> ptr(new MyClass());
//      ptr->do_something();
//
// Note that the p should be initialized before use it, or it will crash if not set, for example:
//      Myclass* p;
//      SrsUniquePtr<MyClass> ptr(p); // crash because p is an invalid pointer.
//
// Note that do not support array or object created by malloc, because we only use delete to dispose
// the resource. You can use a custom function to free the memory allocated by malloc or other
// allocators.
//      char* p = (char*)malloc(1024);
//      SrsUniquePtr<char> ptr(p, your_free_chars);
// Or to free a specific object:
//      addrinfo* r = NULL;
//      getaddrinfo("127.0.0.1", NULL, &hints, &r);
//      SrsUniquePtr<addrinfo> ptr(r, freeaddrinfo);
template <class T>
class SrsUniquePtr
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    T *ptr_;
    void (*deleter_)(T *);

public:
    SrsUniquePtr(T *ptr = NULL, void (*deleter)(T *) = NULL)
    {
        ptr_ = ptr;
        deleter_ = deleter;
    }
    virtual ~SrsUniquePtr()
    {
        if (!deleter_) {
            delete ptr_;
        } else {
            deleter_(ptr_);
        }
    }

public:
    // Get the object.
    T *get()
    {
        return ptr_;
    }
    // Overload the -> operator.
    T *operator->()
    {
        return ptr_;
    }

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Copy the unique ptr.
    SrsUniquePtr(const SrsUniquePtr<T> &);
    // The assign operator.
    SrsUniquePtr<T> &operator=(const SrsUniquePtr<T> &);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Overload the * operator.
    T &
    operator*();
    // Overload the bool operator.
    operator bool() const;
#if __cplusplus >= 201103L // C++11
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The move constructor.
    SrsUniquePtr(SrsUniquePtr<T> &&);
    // The move assign operator.
    SrsUniquePtr<T> &operator=(SrsUniquePtr<T> &&);
#endif
};

// The unique ptr for array objects, only support unique ptr, with limited APIs and features,
// see https://github.com/ossrs/srs/issues/4551
//
// Usage:
//      SrsUniquePtr<MyClass[]> ptr(new MyClass[10]);
//      ptr[0]->do_something();
//
// Note that the p should be initialized before use it, or it will crash if not set, for example:
//      Myclass* p;
//      SrsUniquePtr<MyClass[]> ptr(p); // crash because p is an invalid pointer.
template <class T>
class SrsUniquePtr<T[]>
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    T *ptr_;

public:
    SrsUniquePtr(T *ptr = NULL)
    {
        ptr_ = ptr;
    }
    virtual ~SrsUniquePtr()
    {
        delete[] ptr_;
    }

public:
    // Get the object.
    T *get()
    {
        return ptr_;
    }
    // Overload the [] operator.
    T &operator[](std::size_t index)
    {
        return ptr_[index];
    }
    const T &operator[](std::size_t index) const
    {
        return ptr_[index];
    }

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Copy the unique ptr.
    SrsUniquePtr(const SrsUniquePtr<T> &);
    // The assign operator.
    SrsUniquePtr<T> &operator=(const SrsUniquePtr<T> &);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Overload the * operator.
    T &
    operator*();
    // Overload the bool operator.
    operator bool() const;
#if __cplusplus >= 201103L // C++11
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The move constructor.
    SrsUniquePtr(SrsUniquePtr<T> &&);
    // The move assign operator.
    SrsUniquePtr<T> &operator=(SrsUniquePtr<T> &&);
#endif
};

// Shared ptr smart pointer, only support shared ptr, no weak ptr, no shared from this, no inheritance,
// no comparing, see https://github.com/ossrs/srs/issues/4551
//
// Usage:
//      SrsSharedPtr<MyClass> ptr(new MyClass());
//      ptr->do_something();
//
//      SrsSharedPtr<MyClass> cp = ptr;
//      cp->do_something();
template <class T>
class SrsSharedPtr
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The pointer to the object.
    T *ptr_;
    // The reference count of the object.
    uint32_t *ref_count_;

public:
    // Create a shared ptr with the object.
    SrsSharedPtr(T *ptr = NULL)
    {
        ptr_ = ptr;
        ref_count_ = new uint32_t(1);
    }
    // Copy the shared ptr.
    SrsSharedPtr(const SrsSharedPtr<T> &cp)
    {
        copy(cp);
    }
    // Dispose and delete the shared ptr.
    virtual ~SrsSharedPtr()
    {
        reset();
    }

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Reset the shared ptr.
    void reset()
    {
        if (!ref_count_)
            return;

        (*ref_count_)--;
        if (*ref_count_ == 0) {
            delete ptr_;
            delete ref_count_;
        }

        ptr_ = NULL;
        ref_count_ = NULL;
    }
    // Copy from other shared ptr.
    void copy(const SrsSharedPtr<T> &cp)
    {
        ptr_ = cp.ptr_;
        ref_count_ = cp.ref_count_;
        if (ref_count_)
            (*ref_count_)++;
    }
    // Move from other shared ptr.
    void move(SrsSharedPtr<T> &cp)
    {
        ptr_ = cp.ptr_;
        ref_count_ = cp.ref_count_;
        cp.ptr_ = NULL;
        cp.ref_count_ = NULL;
    }

public:
    // Get the object.
    T *get()
    {
        return ptr_;
    }
    // Overload the -> operator.
    T *operator->()
    {
        return ptr_;
    }
    // The assign operator.
    SrsSharedPtr<T> &operator=(const SrsSharedPtr<T> &cp)
    {
        if (this != &cp) {
            reset();
            copy(cp);
        }
        return *this;
    }

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Overload the * operator.
    T &
    operator*()
    {
        return *ptr_;
    }
    // Overload the bool operator.
    operator bool() const
    {
        return ptr_ != NULL;
    }
#if __cplusplus >= 201103L // C++11
public:
    // The move constructor.
    SrsSharedPtr(SrsSharedPtr<T> &&cp)
    {
        move(cp);
    };
    // The move assign operator.
    SrsSharedPtr<T> &operator=(SrsSharedPtr<T> &&cp)
    {
        if (this != &cp) {
            reset();
            move(cp);
        }
        return *this;
    };
#endif
};

#endif
