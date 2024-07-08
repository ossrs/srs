//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_CORE_AUTO_FREE_HPP
#define SRS_CORE_AUTO_FREE_HPP

#include <srs_core.hpp>

// Unique ptr smart pointer, only support unique ptr, with limited APIs and features,
// see https://github.com/ossrs/srs/discussions/3667#discussioncomment-8969107
//
// Usage:
//      SrsUniquePtr<MyClass> ptr(new MyClass());
//      ptr->do_something();
//
// Note that the ptr should be initialized before use it, or it will crash if not set, for example:
//      Myclass* p;
//      SrsUniquePtr<MyClass> ptr(p); // crash because p is an invalid pointer.
//
// Note that do not support array or object created by malloc, because we only use delete to dispose
// the resource.
template<class T>
class SrsUniquePtr
{
private:
    T* ptr_;
public:
    SrsUniquePtr(T* ptr = NULL) {
        ptr_ = ptr;
    }
    virtual ~SrsUniquePtr() {
        delete ptr_;
    }
public:
    // Get the object.
    T* get() {
        return ptr_;
    }
    // Overload the -> operator.
    T* operator->() {
        return ptr_;
    }
private:
    // Copy the unique ptr.
    SrsUniquePtr(const SrsUniquePtr<T>&) = delete;
    // The assign operator.
    SrsUniquePtr<T>& operator=(const SrsUniquePtr<T>&) = delete;
private:
    // Overload the * operator.
    T& operator*() = delete;
    // Overload the bool operator.
    operator bool() const = delete;
#if __cplusplus >= 201103L // C++11
private:
    // The move constructor.
    SrsUniquePtr(SrsUniquePtr<T>&&) = delete;
    // The move assign operator.
    SrsUniquePtr<T>& operator=(SrsUniquePtr<T>&&) = delete;
#endif
};

// The unique ptr for array objects, only support unique ptr, with limited APIs and features,
// see https://github.com/ossrs/srs/discussions/3667#discussioncomment-8969107
//
// Usage:
//      SrsUniquePtr<MyClass[]> ptr(new MyClass[10]);
//      ptr[0]->do_something();
template<class T>
class SrsUniquePtr<T[]>
{
private:
    T* ptr_;
public:
    SrsUniquePtr(T* ptr = NULL) {
        ptr_ = ptr;
    }
    virtual ~SrsUniquePtr() {
        delete[] ptr_;
    }
public:
    // Get the object.
    T* get() {
        return ptr_;
    }
    // Overload the [] operator.
    T& operator[](std::size_t index) {
        return ptr_[index];
    }
    const T& operator[](std::size_t index) const {
        return ptr_[index];
    }
private:
    // Copy the unique ptr.
    SrsUniquePtr(const SrsUniquePtr<T>&) = delete;
    // The assign operator.
    SrsUniquePtr<T>& operator=(const SrsUniquePtr<T>&) = delete;
private:
    // Overload the * operator.
    T& operator*() = delete;
    // Overload the bool operator.
    operator bool() const = delete;
#if __cplusplus >= 201103L // C++11
private:
    // The move constructor.
    SrsUniquePtr(SrsUniquePtr<T>&&) = delete;
    // The move assign operator.
    SrsUniquePtr<T>& operator=(SrsUniquePtr<T>&&) = delete;
#endif
};

// Shared ptr smart pointer, only support shared ptr, no weak ptr, no shared from this, no inheritance,
// no comparing, see https://github.com/ossrs/srs/discussions/3667#discussioncomment-8969107
//
// Usage:
//      SrsSharedPtr<MyClass> ptr(new MyClass());
//      ptr->do_something();
//
//      SrsSharedPtr<MyClass> cp = ptr;
//      cp->do_something();
template<class T>
class SrsSharedPtr
{
private:
    // The pointer to the object.
    T* ptr_;
    // The reference count of the object.
    uint32_t* ref_count_;
public:
    // Create a shared ptr with the object.
    SrsSharedPtr(T* ptr = NULL) {
        ptr_ = ptr;
        ref_count_ = new uint32_t(1);
    }
    // Copy the shared ptr.
    SrsSharedPtr(const SrsSharedPtr<T>& cp) {
        copy(cp);
    }
    // Dispose and delete the shared ptr.
    virtual ~SrsSharedPtr() {
        reset();
    }
private:
    // Reset the shared ptr.
    void reset() {
        if (!ref_count_) return;

        (*ref_count_)--;
        if (*ref_count_ == 0) {
            delete ptr_;
            delete ref_count_;
        }

        ptr_ = NULL;
        ref_count_ = NULL;
    }
    // Copy from other shared ptr.
    void copy(const SrsSharedPtr<T>& cp) {
        ptr_ = cp.ptr_;
        ref_count_ = cp.ref_count_;
        if (ref_count_) (*ref_count_)++;
    }
    // Move from other shared ptr.
    void move(SrsSharedPtr<T>& cp) {
        ptr_ = cp.ptr_;
        ref_count_ = cp.ref_count_;
        cp.ptr_ = NULL;
        cp.ref_count_ = NULL;
    }
public:
    // Get the object.
    T* get() {
        return ptr_;
    }
    // Overload the -> operator.
    T* operator->() {
        return ptr_;
    }
    // The assign operator.
    SrsSharedPtr<T>& operator=(const SrsSharedPtr<T>& cp) {
        if (this != &cp) {
            reset();
            copy(cp);
        }
        return *this;
    }
private:
    // Overload the * operator.
    T& operator*() {
        return *ptr_;
    }
    // Overload the bool operator.
    operator bool() const {
        return ptr_ != NULL;
    }
#if __cplusplus >= 201103L // C++11
public:
    // The move constructor.
    SrsSharedPtr(SrsSharedPtr<T>&& cp) {
        move(cp);
    };
    // The move assign operator.
    SrsSharedPtr<T>& operator=(SrsSharedPtr<T>&& cp) {
        if (this != &cp) {
            reset();
            move(cp);
        }
        return *this;
    };
#endif
};

#endif
