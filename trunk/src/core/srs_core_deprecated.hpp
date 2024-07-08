//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_CORE_DEPRECATED_HPP
#define SRS_CORE_DEPRECATED_HPP

#include <srs_core.hpp>

#include <stdlib.h>

// Note that the SrsAutoFree is deprecated, please use SrsUniquePtr instead.
//
// Note: Please use SrsUniquePtr if possible. Please aware that there is a slight difference between SrsAutoFree
// and SrsUniquePtr. SrsAutoFree will track the address of pointer, while SrsUniquePtr will not.
//      MyClass* p;
//      SrsAutoFree(MyClass, p); // p will be freed even p is changed later.
//      SrsUniquePtr ptr(p); // crash because p is an invalid pointer.
//
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

#endif
