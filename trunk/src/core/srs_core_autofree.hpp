//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_CORE_AUTO_FREE_HPP
#define SRS_CORE_AUTO_FREE_HPP

#include <srs_core.hpp>

/**
 * To free the instance in the current scope, for instance, MyClass* ptr,
 * which is a ptr and this class will:
 *       1. free the ptr.
 *       2. set ptr to NULL.
 *
 * Usage:
 *       MyClass* po = new MyClass();
 *       // ...... use po
 *       SrsAutoFree(MyClass, po);
 *
 * Usage for array:
 *      MyClass** pa = new MyClass*[size];
 *      // ....... use pa
 *      SrsAutoFreeA(MyClass*, pa);
 *
 * @remark the MyClass can be basic type, for instance, SrsAutoFreeA(char, pstr),
 *      where the char* pstr = new char[size].
 */
#define SrsAutoFree(className, instance) \
impl_SrsAutoFree<className> _auto_free_##instance(&instance, false)
#define SrsAutoFreeA(className, instance) \
impl_SrsAutoFree<className> _auto_free_array_##instance(&instance, true)
template<class T>
class impl_SrsAutoFree
{
private:
    T** ptr;
    bool is_array;
public:
    impl_SrsAutoFree(T** p, bool array) {
        ptr = p;
        is_array = array;
    }
    
    virtual ~impl_SrsAutoFree() {
        if (ptr == NULL || *ptr == NULL) {
            return;
        }
        
        if (is_array) {
            delete[] *ptr;
        } else {
            delete *ptr;
        }
        
        *ptr = NULL;
    }
};

#endif
