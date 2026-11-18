//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_CORE_DEPRECATED_HPP
#define SRS_CORE_DEPRECATED_HPP

#include <srs_core.hpp>

#include <stdlib.h>

// SrsAutoFree, SrsAutoFreeA and SrsAutoFreeH were replaced by SrsUniquePtr and are no longer defined.
// They held the address of the pointer and freed the value it had at scope exit, while SrsUniquePtr
// holds the value it is given, so create it after the pointer is set, such as after getaddrinfo.
// See https://github.com/ossrs/srs/issues/4551
//
// Their implementation:
// template<class T>
// class impl_SrsAutoFree
//{
// SRS_DECLARE_PRIVATE:
//    T** ptr;
//    bool is_array;
//    bool use_free_;
//    void (*hook_)(T*);
// public:
//    // If use_free, use free(void*) to release the p.
//    // If specified hook, use hook(p) to release it.
//    // Use delete to release p, or delete[] if p is an array.
//    impl_SrsAutoFree(T** p, bool array, bool use_free, void (*hook)(T*)) {
//        ptr = p;
//        is_array = array;
//        use_free_ = use_free;
//        hook_ = hook;
//    }
//
//    virtual ~impl_SrsAutoFree() {
//        if (ptr == NULL || *ptr == NULL) {
//            return;
//        }
//
//        if (use_free_) {
//            free(*ptr);
//        } else if (hook_) {
//            hook_(*ptr);
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
