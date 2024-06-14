//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_CORE_AUTO_FREE_HPP
#define SRS_CORE_AUTO_FREE_HPP

#include <srs_core.hpp>

#include <stdlib.h>

// The auto free helper, which is actually the unique ptr, without the move feature,
// see https://github.com/ossrs/srs/discussions/3667#discussioncomment-8969107
//
// To free the instance in the current scope, for instance, MyClass* ptr,
// which is a ptr and this class will:
//       1. free the ptr.
//       2. set ptr to NULL.
//
// Usage:
//       MyClass* po = new MyClass();
//       // ...... use po
//       SrsAutoFree(MyClass, po);
//
// Usage for array:
//      MyClass** pa = new MyClass*[size];
//      // ....... use pa
//      SrsAutoFreeA(MyClass*, pa);
//
// @remark the MyClass can be basic type, for instance, SrsAutoFreeA(char, pstr),
//      where the char* pstr = new char[size].
// To delete object.
#define SrsAutoFree(className, instance) \
    impl_SrsAutoFree<className> _auto_free_##instance(&instance, false, false, NULL)
// To delete array.
#define SrsAutoFreeA(className, instance) \
    impl_SrsAutoFree<className> _auto_free_array_##instance(&instance, true, false, NULL)
// Use free instead of delete.
#define SrsAutoFreeF(className, instance) \
    impl_SrsAutoFree<className> _auto_free_##instance(&instance, false, true, NULL)
// Use hook instead of delete.
#define SrsAutoFreeH(className, instance, hook) \
    impl_SrsAutoFree<className> _auto_free_##instance(&instance, false, false, hook)
// The template implementation.
template<class T>
class impl_SrsAutoFree
{
private:
    T** ptr;
    bool is_array;
    bool _use_free;
    void (*_hook)(T*);
public:
    // If use_free, use free(void*) to release the p.
    // If specified hook, use hook(p) to release it.
    // Use delete to release p, or delete[] if p is an array.
    impl_SrsAutoFree(T** p, bool array, bool use_free, void (*hook)(T*)) {
        ptr = p;
        is_array = array;
        _use_free = use_free;
        _hook = hook;
    }
    
    virtual ~impl_SrsAutoFree() {
        if (ptr == NULL || *ptr == NULL) {
            return;
        }

        if (_use_free) {
            free(*ptr);
        } else if (_hook) {
            _hook(*ptr);
        } else {
            if (is_array) {
                delete[] *ptr;
            } else {
                delete *ptr;
            }
        }
        
        *ptr = NULL;
    }
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
