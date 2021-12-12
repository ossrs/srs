// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright (c) 2008, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// ---
// Author: Sanjay Ghemawat <opensource@google.com>

#ifndef TCMALLOC_PAGE_HEAP_ALLOCATOR_H_
#define TCMALLOC_PAGE_HEAP_ALLOCATOR_H_

#include <stddef.h>                     // for NULL, size_t

#include "common.h"            // for MetaDataAlloc
#include "internal_logging.h"  // for ASSERT

namespace tcmalloc {

// Simple allocator for objects of a specified type.  External locking
// is required before accessing one of these objects.
template <class T>
class PageHeapAllocator {
 public:
  // We use an explicit Init function because these variables are statically
  // allocated and their constructors might not have run by the time some
  // other static variable tries to allocate memory.
  void Init() {
    ASSERT(sizeof(T) <= kAllocIncrement);
    inuse_ = 0;
    free_area_ = NULL;
    free_avail_ = 0;
    free_list_ = NULL;
    // Reserve some space at the beginning to avoid fragmentation.
    Delete(New());
  }

  T* New() {
    // Consult free list
    void* result;
    if (free_list_ != NULL) {
      result = free_list_;
      free_list_ = *(reinterpret_cast<void**>(result));
    } else {
      if (free_avail_ < sizeof(T)) {
        // Need more room. We assume that MetaDataAlloc returns
        // suitably aligned memory.
        free_area_ = reinterpret_cast<char*>(MetaDataAlloc(kAllocIncrement));
        if (free_area_ == NULL) {
          Log(kCrash, __FILE__, __LINE__,
              "FATAL ERROR: Out of memory trying to allocate internal "
              "tcmalloc data (bytes, object-size)",
              kAllocIncrement, sizeof(T));
        }
        free_avail_ = kAllocIncrement;
      }
      result = free_area_;
      free_area_ += sizeof(T);
      free_avail_ -= sizeof(T);
    }
    inuse_++;
    return reinterpret_cast<T*>(result);
  }

  void Delete(T* p) {
    *(reinterpret_cast<void**>(p)) = free_list_;
    free_list_ = p;
    inuse_--;
  }

  int inuse() const { return inuse_; }

 private:
  // How much to allocate from system at a time
  static const int kAllocIncrement = 128 << 10;

  // Free area from which to carve new objects
  char* free_area_;
  size_t free_avail_;

  // Free list of already carved objects
  void* free_list_;

  // Number of allocated but unfreed objects
  int inuse_;
};

// STL-compatible allocator which forwards allocations to a PageHeapAllocator.
//
// Like PageHeapAllocator, this requires external synchronization. To avoid multiple
// separate STLPageHeapAllocator<T> from sharing the same underlying PageHeapAllocator<T>,
// the |LockingTag| template argument should be used. Template instantiations with
// different locking tags can safely be used concurrently.
template <typename T, class LockingTag>
class STLPageHeapAllocator {
 public:
  typedef size_t     size_type;
  typedef ptrdiff_t  difference_type;
  typedef T*         pointer;
  typedef const T*   const_pointer;
  typedef T&         reference;
  typedef const T&   const_reference;
  typedef T          value_type;

  template <class T1> struct rebind {
    typedef STLPageHeapAllocator<T1, LockingTag> other;
  };

  STLPageHeapAllocator() { }
  STLPageHeapAllocator(const STLPageHeapAllocator&) { }
  template <class T1> STLPageHeapAllocator(const STLPageHeapAllocator<T1, LockingTag>&) { }
  ~STLPageHeapAllocator() { }

  pointer address(reference x) const { return &x; }
  const_pointer address(const_reference x) const { return &x; }

  size_type max_size() const { return size_t(-1) / sizeof(T); }

  void construct(pointer p, const T& val) { ::new(p) T(val); }
  void construct(pointer p) { ::new(p) T(); }
  void destroy(pointer p) { p->~T(); }

  // There's no state, so these allocators are always equal
  bool operator==(const STLPageHeapAllocator&) const { return true; }
  bool operator!=(const STLPageHeapAllocator&) const { return false; }

  pointer allocate(size_type n, const void* = 0) {
    if (!underlying_.initialized) {
      underlying_.allocator.Init();
      underlying_.initialized = true;
    }

    CHECK_CONDITION(n == 1);
    return underlying_.allocator.New();
  }
  void deallocate(pointer p, size_type n) {
    CHECK_CONDITION(n == 1);
    underlying_.allocator.Delete(p);
  }

 private:
  struct Storage {
    explicit Storage(base::LinkerInitialized x) {}
    PageHeapAllocator<T> allocator;
    bool initialized;
  };
  static Storage underlying_;
};

template<typename T, class LockingTag>
typename STLPageHeapAllocator<T, LockingTag>::Storage STLPageHeapAllocator<T, LockingTag>::underlying_(base::LINKER_INITIALIZED);

}  // namespace tcmalloc

#endif  // TCMALLOC_PAGE_HEAP_ALLOCATOR_H_
