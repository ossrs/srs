//----------------------------------------------------------------------------
// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or distribute
// this software, either in source code form or as a compiled binary, for any
// purpose, commercial or non-commercial, and by any means.
//
// In jurisdictions that recognize copyright laws, the author or authors of
// this software dedicate any and all copyright interest in the software to the
// public domain. We make this dedication for the benefit of the public at
// large and to the detriment of our heirs and successors. We intend this
// dedication to be an overt act of relinquishment in perpetuity of all present
// and future rights to this software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to <http://unlicense.org/>
//-----------------------------------------------------------------------------

// SRT Project information:
// This file was adopted from a Public Domain project from
// https://github.com/mbitsnbites/atomic
// Only namespaces were changed to adopt it for SRT project.

#ifndef SRT_SYNC_ATOMIC_H_
#define SRT_SYNC_ATOMIC_H_

// Macro for disallowing copying of an object.
#if __cplusplus >= 201103L
#define ATOMIC_DISALLOW_COPY(T) \
  T(const T&) = delete;         \
  T& operator=(const T&) = delete;
#else
#define ATOMIC_DISALLOW_COPY(T) \
  T(const T&);                  \
  T& operator=(const T&);
#endif

// A portable static assert.
#if __cplusplus >= 201103L
#define ATOMIC_STATIC_ASSERT(condition, message) \
  static_assert((condition), message)
#else
// Based on: http://stackoverflow.com/a/809465/5778708
#define ATOMIC_STATIC_ASSERT(condition, message) \
  _impl_STATIC_ASSERT_LINE(condition, __LINE__)
#define _impl_PASTE(a, b) a##b
#ifdef __GNUC__
#define _impl_UNUSED __attribute__((__unused__))
#else
#define _impl_UNUSED
#endif
#define _impl_STATIC_ASSERT_LINE(condition, line) \
  typedef char _impl_PASTE(                       \
      STATIC_ASSERT_failed_,                      \
      line)[(2 * static_cast<int>(!!(condition))) - 1] _impl_UNUSED
#endif

#if defined(ATOMIC_USE_SRT_SYNC_MUTEX) && (ATOMIC_USE_SRT_SYNC_MUTEX == 1)
   // NOTE: Defined at the top level.
#elif __cplusplus >= 201103L
   // NOTE: Prefer to use the c++11 std::atomic.
   #define ATOMIC_USE_CPP11_ATOMIC
#elif (defined(__clang__) && defined(__clang_major__) && (__clang_major__ > 5)) \
   || defined(__xlc__)
   // NOTE: Clang <6 does not support GCC __atomic_* intrinsics. I am unsure
   //    about Clang6. Since Clang sets __GNUC__ and __GNUC_MINOR__ of this era
   //    to <4.5, older Clang will catch the setting below to use the
   //    POSIX Mutex Implementation.
   #define ATOMIC_USE_GCC_INTRINSICS
#elif defined(__GNUC__) \
   && ( (__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 7)) )
   // NOTE: The __atomic_* family of intrisics were introduced in GCC-4.7.0.
   // NOTE: This follows #if defined(__clang__), because most if, not all,
   //    versions of Clang define __GNUC__ and __GNUC_MINOR__ but often define
   //    them to 4.4 or an even earlier version. Most of the newish versions
   //    of Clang also support GCC Atomic Intrisics even if they set GCC version
   //    macros to <4.7.
   #define ATOMIC_USE_GCC_INTRINSICS
#elif defined(__GNUC__) && !defined(ATOMIC_USE_SRT_SYNC_MUTEX)
   // NOTE: GCC compiler built-ins for atomic operations are pure
   //    compiler extensions prior to GCC-4.7 and were grouped into the
   //    the __sync_* family of functions. GCC-4.7, both the c++11 and C11
   //    standards had been finalized, and GCC updated their built-ins to
   //    better reflect the new memory model and the new functions grouped
   //    into the __atomic_* family. Also the memory models were defined
   //    differently, than in pre 4.7.
   // TODO: PORT to the pre GCC-4.7 __sync_* intrinsics. In the meantime use
   //    the POSIX Mutex Implementation.
   #define ATOMIC_USE_SRT_SYNC_MUTEX 1
#elif defined(_MSC_VER)
   #define ATOMIC_USE_MSVC_INTRINSICS
   #include "atomic_msvc.h"
#else
   #error Unsupported compiler / system.
#endif
// Include any necessary headers for the selected Atomic Implementation.
#if defined(ATOMIC_USE_SRT_SYNC_MUTEX) && (ATOMIC_USE_SRT_SYNC_MUTEX == 1)
   #include "sync.h"
#endif
#if defined(ATOMIC_USE_CPP11_ATOMIC)
   #include <atomic>
#endif

namespace srt {
namespace sync {
template <typename T>
class atomic {
public:
  ATOMIC_STATIC_ASSERT(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 ||
                           sizeof(T) == 8,
                       "Only types of size 1, 2, 4 or 8 are supported");

  atomic()
    : value_(static_cast<T>(0))
#if defined(ATOMIC_USE_SRT_SYNC_MUTEX) && (ATOMIC_USE_SRT_SYNC_MUTEX == 1)
    , mutex_()
#endif
  {
     // No-Op
  }

  explicit atomic(const T value)
    : value_(value)
#if defined(ATOMIC_USE_SRT_SYNC_MUTEX) && (ATOMIC_USE_SRT_SYNC_MUTEX == 1)
    , mutex_()
#endif
  {
     // No-Op
  }

  ~atomic()
  {
     // No-Op
  }

  /// @brief Performs an atomic increment operation (value + 1).
  /// @returns The new value of the atomic object.
  T operator++() {
#if defined(ATOMIC_USE_SRT_SYNC_MUTEX) && (ATOMIC_USE_SRT_SYNC_MUTEX == 1)
    ScopedLock lg_(mutex_);
    const T t = ++value_;
    return t;
#elif defined(ATOMIC_USE_GCC_INTRINSICS)
    return __atomic_add_fetch(&value_, 1, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_MSVC_INTRINSICS)
    return msvc::interlocked<T>::increment(&value_);
#elif defined(ATOMIC_USE_CPP11_ATOMIC)
    return ++value_;
#else
    #error "Implement Me."
#endif
  }

  /// @brief Performs an atomic decrement operation (value - 1).
  /// @returns The new value of the atomic object.
  T operator--() {
#if defined(ATOMIC_USE_SRT_SYNC_MUTEX) && (ATOMIC_USE_SRT_SYNC_MUTEX == 1)
    ScopedLock lg_(mutex_);
    const T t = --value_;
    return t;
#elif defined(ATOMIC_USE_GCC_INTRINSICS)
    return __atomic_sub_fetch(&value_, 1, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_MSVC_INTRINSICS)
    return msvc::interlocked<T>::decrement(&value_);
#elif defined(ATOMIC_USE_CPP11_ATOMIC)
    return --value_;
#else
    #error "Implement Me."
#endif
  }

  /// @brief Performs an atomic compare-and-swap (CAS) operation.
  ///
  /// The value of the atomic object is only updated to the new value if the
  /// old value of the atomic object matches @c expected_val.
  ///
  /// @param expected_val The expected value of the atomic object.
  /// @param new_val The new value to write to the atomic object.
  /// @returns True if new_value was written to the atomic object.
  bool compare_exchange(const T expected_val, const T new_val) {
#if defined(ATOMIC_USE_SRT_SYNC_MUTEX) && (ATOMIC_USE_SRT_SYNC_MUTEX == 1)
    ScopedLock lg_(mutex_);
    bool result = false;
    if (expected_val == value_)
    {
      value_ = new_val;
      result = true;
    }
    return result;
#elif defined(ATOMIC_USE_GCC_INTRINSICS)
    T e = expected_val;
    return __atomic_compare_exchange_n(
        &value_, &e, new_val, true, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_MSVC_INTRINSICS)
    const T old_val =
        msvc::interlocked<T>::compare_exchange(&value_, new_val, expected_val);
    return (old_val == expected_val);
#elif defined(ATOMIC_USE_CPP11_ATOMIC)
    T e = expected_val;
    return value_.compare_exchange_weak(e, new_val);
#else
    #error "Implement Me."
#endif
  }

  /// @brief Performs an atomic set operation.
  ///
  /// The value of the atomic object is unconditionally updated to the new
  /// value.
  ///
  /// @param new_val The new value to write to the atomic object.
  void store(const T new_val) {
#if defined(ATOMIC_USE_SRT_SYNC_MUTEX) && (ATOMIC_USE_SRT_SYNC_MUTEX == 1)
    ScopedLock lg_(mutex_);
    value_ = new_val;
#elif defined(ATOMIC_USE_GCC_INTRINSICS)
    __atomic_store_n(&value_, new_val, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_MSVC_INTRINSICS)
    (void)msvc::interlocked<T>::exchange(&value_, new_val);
#elif defined(ATOMIC_USE_CPP11_ATOMIC)
    value_.store(new_val);
#else
    #error "Implement Me."
#endif
  }

  /// @returns the current value of the atomic object.
  /// @note Be careful about how this is used, since any operations on the
  /// returned value are inherently non-atomic.
  T load() const {
#if defined(ATOMIC_USE_SRT_SYNC_MUTEX) && (ATOMIC_USE_SRT_SYNC_MUTEX == 1)
    ScopedLock lg_(mutex_);
    const T t = value_;
    return t;
#elif defined(ATOMIC_USE_GCC_INTRINSICS)
    return __atomic_load_n(&value_, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_MSVC_INTRINSICS)
    // TODO(m): Is there a better solution for MSVC?
    return value_;
#elif defined(ATOMIC_USE_CPP11_ATOMIC)
    return value_;
#else
    #error "Implement Me."
#endif
  }

  /// @brief Performs an atomic exchange operation.
  ///
  /// The value of the atomic object is unconditionally updated to the new
  /// value, and the old value is returned.
  ///
  /// @param new_val The new value to write to the atomic object.
  /// @returns the old value.
  T exchange(const T new_val) {
#if defined(ATOMIC_USE_SRT_SYNC_MUTEX) && (ATOMIC_USE_SRT_SYNC_MUTEX == 1)
    ScopedLock lg_(mutex_);
    const T t = value_;
    value_ = new_val;
    return t;
#elif defined(ATOMIC_USE_GCC_INTRINSICS)
    return __atomic_exchange_n(&value_, new_val, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_MSVC_INTRINSICS)
    return msvc::interlocked<T>::exchange(&value_, new_val);
#elif defined(ATOMIC_USE_CPP11_ATOMIC)
    return value_.exchange(new_val);
#else
    #error "Implement Me."
#endif
  }

  T operator=(const T new_value) {
    store(new_value);
    return new_value;
  }

  operator T() const {
    return load();
  }

private:
#if defined(ATOMIC_USE_SRT_SYNC_MUTEX) && (ATOMIC_USE_SRT_SYNC_MUTEX == 1)
  T value_;
  mutable Mutex mutex_;
#elif defined(ATOMIC_USE_GCC_INTRINSICS)
  volatile T value_;
#elif defined(ATOMIC_USE_MSVC_INTRINSICS)
  volatile T value_;
#elif defined(ATOMIC_USE_CPP11_ATOMIC)
  std::atomic<T> value_;
#else
   #error "Implement Me. (value_ type)"
#endif

  ATOMIC_DISALLOW_COPY(atomic)
};

}  // namespace sync
}  // namespace srt

// Undef temporary defines.
#undef ATOMIC_USE_GCC_INTRINSICS
#undef ATOMIC_USE_MSVC_INTRINSICS
#undef ATOMIC_USE_CPP11_ATOMIC

#endif  // ATOMIC_ATOMIC_H_
