//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_CORE_DEPRECATED_HPP
#define SRS_CORE_DEPRECATED_HPP

#include <srs_core.hpp>

#include <stdlib.h>

// Note that the SrsAutoFree is deprecated, please use SrsUniquePtr instead. For example:
//      MyClass* p = new MyClass();
//      SrsAutoFree(MyClass, p);
// This can be replaced by:
//      SrsUniquePtr<MyClass> p(new MyClass());
//
// Note: Please aware that there is a slight difference between SrsAutoFree and SrsUniquePtr.
// SrsAutoFree will track the address of pointer, while SrsUniquePtr will not.
//      MyClass* p;
//      SrsAutoFree(MyClass, p); // p will be freed even p is changed later.
//      SrsUniquePtr ptr(p); // crash because p is an invalid pointer.
//
// See https://github.com/ossrs/srs/discussions/3667#discussioncomment-8969107 for more details.
//
// To delete array. Please use SrsUniquePtr instead. For example:
//      MyClass** pa = new MyClass*[size];
//      SrsAutoFreeA(MyClass*, pa);
// This can be replaced by:
//      SrsUniquePtr<MyClass[]> pa(new MyClass[10]);
//
// Note: Please aware that there is a slight difference between SrsAutoFreeA and SrsUniquePtr.
// SrsAutoFreeA will track the address of pointer, while SrsUniquePtr will not.
//      MyClass** pa;
//      SrsAutoFreeA(MyClass*, pa); // pa will be freed even pa is changed later.
//      SrsUniquePtr<MyClass[]> ptr(pa); // crash because pa is an invalid pointer.
//
// See https://github.com/ossrs/srs/discussions/3667#discussioncomment-8969107 for more details.
//
// Use hook instead of delete. Please use SrsUniquePtr instead. For example:
//      addrinfo* r = NULL;
//      SrsAutoFreeH(addrinfo, r, freeaddrinfo);
//      getaddrinfo("127.0.0.1", NULL, &hints, &r);
// This can be replaced by:
//      addrinfo* r = NULL;
//      getaddrinfo("127.0.0.1", NULL, &hints, &r);
//      SrsUniquePtr<addrinfo> ptr(r, freeaddrinfo);
//
// Note: Please aware that there is a slight difference between SrsAutoFreeH and SrsUniquePtr.
// SrsAutoFreeH will track the address of pointer, while SrsUniquePtr will not.
//      addrinfo* r = NULL;
//      SrsAutoFreeH(addrinfo, r, freeaddrinfo); // r will be freed even r is changed later.
//      SrsUniquePtr<addrinfo> ptr(r, freeaddrinfo); // crash because r is an invalid pointer.
//
// See https://github.com/ossrs/srs/discussions/3667#discussioncomment-8969107 for more details.
//
// The template implementation.
// template<class T>
// class impl_SrsAutoFree
//{
// SRS_DECLARE_PRIVATE:
//    T** ptr;
//    bool is_array;
//    bool _use_free;
//    void (*_hook)(T*);
// public:
//    // If use_free, use free(void*) to release the p.
//    // If specified hook, use hook(p) to release it.
//    // Use delete to release p, or delete[] if p is an array.
//    impl_SrsAutoFree(T** p, bool array, bool use_free, void (*hook)(T*)) {
//        ptr = p;
//        is_array = array;
//        _use_free = use_free;
//        _hook = hook;
//    }
//
//    virtual ~impl_SrsAutoFree() {
//        if (ptr == NULL || *ptr == NULL) {
//            return;
//        }
//
//        if (_use_free) {
//            free(*ptr);
//        } else if (_hook) {
//            _hook(*ptr);
//        } else {
//            if (is_array) {
//                delete[] *ptr;
//            } else {
//                delete *ptr;
//            }
//        }
//
//        *ptr = NULL;
//    }
//};

#endif
