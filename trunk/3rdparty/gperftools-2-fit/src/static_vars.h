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
// Author: Ken Ashcraft <opensource@google.com>
//
// Static variables shared by multiple classes.

#ifndef TCMALLOC_STATIC_VARS_H_
#define TCMALLOC_STATIC_VARS_H_

#include <config.h>
#include "base/basictypes.h"
#include "base/spinlock.h"
#include "central_freelist.h"
#include "common.h"
#include "page_heap.h"
#include "page_heap_allocator.h"
#include "span.h"
#include "stack_trace_table.h"

namespace tcmalloc {

class Static {
 public:
  // Linker initialized, so this lock can be accessed at any time.
  static SpinLock* pageheap_lock() { return &pageheap_lock_; }

  // Must be called before calling any of the accessors below.
  static void InitStaticVars();
  static void InitLateMaybeRecursive();

  // Central cache -- an array of free-lists, one per size-class.
  // We have a separate lock per free-list to reduce contention.
  static CentralFreeListPadded* central_cache() { return central_cache_; }

  static SizeMap* sizemap() { return &sizemap_; }

  static unsigned num_size_classes() { return sizemap_.num_size_classes; }

  //////////////////////////////////////////////////////////////////////
  // In addition to the explicit initialization comment, the variables below
  // must be protected by pageheap_lock.

  // Page-level allocator.
  static PageHeap* pageheap() { return reinterpret_cast<PageHeap *>(&pageheap_.memory); }

  static PageHeapAllocator<Span>* span_allocator() { return &span_allocator_; }

  static PageHeapAllocator<StackTrace>* stacktrace_allocator() {
    return &stacktrace_allocator_;
  }

  static StackTrace* growth_stacks() { return growth_stacks_; }
  static void set_growth_stacks(StackTrace* s) { growth_stacks_ = s; }

  // State kept for sampled allocations (/pprof/heap support)
  static Span* sampled_objects() { return &sampled_objects_; }

  // Check if InitStaticVars() has been run.
  static bool IsInited() { return inited_; }

 private:
  // some unit tests depend on this and link to static vars
  // imperfectly. Thus we keep those unhidden for now. Thankfully
  // they're not performance-critical.
  /* ATTRIBUTE_HIDDEN */ static bool inited_;
  /* ATTRIBUTE_HIDDEN */ static SpinLock pageheap_lock_;

  // These static variables require explicit initialization.  We cannot
  // count on their constructors to do any initialization because other
  // static variables may try to allocate memory before these variables
  // can run their constructors.

  ATTRIBUTE_HIDDEN static SizeMap sizemap_;
  ATTRIBUTE_HIDDEN static CentralFreeListPadded central_cache_[kClassSizesMax];
  ATTRIBUTE_HIDDEN static PageHeapAllocator<Span> span_allocator_;
  ATTRIBUTE_HIDDEN static PageHeapAllocator<StackTrace> stacktrace_allocator_;
  ATTRIBUTE_HIDDEN static Span sampled_objects_;

  // Linked list of stack traces recorded every time we allocated memory
  // from the system.  Useful for finding allocation sites that cause
  // increase in the footprint of the system.  The linked list pointer
  // is stored in trace->stack[kMaxStackDepth-1].
  ATTRIBUTE_HIDDEN static StackTrace* growth_stacks_;

  // PageHeap uses a constructor for initialization.  Like the members above,
  // we can't depend on initialization order, so pageheap is new'd
  // into this buffer.
  union PageHeapStorage {
    char memory[sizeof(PageHeap)];
    uintptr_t extra;  // To force alignment
  };
  ATTRIBUTE_HIDDEN static PageHeapStorage pageheap_;
};

}  // namespace tcmalloc

#endif  // TCMALLOC_STATIC_VARS_H_
