// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright (c) 2009, Google Inc.
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
// Author: Andrew Fikes
//
// Utility class for coalescing sampled stack traces.  Not thread-safe.

#ifndef TCMALLOC_STACK_TRACE_TABLE_H_
#define TCMALLOC_STACK_TRACE_TABLE_H_

#include <config.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>                     // for uintptr_t
#endif
#include "common.h"
#include "page_heap_allocator.h"

namespace tcmalloc {

class PERFTOOLS_DLL_DECL StackTraceTable {
 public:
  // REQUIRES: L < pageheap_lock
  StackTraceTable();
  ~StackTraceTable();

  // Adds stack trace "t" to table.
  //
  // REQUIRES: L >= pageheap_lock
  void AddTrace(const StackTrace& t);

  // Returns stack traces formatted per MallocExtension guidelines.
  // May return NULL on error.  Clears state before returning.
  //
  // REQUIRES: L < pageheap_lock
  void** ReadStackTracesAndClear();

  // Exposed for PageHeapAllocator
  // For testing
  int depth_total() const { return depth_total_; }
  int bucket_total() const { return bucket_total_; }

 private:
  struct Entry {
    Entry* next;
    StackTrace trace;
  };

  bool error_;
  int depth_total_;
  int bucket_total_;
  Entry* head_;
  STLPageHeapAllocator<Entry, void> allocator_;
};

}  // namespace tcmalloc

#endif  // TCMALLOC_STACK_TRACE_TABLE_H_
