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
//
// A Span is a contiguous run of pages.

#ifndef TCMALLOC_SPAN_H_
#define TCMALLOC_SPAN_H_

#include <config.h>
#include <set>
#include "common.h"
#include "base/logging.h"
#include "page_heap_allocator.h"

namespace tcmalloc {

struct SpanBestFitLess;
struct Span;

// Store a pointer to a span along with a cached copy of its length.
// These are used as set elements to improve the performance of
// comparisons during tree traversal: the lengths are inline with the
// tree nodes and thus avoid expensive cache misses to dereference
// the actual Span objects in most cases.
struct SpanPtrWithLength {
  explicit SpanPtrWithLength(Span* s);

  Span* span;
  Length length;
};
typedef std::set<SpanPtrWithLength, SpanBestFitLess, STLPageHeapAllocator<SpanPtrWithLength, void> > SpanSet;

// Comparator for best-fit search, with address order as a tie-breaker.
struct SpanBestFitLess {
  bool operator()(SpanPtrWithLength a, SpanPtrWithLength b) const;
};

// Information kept for a span (a contiguous run of pages).
struct Span {
  PageID        start;          // Starting page number
  Length        length;         // Number of pages in span
  Span*         next;           // Used when in link list
  Span*         prev;           // Used when in link list
  union {
    void* objects;              // Linked list of free objects

    // Span may contain iterator pointing back at SpanSet entry of
    // this span into set of large spans. It is used to quickly delete
    // spans from those sets. span_iter_space is space for such
    // iterator which lifetime is controlled explicitly.
    char span_iter_space[sizeof(SpanSet::iterator)];
  };
  unsigned int  refcount : 16;  // Number of non-free objects
  unsigned int  sizeclass : 8;  // Size-class for small objects (or 0)
  unsigned int  location : 2;   // Is the span on a freelist, and if so, which?
  unsigned int  sample : 1;     // Sampled object?
  bool          has_span_iter : 1; // Iff span_iter_space has valid
                                   // iterator. Only for debug builds.

  // Sets iterator stored in span_iter_space.
  // Requires has_span_iter == 0.
  void SetSpanSetIterator(const SpanSet::iterator& iter);
  // Copies out and destroys iterator stored in span_iter_space.
  SpanSet::iterator ExtractSpanSetIterator();

  // What freelist the span is on: IN_USE if on none, or normal or returned
  enum { IN_USE, ON_NORMAL_FREELIST, ON_RETURNED_FREELIST };
};

inline SpanPtrWithLength::SpanPtrWithLength(Span* s)
    : span(s),
      length(s->length) {
}

inline bool SpanBestFitLess::operator()(SpanPtrWithLength a, SpanPtrWithLength b) const {
  if (a.length < b.length)
    return true;
  if (a.length > b.length)
    return false;
  return a.span->start < b.span->start;
}

inline void Span::SetSpanSetIterator(const SpanSet::iterator& iter) {
  ASSERT(!has_span_iter);
  has_span_iter = 1;

  new (span_iter_space) SpanSet::iterator(iter);
}

inline SpanSet::iterator Span::ExtractSpanSetIterator() {
  typedef SpanSet::iterator iterator_type;

  ASSERT(has_span_iter);
  has_span_iter = 0;

  iterator_type* this_iter =
    reinterpret_cast<iterator_type*>(span_iter_space);
  iterator_type retval = *this_iter;
  this_iter->~iterator_type();
  return retval;
}

// Allocator/deallocator for spans
Span* NewSpan(PageID p, Length len);
void DeleteSpan(Span* span);

// -------------------------------------------------------------------------
// Doubly linked list of spans.
// -------------------------------------------------------------------------

// Initialize *list to an empty list.
void DLL_Init(Span* list);

// Remove 'span' from the linked list in which it resides, updating the
// pointers of adjacent Spans and setting span's next and prev to NULL.
void DLL_Remove(Span* span);

// Return true iff "list" is empty.
inline bool DLL_IsEmpty(const Span* list) {
  return list->next == list;
}

// Add span to the front of list.
void DLL_Prepend(Span* list, Span* span);

// Return the length of the linked list. O(n)
int DLL_Length(const Span* list);

}  // namespace tcmalloc

#endif  // TCMALLOC_SPAN_H_
