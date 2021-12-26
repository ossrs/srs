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

#include <config.h>
#include "stack_trace_table.h"
#include <string.h>                     // for NULL, memset
#include "base/spinlock.h"              // for SpinLockHolder
#include "common.h"            // for StackTrace
#include "internal_logging.h"  // for ASSERT, Log
#include "page_heap_allocator.h"  // for PageHeapAllocator
#include "static_vars.h"       // for Static

namespace tcmalloc {

StackTraceTable::StackTraceTable()
    : error_(false),
      depth_total_(0),
      bucket_total_(0),
      head_(nullptr) {
}

StackTraceTable::~StackTraceTable() {
  ASSERT(head_ == nullptr);
}

void StackTraceTable::AddTrace(const StackTrace& t) {
  if (error_) {
    return;
  }

  depth_total_ += t.depth;
  bucket_total_++;
  Entry* entry = allocator_.allocate(1);
  if (entry == nullptr) {
    Log(kLog, __FILE__, __LINE__,
        "tcmalloc: could not allocate bucket", sizeof(*entry));
    error_ = true;
  } else {
    entry->trace = t;
    entry->next = head_;
    head_ = entry;
  }
}

void** StackTraceTable::ReadStackTracesAndClear() {
  void** out = nullptr;

  const int out_len = bucket_total_ * 3 + depth_total_ + 1;
  if (!error_) {
    // Allocate output array
    out = new (std::nothrow_t{}) void*[out_len];
    if (out == nullptr) {
      Log(kLog, __FILE__, __LINE__,
          "tcmalloc: allocation failed for stack traces",
          out_len * sizeof(*out));
    }
  }

  if (out) {
    // Fill output array
    int idx = 0;
    Entry* entry = head_;
    while (entry != NULL) {
      out[idx++] = reinterpret_cast<void*>(uintptr_t{1});   // count
      out[idx++] = reinterpret_cast<void*>(entry->trace.size);  // cumulative size
      out[idx++] = reinterpret_cast<void*>(entry->trace.depth);
      for (int d = 0; d < entry->trace.depth; ++d) {
        out[idx++] = entry->trace.stack[d];
      }
      entry = entry->next;
    }
    out[idx++] = NULL;
    ASSERT(idx == out_len);
  }

  // Clear state
  error_ = false;
  depth_total_ = 0;
  bucket_total_ = 0;

  SpinLockHolder h(Static::pageheap_lock());
  Entry* entry = head_;
  while (entry != nullptr) {
    Entry* next = entry->next;
    allocator_.deallocate(entry, 1);
    entry = next;
  }
  head_ = nullptr;

  return out;
}

}  // namespace tcmalloc
