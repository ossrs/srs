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

#include <config.h>
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>                   // for PRIuPTR
#endif
#include <errno.h>                      // for ENOMEM, errno
#include <gperftools/malloc_extension.h>      // for MallocRange, etc
#include "base/basictypes.h"
#include "base/commandlineflags.h"
#include "internal_logging.h"  // for ASSERT, TCMalloc_Printer, etc
#include "page_heap_allocator.h"  // for PageHeapAllocator
#include "static_vars.h"       // for Static
#include "system-alloc.h"      // for TCMalloc_SystemAlloc, etc

DEFINE_double(tcmalloc_release_rate,
              EnvToDouble("TCMALLOC_RELEASE_RATE", 1.0),
              "Rate at which we release unused memory to the system.  "
              "Zero means we never release memory back to the system.  "
              "Increase this flag to return memory faster; decrease it "
              "to return memory slower.  Reasonable rates are in the "
              "range [0,10]");

DEFINE_int64(tcmalloc_heap_limit_mb,
              EnvToInt("TCMALLOC_HEAP_LIMIT_MB", 0),
              "Limit total size of the process heap to the "
              "specified number of MiB. "
              "When we approach the limit the memory is released "
              "to the system more aggressively (more minor page faults). "
              "Zero means to allocate as long as system allows.");

namespace tcmalloc {

PageHeap::PageHeap()
    : pagemap_(MetaDataAlloc),
      scavenge_counter_(0),
      // Start scavenging at kMaxPages list
      release_index_(kMaxPages),
      aggressive_decommit_(false) {
  COMPILE_ASSERT(kClassSizesMax <= (1 << PageMapCache::kValuebits), valuebits);
  for (int i = 0; i < kMaxPages; i++) {
    DLL_Init(&free_[i].normal);
    DLL_Init(&free_[i].returned);
  }
}

Span* PageHeap::SearchFreeAndLargeLists(Length n) {
  ASSERT(Check());
  ASSERT(n > 0);

  // Find first size >= n that has a non-empty list
  for (Length s = n; s <= kMaxPages; s++) {
    Span* ll = &free_[s - 1].normal;
    // If we're lucky, ll is non-empty, meaning it has a suitable span.
    if (!DLL_IsEmpty(ll)) {
      ASSERT(ll->next->location == Span::ON_NORMAL_FREELIST);
      return Carve(ll->next, n);
    }
    // Alternatively, maybe there's a usable returned span.
    ll = &free_[s - 1].returned;
    if (!DLL_IsEmpty(ll)) {
      // We did not call EnsureLimit before, to avoid releasing the span
      // that will be taken immediately back.
      // Calling EnsureLimit here is not very expensive, as it fails only if
      // there is no more normal spans (and it fails efficiently)
      // or SystemRelease does not work (there is probably no returned spans).
      if (EnsureLimit(n)) {
        // ll may have became empty due to coalescing
        if (!DLL_IsEmpty(ll)) {
          ASSERT(ll->next->location == Span::ON_RETURNED_FREELIST);
          return Carve(ll->next, n);
        }
      }
    }
  }
  // No luck in free lists, our last chance is in a larger class.
  return AllocLarge(n);  // May be NULL
}

static const size_t kForcedCoalesceInterval = 128*1024*1024;

Span* PageHeap::New(Length n) {
  ASSERT(Check());
  ASSERT(n > 0);

  Span* result = SearchFreeAndLargeLists(n);
  if (result != NULL)
    return result;

  if (stats_.free_bytes != 0 && stats_.unmapped_bytes != 0
      && stats_.free_bytes + stats_.unmapped_bytes >= stats_.system_bytes / 4
      && (stats_.system_bytes / kForcedCoalesceInterval
          != (stats_.system_bytes + (n << kPageShift)) / kForcedCoalesceInterval)) {
    // We're about to grow heap, but there are lots of free pages.
    // tcmalloc's design decision to keep unmapped and free spans
    // separately and never coalesce them means that sometimes there
    // can be free pages span of sufficient size, but it consists of
    // "segments" of different type so page heap search cannot find
    // it. In order to prevent growing heap and wasting memory in such
    // case we're going to unmap all free pages. So that all free
    // spans are maximally coalesced.
    //
    // We're also limiting 'rate' of going into this path to be at
    // most once per 128 megs of heap growth. Otherwise programs that
    // grow heap frequently (and that means by small amount) could be
    // penalized with higher count of minor page faults.
    //
    // See also large_heap_fragmentation_unittest.cc and
    // https://code.google.com/p/gperftools/issues/detail?id=368
    ReleaseAtLeastNPages(static_cast<Length>(0x7fffffff));

    // then try again. If we are forced to grow heap because of large
    // spans fragmentation and not because of problem described above,
    // then at the very least we've just unmapped free but
    // insufficiently big large spans back to OS. So in case of really
    // unlucky memory fragmentation we'll be consuming virtual address
    // space, but not real memory
    result = SearchFreeAndLargeLists(n);
    if (result != NULL) return result;
  }

  // Grow the heap and try again.
  if (!GrowHeap(n)) {
    ASSERT(stats_.unmapped_bytes+ stats_.committed_bytes==stats_.system_bytes);
    ASSERT(Check());
    // underlying SysAllocator likely set ENOMEM but we can get here
    // due to EnsureLimit so we set it here too.
    //
    // Setting errno to ENOMEM here allows us to avoid dealing with it
    // in fast-path.
    errno = ENOMEM;
    return NULL;
  }
  return SearchFreeAndLargeLists(n);
}

Span* PageHeap::AllocLarge(Length n) {
  Span *best = NULL;
  Span *best_normal = NULL;

  // Create a Span to use as an upper bound.
  Span bound;
  bound.start = 0;
  bound.length = n;

  // First search the NORMAL spans..
  SpanSet::iterator place = large_normal_.upper_bound(SpanPtrWithLength(&bound));
  if (place != large_normal_.end()) {
    best = place->span;
    best_normal = best;
    ASSERT(best->location == Span::ON_NORMAL_FREELIST);
  }

  // Try to find better fit from RETURNED spans.
  place = large_returned_.upper_bound(SpanPtrWithLength(&bound));
  if (place != large_returned_.end()) {
    Span *c = place->span;
    ASSERT(c->location == Span::ON_RETURNED_FREELIST);
    if (best_normal == NULL
        || c->length < best->length
        || (c->length == best->length && c->start < best->start))
      best = place->span;
  }

  if (best == best_normal) {
    return best == NULL ? NULL : Carve(best, n);
  }

  // best comes from RETURNED set.

  if (EnsureLimit(n, false)) {
    return Carve(best, n);
  }

  if (EnsureLimit(n, true)) {
    // best could have been destroyed by coalescing.
    // best_normal is not a best-fit, and it could be destroyed as well.
    // We retry, the limit is already ensured:
    return AllocLarge(n);
  }

  // If best_normal existed, EnsureLimit would succeeded:
  ASSERT(best_normal == NULL);
  // We are not allowed to take best from returned list.
  return NULL;
}

Span* PageHeap::Split(Span* span, Length n) {
  ASSERT(0 < n);
  ASSERT(n < span->length);
  ASSERT(span->location == Span::IN_USE);
  ASSERT(span->sizeclass == 0);

  const int extra = span->length - n;
  Span* leftover = NewSpan(span->start + n, extra);
  ASSERT(leftover->location == Span::IN_USE);
  RecordSpan(leftover);
  pagemap_.set(span->start + n - 1, span); // Update map from pageid to span
  span->length = n;

  return leftover;
}

void PageHeap::CommitSpan(Span* span) {
  ++stats_.commit_count;

  TCMalloc_SystemCommit(reinterpret_cast<void*>(span->start << kPageShift),
                        static_cast<size_t>(span->length << kPageShift));
  stats_.committed_bytes += span->length << kPageShift;
  stats_.total_commit_bytes += (span->length << kPageShift);
}

bool PageHeap::DecommitSpan(Span* span) {
  ++stats_.decommit_count;

  bool rv = TCMalloc_SystemRelease(reinterpret_cast<void*>(span->start << kPageShift),
                                   static_cast<size_t>(span->length << kPageShift));
  if (rv) {
    stats_.committed_bytes -= span->length << kPageShift;
    stats_.total_decommit_bytes += (span->length << kPageShift);
  }

  return rv;
}

Span* PageHeap::Carve(Span* span, Length n) {
  ASSERT(n > 0);
  ASSERT(span->location != Span::IN_USE);
  const int old_location = span->location;
  RemoveFromFreeList(span);
  span->location = Span::IN_USE;

  const int extra = span->length - n;
  ASSERT(extra >= 0);
  if (extra > 0) {
    Span* leftover = NewSpan(span->start + n, extra);
    leftover->location = old_location;
    RecordSpan(leftover);

    // The previous span of |leftover| was just splitted -- no need to
    // coalesce them. The next span of |leftover| was not previously coalesced
    // with |span|, i.e. is NULL or has got location other than |old_location|.
#ifndef NDEBUG
    const PageID p = leftover->start;
    const Length len = leftover->length;
    Span* next = GetDescriptor(p+len);
    ASSERT (next == NULL ||
            next->location == Span::IN_USE ||
            next->location != leftover->location);
#endif

    PrependToFreeList(leftover);  // Skip coalescing - no candidates possible
    span->length = n;
    pagemap_.set(span->start + n - 1, span);
  }
  ASSERT(Check());
  if (old_location == Span::ON_RETURNED_FREELIST) {
    // We need to recommit this address space.
    CommitSpan(span);
  }
  ASSERT(span->location == Span::IN_USE);
  ASSERT(span->length == n);
  ASSERT(stats_.unmapped_bytes+ stats_.committed_bytes==stats_.system_bytes);
  return span;
}

void PageHeap::Delete(Span* span) {
  ASSERT(Check());
  ASSERT(span->location == Span::IN_USE);
  ASSERT(span->length > 0);
  ASSERT(GetDescriptor(span->start) == span);
  ASSERT(GetDescriptor(span->start + span->length - 1) == span);
  const Length n = span->length;
  span->sizeclass = 0;
  span->sample = 0;
  span->location = Span::ON_NORMAL_FREELIST;
  MergeIntoFreeList(span);  // Coalesces if possible
  IncrementalScavenge(n);
  ASSERT(stats_.unmapped_bytes+ stats_.committed_bytes==stats_.system_bytes);
  ASSERT(Check());
}

// Given span we're about to free and other span (still on free list),
// checks if 'other' span is mergable with 'span'. If it is, removes
// other span from free list, performs aggressive decommit if
// necessary and returns 'other' span. Otherwise 'other' span cannot
// be merged and is left untouched. In that case NULL is returned.
Span* PageHeap::CheckAndHandlePreMerge(Span* span, Span* other) {
  if (other == NULL) {
    return other;
  }
  // if we're in aggressive decommit mode and span is decommitted,
  // then we try to decommit adjacent span.
  if (aggressive_decommit_ && other->location == Span::ON_NORMAL_FREELIST
      && span->location == Span::ON_RETURNED_FREELIST) {
    bool worked = DecommitSpan(other);
    if (!worked) {
      return NULL;
    }
  } else if (other->location != span->location) {
    return NULL;
  }

  RemoveFromFreeList(other);
  return other;
}

void PageHeap::MergeIntoFreeList(Span* span) {
  ASSERT(span->location != Span::IN_USE);

  // Coalesce -- we guarantee that "p" != 0, so no bounds checking
  // necessary.  We do not bother resetting the stale pagemap
  // entries for the pieces we are merging together because we only
  // care about the pagemap entries for the boundaries.
  //
  // Note: depending on aggressive_decommit_ mode we allow only
  // similar spans to be coalesced.
  //
  // The following applies if aggressive_decommit_ is enabled:
  //
  // TODO(jar): "Always decommit" causes some extra calls to commit when we are
  // called in GrowHeap() during an allocation :-/.  We need to eval the cost of
  // that oscillation, and possibly do something to reduce it.

  // TODO(jar): We need a better strategy for deciding to commit, or decommit,
  // based on memory usage and free heap sizes.

  const PageID p = span->start;
  const Length n = span->length;

  if (aggressive_decommit_ && span->location == Span::ON_NORMAL_FREELIST) {
    if (DecommitSpan(span)) {
      span->location = Span::ON_RETURNED_FREELIST;
    }
  }

  Span* prev = CheckAndHandlePreMerge(span, GetDescriptor(p-1));
  if (prev != NULL) {
    // Merge preceding span into this span
    ASSERT(prev->start + prev->length == p);
    const Length len = prev->length;
    DeleteSpan(prev);
    span->start -= len;
    span->length += len;
    pagemap_.set(span->start, span);
  }
  Span* next = CheckAndHandlePreMerge(span, GetDescriptor(p+n));
  if (next != NULL) {
    // Merge next span into this span
    ASSERT(next->start == p+n);
    const Length len = next->length;
    DeleteSpan(next);
    span->length += len;
    pagemap_.set(span->start + span->length - 1, span);
  }

  PrependToFreeList(span);
}

void PageHeap::PrependToFreeList(Span* span) {
  ASSERT(span->location != Span::IN_USE);
  if (span->location == Span::ON_NORMAL_FREELIST)
    stats_.free_bytes += (span->length << kPageShift);
  else
    stats_.unmapped_bytes += (span->length << kPageShift);

  if (span->length > kMaxPages) {
    SpanSet *set = &large_normal_;
    if (span->location == Span::ON_RETURNED_FREELIST)
      set = &large_returned_;
    std::pair<SpanSet::iterator, bool> p =
        set->insert(SpanPtrWithLength(span));
    ASSERT(p.second); // We never have duplicates since span->start is unique.
    span->SetSpanSetIterator(p.first);
    return;
  }

  SpanList* list = &free_[span->length - 1];
  if (span->location == Span::ON_NORMAL_FREELIST) {
    DLL_Prepend(&list->normal, span);
  } else {
    DLL_Prepend(&list->returned, span);
  }
}

void PageHeap::RemoveFromFreeList(Span* span) {
  ASSERT(span->location != Span::IN_USE);
  if (span->location == Span::ON_NORMAL_FREELIST) {
    stats_.free_bytes -= (span->length << kPageShift);
  } else {
    stats_.unmapped_bytes -= (span->length << kPageShift);
  }
  if (span->length > kMaxPages) {
    SpanSet *set = &large_normal_;
    if (span->location == Span::ON_RETURNED_FREELIST)
      set = &large_returned_;
    SpanSet::iterator iter = span->ExtractSpanSetIterator();
    ASSERT(iter->span == span);
    ASSERT(set->find(SpanPtrWithLength(span)) == iter);
    set->erase(iter);
  } else {
    DLL_Remove(span);
  }
}

void PageHeap::IncrementalScavenge(Length n) {
  // Fast path; not yet time to release memory
  scavenge_counter_ -= n;
  if (scavenge_counter_ >= 0) return;  // Not yet time to scavenge

  const double rate = FLAGS_tcmalloc_release_rate;
  if (rate <= 1e-6) {
    // Tiny release rate means that releasing is disabled.
    scavenge_counter_ = kDefaultReleaseDelay;
    return;
  }

  ++stats_.scavenge_count;

  Length released_pages = ReleaseAtLeastNPages(1);

  if (released_pages == 0) {
    // Nothing to scavenge, delay for a while.
    scavenge_counter_ = kDefaultReleaseDelay;
  } else {
    // Compute how long to wait until we return memory.
    // FLAGS_tcmalloc_release_rate==1 means wait for 1000 pages
    // after releasing one page.
    const double mult = 1000.0 / rate;
    double wait = mult * static_cast<double>(released_pages);
    if (wait > kMaxReleaseDelay) {
      // Avoid overflow and bound to reasonable range.
      wait = kMaxReleaseDelay;
    }
    scavenge_counter_ = static_cast<int64_t>(wait);
  }
}

Length PageHeap::ReleaseSpan(Span* s) {
  ASSERT(s->location == Span::ON_NORMAL_FREELIST);

  if (DecommitSpan(s)) {
    RemoveFromFreeList(s);
    const Length n = s->length;
    s->location = Span::ON_RETURNED_FREELIST;
    MergeIntoFreeList(s);  // Coalesces if possible.
    return n;
  }

  return 0;
}

Length PageHeap::ReleaseAtLeastNPages(Length num_pages) {
  Length released_pages = 0;

  // Round robin through the lists of free spans, releasing a
  // span from each list.  Stop after releasing at least num_pages
  // or when there is nothing more to release.
  while (released_pages < num_pages && stats_.free_bytes > 0) {
    for (int i = 0; i < kMaxPages+1 && released_pages < num_pages;
         i++, release_index_++) {
      Span *s;
      if (release_index_ > kMaxPages) release_index_ = 0;

      if (release_index_ == kMaxPages) {
        if (large_normal_.empty()) {
          continue;
        }
        s = (large_normal_.begin())->span;
      } else {
        SpanList* slist = &free_[release_index_];
        if (DLL_IsEmpty(&slist->normal)) {
          continue;
        }
        s = slist->normal.prev;
      }
      // TODO(todd) if the remaining number of pages to release
      // is significantly smaller than s->length, and s is on the
      // large freelist, should we carve s instead of releasing?
      // the whole thing?
      Length released_len = ReleaseSpan(s);
      // Some systems do not support release
      if (released_len == 0) return released_pages;
      released_pages += released_len;
    }
  }
  return released_pages;
}

bool PageHeap::EnsureLimit(Length n, bool withRelease)
{
  Length limit = (FLAGS_tcmalloc_heap_limit_mb*1024*1024) >> kPageShift;
  if (limit == 0) return true; //there is no limit

  // We do not use stats_.system_bytes because it does not take
  // MetaDataAllocs into account.
  Length takenPages = TCMalloc_SystemTaken >> kPageShift;
  //XXX takenPages may be slightly bigger than limit for two reasons:
  //* MetaDataAllocs ignore the limit (it is not easy to handle
  //  out of memory there)
  //* sys_alloc may round allocation up to huge page size,
  //  although smaller limit was ensured

  ASSERT(takenPages >= stats_.unmapped_bytes >> kPageShift);
  takenPages -= stats_.unmapped_bytes >> kPageShift;

  if (takenPages + n > limit && withRelease) {
    takenPages -= ReleaseAtLeastNPages(takenPages + n - limit);
  }

  return takenPages + n <= limit;
}

void PageHeap::RegisterSizeClass(Span* span, uint32 sc) {
  // Associate span object with all interior pages as well
  ASSERT(span->location == Span::IN_USE);
  ASSERT(GetDescriptor(span->start) == span);
  ASSERT(GetDescriptor(span->start+span->length-1) == span);
  span->sizeclass = sc;
  for (Length i = 1; i < span->length-1; i++) {
    pagemap_.set(span->start+i, span);
  }
}

void PageHeap::GetSmallSpanStats(SmallSpanStats* result) {
  for (int i = 0; i < kMaxPages; i++) {
    result->normal_length[i] = DLL_Length(&free_[i].normal);
    result->returned_length[i] = DLL_Length(&free_[i].returned);
  }
}

void PageHeap::GetLargeSpanStats(LargeSpanStats* result) {
  result->spans = 0;
  result->normal_pages = 0;
  result->returned_pages = 0;
  for (SpanSet::iterator it = large_normal_.begin(); it != large_normal_.end(); ++it) {
    result->normal_pages += it->length;
    result->spans++;
  }
  for (SpanSet::iterator it = large_returned_.begin(); it != large_returned_.end(); ++it) {
    result->returned_pages += it->length;
    result->spans++;
  }
}

bool PageHeap::GetNextRange(PageID start, base::MallocRange* r) {
  Span* span = reinterpret_cast<Span*>(pagemap_.Next(start));
  if (span == NULL) {
    return false;
  }
  r->address = span->start << kPageShift;
  r->length = span->length << kPageShift;
  r->fraction = 0;
  switch (span->location) {
    case Span::IN_USE:
      r->type = base::MallocRange::INUSE;
      r->fraction = 1;
      if (span->sizeclass > 0) {
        // Only some of the objects in this span may be in use.
        const size_t osize = Static::sizemap()->class_to_size(span->sizeclass);
        r->fraction = (1.0 * osize * span->refcount) / r->length;
      }
      break;
    case Span::ON_NORMAL_FREELIST:
      r->type = base::MallocRange::FREE;
      break;
    case Span::ON_RETURNED_FREELIST:
      r->type = base::MallocRange::UNMAPPED;
      break;
    default:
      r->type = base::MallocRange::UNKNOWN;
      break;
  }
  return true;
}

static void RecordGrowth(size_t growth) {
  StackTrace* t = Static::stacktrace_allocator()->New();
  t->depth = GetStackTrace(t->stack, kMaxStackDepth-1, 3);
  t->size = growth;
  t->stack[kMaxStackDepth-1] = reinterpret_cast<void*>(Static::growth_stacks());
  Static::set_growth_stacks(t);
}

bool PageHeap::GrowHeap(Length n) {
  ASSERT(kMaxPages >= kMinSystemAlloc);
  if (n > kMaxValidPages) return false;
  Length ask = (n>kMinSystemAlloc) ? n : static_cast<Length>(kMinSystemAlloc);
  size_t actual_size;
  void* ptr = NULL;
  if (EnsureLimit(ask)) {
      ptr = TCMalloc_SystemAlloc(ask << kPageShift, &actual_size, kPageSize);
  }
  if (ptr == NULL) {
    if (n < ask) {
      // Try growing just "n" pages
      ask = n;
      if (EnsureLimit(ask)) {
        ptr = TCMalloc_SystemAlloc(ask << kPageShift, &actual_size, kPageSize);
      }
    }
    if (ptr == NULL) return false;
  }
  ask = actual_size >> kPageShift;
  RecordGrowth(ask << kPageShift);

  ++stats_.reserve_count;
  ++stats_.commit_count;

  uint64_t old_system_bytes = stats_.system_bytes;
  stats_.system_bytes += (ask << kPageShift);
  stats_.committed_bytes += (ask << kPageShift);

  stats_.total_commit_bytes += (ask << kPageShift);
  stats_.total_reserve_bytes += (ask << kPageShift);

  const PageID p = reinterpret_cast<uintptr_t>(ptr) >> kPageShift;
  ASSERT(p > 0);

  // If we have already a lot of pages allocated, just pre allocate a bunch of
  // memory for the page map. This prevents fragmentation by pagemap metadata
  // when a program keeps allocating and freeing large blocks.

  if (old_system_bytes < kPageMapBigAllocationThreshold
      && stats_.system_bytes >= kPageMapBigAllocationThreshold) {
    pagemap_.PreallocateMoreMemory();
  }

  // Make sure pagemap_ has entries for all of the new pages.
  // Plus ensure one before and one after so coalescing code
  // does not need bounds-checking.
  if (pagemap_.Ensure(p-1, ask+2)) {
    // Pretend the new area is allocated and then Delete() it to cause
    // any necessary coalescing to occur.
    Span* span = NewSpan(p, ask);
    RecordSpan(span);
    Delete(span);
    ASSERT(stats_.unmapped_bytes+ stats_.committed_bytes==stats_.system_bytes);
    ASSERT(Check());
    return true;
  } else {
    // We could not allocate memory within "pagemap_"
    // TODO: Once we can return memory to the system, return the new span
    return false;
  }
}

bool PageHeap::Check() {
  return true;
}

bool PageHeap::CheckExpensive() {
  bool result = Check();
  CheckSet(&large_normal_, kMaxPages + 1, Span::ON_NORMAL_FREELIST);
  CheckSet(&large_returned_, kMaxPages + 1, Span::ON_RETURNED_FREELIST);
  for (int s = 1; s <= kMaxPages; s++) {
    CheckList(&free_[s - 1].normal, s, s, Span::ON_NORMAL_FREELIST);
    CheckList(&free_[s - 1].returned, s, s, Span::ON_RETURNED_FREELIST);
  }
  return result;
}

bool PageHeap::CheckList(Span* list, Length min_pages, Length max_pages,
                         int freelist) {
  for (Span* s = list->next; s != list; s = s->next) {
    CHECK_CONDITION(s->location == freelist);  // NORMAL or RETURNED
    CHECK_CONDITION(s->length >= min_pages);
    CHECK_CONDITION(s->length <= max_pages);
    CHECK_CONDITION(GetDescriptor(s->start) == s);
    CHECK_CONDITION(GetDescriptor(s->start+s->length-1) == s);
  }
  return true;
}

bool PageHeap::CheckSet(SpanSet* spanset, Length min_pages,int freelist) {
  for (SpanSet::iterator it = spanset->begin(); it != spanset->end(); ++it) {
    Span* s = it->span;
    CHECK_CONDITION(s->length == it->length);
    CHECK_CONDITION(s->location == freelist);  // NORMAL or RETURNED
    CHECK_CONDITION(s->length >= min_pages);
    CHECK_CONDITION(GetDescriptor(s->start) == s);
    CHECK_CONDITION(GetDescriptor(s->start+s->length-1) == s);
  }
  return true;
}

}  // namespace tcmalloc
