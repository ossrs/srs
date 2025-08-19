/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v.2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */
/*****************************************************************************
The file contains various planform and compiler dependent attribute definitions
used by SRT library internally.
 *****************************************************************************/

#ifndef INC_SRT_ATTR_DEFS_H
#define INC_SRT_ATTR_DEFS_H

// ATTRIBUTES:
//
// SRT_ATR_UNUSED: declare an entity ALLOWED to be unused (prevents warnings)
// ATR_DEPRECATED: declare an entity deprecated (compiler should warn when used)
// ATR_NOEXCEPT: The true `noexcept` from C++11, or nothing if compiling in pre-C++11 mode
// ATR_NOTHROW: In C++11: `noexcept`. In pre-C++11: `throw()`. Required for GNU libstdc++.
// ATR_CONSTEXPR: In C++11: `constexpr`. Otherwise empty.
// ATR_OVERRIDE: In C++11: `override`. Otherwise empty.
// ATR_FINAL: In C++11: `final`. Otherwise empty.

#ifdef __GNUG__
#define ATR_DEPRECATED __attribute__((deprecated))
#else
#define ATR_DEPRECATED
#endif

#if defined(__cplusplus) && __cplusplus > 199711L
#define HAVE_CXX11 1
// For gcc 4.7, claim C++11 is supported, as long as experimental C++0x is on,
// however it's only the "most required C++11 support".
#if defined(__GXX_EXPERIMENTAL_CXX0X__) && __GNUC__ == 4 && __GNUC_MINOR__ >= 7 // 4.7 only!
#define ATR_NOEXCEPT
#define ATR_NOTHROW throw()
#define ATR_CONSTEXPR
#define ATR_OVERRIDE
#define ATR_FINAL
#else
#define HAVE_FULL_CXX11 1
#define ATR_NOEXCEPT noexcept
#define ATR_NOTHROW noexcept
#define ATR_CONSTEXPR constexpr
#define ATR_OVERRIDE override
#define ATR_FINAL final
#endif
#elif defined(_MSC_VER) && _MSC_VER >= 1800
// Microsoft Visual Studio supports C++11, but not fully,
// and still did not change the value of __cplusplus. Treat
// this special way.
// _MSC_VER == 1800  means Microsoft Visual Studio 2013.
#define HAVE_CXX11 1
#if defined(_MSC_FULL_VER) && _MSC_FULL_VER >= 190023026
#define HAVE_FULL_CXX11 1
#define ATR_NOEXCEPT noexcept
#define ATR_NOTHROW noexcept
#define ATR_CONSTEXPR constexpr
#define ATR_OVERRIDE override
#define ATR_FINAL final
#else
#define ATR_NOEXCEPT
#define ATR_NOTHROW throw()
#define ATR_CONSTEXPR
#define ATR_OVERRIDE
#define ATR_FINAL
#endif
#else
#define HAVE_CXX11 0
#define ATR_NOEXCEPT
#define ATR_NOTHROW throw()
#define ATR_CONSTEXPR
#define ATR_OVERRIDE
#define ATR_FINAL
#endif // __cplusplus

#if !HAVE_CXX11 && defined(REQUIRE_CXX11) && REQUIRE_CXX11 == 1
#error "The currently compiled application required C++11, but your compiler doesn't support it."
#endif

///////////////////////////////////////////////////////////////////////////////
// Attributes for thread safety analysis
// - Clang TSA (https://clang.llvm.org/docs/ThreadSafetyAnalysis.html#mutexheader).
// - MSVC SAL (partially).
// - Other compilers: none.
///////////////////////////////////////////////////////////////////////////////
#if _MSC_VER >= 1920
// In case of MSVC these attributes have to precede the attributed objects (variable, function).
// E.g. SRT_ATTR_GUARDED_BY(mtx) int object;
// It is tricky to annotate e.g. the following function, as clang complaints it does not know 'm'.
// SRT_ATTR_EXCLUDES(m) SRT_ATTR_ACQUIRE(m)
// inline void enterCS(Mutex& m) { m.lock(); }
#define SRT_ATTR_CAPABILITY(expr)
#define SRT_ATTR_SCOPED_CAPABILITY
#define SRT_ATTR_GUARDED_BY(expr) _Guarded_by_(expr)
#define SRT_ATTR_PT_GUARDED_BY(expr)
#define SRT_ATTR_ACQUIRED_BEFORE(...)
#define SRT_ATTR_ACQUIRED_AFTER(...)
#define SRT_ATTR_REQUIRES(expr) _Requires_lock_held_(expr)
#define SRT_ATTR_REQUIRES2(expr1, expr2) _Requires_lock_held_(expr1) _Requires_lock_held_(expr2)
#define SRT_ATTR_REQUIRES_SHARED(...)
#define SRT_ATTR_ACQUIRE(expr) _Acquires_nonreentrant_lock_(expr)
#define SRT_ATTR_ACQUIRE_SHARED(...)
#define SRT_ATTR_RELEASE(expr) _Releases_lock_(expr)
#define SRT_ATTR_RELEASE_SHARED(...)
#define SRT_ATTR_RELEASE_GENERIC(...)
#define SRT_ATTR_TRY_ACQUIRE(...) _Acquires_nonreentrant_lock_(expr)
#define SRT_ATTR_TRY_ACQUIRE_SHARED(...)
#define SRT_ATTR_EXCLUDES(...)
#define SRT_ATTR_ASSERT_CAPABILITY(expr)
#define SRT_ATTR_ASSERT_SHARED_CAPABILITY(x)
#define SRT_ATTR_RETURN_CAPABILITY(x)
#define SRT_ATTR_NO_THREAD_SAFETY_ANALYSIS
#else

#if defined(__clang__) && defined(__clang_major__) && (__clang_major__ > 5)
#define THREAD_ANNOTATION_ATTRIBUTE__(x)   __attribute__((x))
#else
#define THREAD_ANNOTATION_ATTRIBUTE__(x)   // no-op
#endif

#define SRT_ATTR_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(capability(x))

#define SRT_ATTR_SCOPED_CAPABILITY \
  THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

#define SRT_ATTR_GUARDED_BY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))

#define SRT_ATTR_PT_GUARDED_BY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded_by(x))

#define SRT_ATTR_ACQUIRED_BEFORE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquired_before(__VA_ARGS__))

#define SRT_ATTR_ACQUIRED_AFTER(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquired_after(__VA_ARGS__))

#define SRT_ATTR_REQUIRES(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))

#define SRT_ATTR_REQUIRES2(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))

#define SRT_ATTR_REQUIRES_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(requires_shared_capability(__VA_ARGS__))

#define SRT_ATTR_ACQUIRE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))

#define SRT_ATTR_ACQUIRE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquire_shared_capability(__VA_ARGS__))

#define SRT_ATTR_RELEASE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))

#define SRT_ATTR_RELEASE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(release_shared_capability(__VA_ARGS__))

#define SRT_ATTR_RELEASE_GENERIC(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(release_generic_capability(__VA_ARGS__))

#define SRT_ATTR_TRY_ACQUIRE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_capability(__VA_ARGS__))

#define SRT_ATTR_TRY_ACQUIRE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_shared_capability(__VA_ARGS__))

#define SRT_ATTR_EXCLUDES(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(locks_excluded(__VA_ARGS__))

#define SRT_ATTR_ASSERT_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(assert_capability(x))

#define SRT_ATTR_ASSERT_SHARED_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(assert_shared_capability(x))

#define SRT_ATTR_RETURN_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(lock_returned(x))

#define SRT_ATTR_NO_THREAD_SAFETY_ANALYSIS \
  THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)

#endif // not _MSC_VER

#endif // INC_SRT_ATTR_DEFS_H
