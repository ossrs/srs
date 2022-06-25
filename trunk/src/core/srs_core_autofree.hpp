//
// Copyright (c) 2013-2022 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_CORE_AUTO_FREE_HPP
#define SRS_CORE_AUTO_FREE_HPP

#include <srs_core.hpp>

#include <stdlib.h>

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


class NonCopyable {
    public:
        NonCopyable() = default;
        ~NonCopyable() = default;
        NonCopyable(NonCopyable&&) = default; // allow move constructor

    private:
        NonCopyable(NonCopyable const& ) = delete;             // copy constructor is hidden
        NonCopyable &operator=(NonCopyable const& ) = delete;  // copy operator is hidden
};

// ScopeExit
template<typename FuncType>
class ScopeExit : public NonCopyable {
    public:
        // constructor, use std::forward for perfect forwarding
        inline explicit ScopeExit(FuncType&& func) : _func(std::move(func)) {}
        inline explicit ScopeExit(FuncType const& func) : _func(func) {}

        // move constructor, move all content
        inline ScopeExit(ScopeExit&& rhs) : _func(std::move(rhs._func)) {}

        // destructor, if not dismiss then call func
        inline ~ScopeExit() { if (!_is_dismiss) _func(); }

        inline void Dismiss() { _is_dismiss = true; }

    private:
        FuncType _func;
        bool _is_dismiss = false;
};


// ScopeExitCreator
class ScopeExitCreator {
    public:
        template<typename FuncType>
            inline ScopeExit<typename std::decay<FuncType>::type> operator<<(FuncType&& func) {
                    return ScopeExit<typename std::decay<FuncType>::type>(std::forward<FuncType>(func));
            }
};

#define TOKEN_CAT(x, y) x##y

#define SCOPE_EXIT auto TOKEN_CAT(scope_exit_, __LINE__) = ScopeExitCreator() << [&]
#define SCOPE_EXIT_ARGS(args...) auto TOKEN_CAT(scope_exit_, __LINE__) = ScopeExitCreator() << [##args]
#define SCOPE_EXIT_NAME(vname) auto vname = ScopeExitCreator() << [&]

#endif
