// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright (c) 2005, Google Inc.
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
// A malloc that uses a per-thread cache to satisfy small malloc requests.
// (The time for malloc/free of a small object drops from 300 ns to 50 ns.)
//
// See docs/tcmalloc.html for a high-level
// description of how this malloc works.
//
// SYNCHRONIZATION
//  1. The thread-specific lists are accessed without acquiring any locks.
//     This is safe because each such list is only accessed by one thread.
//  2. We have a lock per central free-list, and hold it while manipulating
//     the central free list for a particular size.
//  3. The central page allocator is protected by "pageheap_lock".
//  4. The pagemap (which maps from page-number to descriptor),
//     can be read without holding any locks, and written while holding
//     the "pageheap_lock".
//  5. To improve performance, a subset of the information one can get
//     from the pagemap is cached in a data structure, pagemap_cache_,
//     that atomically reads and writes its entries.  This cache can be
//     read and written without locking.
//
//     This multi-threaded access to the pagemap is safe for fairly
//     subtle reasons.  We basically assume that when an object X is
//     allocated by thread A and deallocated by thread B, there must
//     have been appropriate synchronization in the handoff of object
//     X from thread A to thread B.  The same logic applies to pagemap_cache_.
//
// THE PAGEID-TO-SIZECLASS CACHE
// Hot PageID-to-sizeclass mappings are held by pagemap_cache_.  If this cache
// returns 0 for a particular PageID then that means "no information," not that
// the sizeclass is 0.  The cache may have stale information for pages that do
// not hold the beginning of any free()'able object.  Staleness is eliminated
// in Populate() for pages with sizeclass > 0 objects, and in do_malloc() and
// do_memalign() for all other relevant pages.
//
// PAGEMAP
// -------
// Page map contains a mapping from page id to Span.
//
// If Span s occupies pages [p..q],
//      pagemap[p] == s
//      pagemap[q] == s
//      pagemap[p+1..q-1] are undefined
//      pagemap[p-1] and pagemap[q+1] are defined:
//         NULL if the corresponding page is not yet in the address space.
//         Otherwise it points to a Span.  This span may be free
//         or allocated.  If free, it is in one of pageheap's freelist.
//
// TODO: Bias reclamation to larger addresses
// TODO: implement mallinfo/mallopt
// TODO: Better testing
//
// 9/28/2003 (new page-level allocator replaces ptmalloc2):
// * malloc/free of small objects goes from ~300 ns to ~50 ns.
// * allocation of a reasonably complicated struct
//   goes from about 1100 ns to about 300 ns.

#include "config.h"
// At least for gcc on Linux/i386 and Linux/amd64 not adding throw()
// to tc_xxx functions actually ends up generating better code.
#define PERFTOOLS_NOTHROW
#include <gperftools/tcmalloc.h>

#include <errno.h>                      // for ENOMEM, EINVAL, errno
#if defined HAVE_STDINT_H
#include <stdint.h>
#elif defined HAVE_INTTYPES_H
#include <inttypes.h>
#else
#include <sys/types.h>
#endif
#include <stddef.h>                     // for size_t, NULL
#include <stdlib.h>                     // for getenv
#include <string.h>                     // for strcmp, memset, strlen, etc
#ifdef HAVE_UNISTD_H
#include <unistd.h>                     // for getpagesize, write, etc
#endif
#include <algorithm>                    // for max, min
#include <limits>                       // for numeric_limits
#include <new>                          // for nothrow_t (ptr only), etc
#include <vector>                       // for vector

#include <gperftools/malloc_extension.h>
#include <gperftools/malloc_hook.h>         // for MallocHook
#include <gperftools/nallocx.h>
#include "base/basictypes.h"            // for int64
#include "base/commandlineflags.h"      // for RegisterFlagValidator, etc
#include "base/dynamic_annotations.h"   // for RunningOnValgrind
#include "base/spinlock.h"              // for SpinLockHolder
#include "central_freelist.h"  // for CentralFreeListPadded
#include "common.h"            // for StackTrace, kPageShift, etc
#include "internal_logging.h"  // for ASSERT, TCMalloc_Printer, etc
#include "linked_list.h"       // for SLL_SetNext
#include "malloc_hook-inl.h"       // for MallocHook::InvokeNewHook, etc
#include "page_heap.h"         // for PageHeap, PageHeap::Stats
#include "page_heap_allocator.h"  // for PageHeapAllocator
#include "span.h"              // for Span, DLL_Prepend, etc
#include "stack_trace_table.h"  // for StackTraceTable
#include "static_vars.h"       // for Static
#include "system-alloc.h"      // for DumpSystemAllocatorStats, etc
#include "tcmalloc_guard.h"    // for TCMallocGuard
#include "thread_cache.h"      // for ThreadCache

#include "maybe_emergency_malloc.h"

#if (defined(_WIN32) && !defined(__CYGWIN__) && !defined(__CYGWIN32__)) && !defined(WIN32_OVERRIDE_ALLOCATORS)
# define WIN32_DO_PATCHING 1
#endif

// Some windows file somewhere (at least on cygwin) #define's small (!)
#undef small

using std::max;
using std::min;
using std::numeric_limits;
using std::vector;

#include "libc_override.h"

using tcmalloc::AlignmentForSize;
using tcmalloc::kLog;
using tcmalloc::kCrash;
using tcmalloc::kCrashWithStats;
using tcmalloc::Log;
using tcmalloc::PageHeap;
using tcmalloc::PageHeapAllocator;
using tcmalloc::SizeMap;
using tcmalloc::Span;
using tcmalloc::StackTrace;
using tcmalloc::Static;
using tcmalloc::ThreadCache;

DECLARE_double(tcmalloc_release_rate);
DECLARE_int64(tcmalloc_heap_limit_mb);

// Those common architectures are known to be safe w.r.t. aliasing function
// with "extra" unused args to function with fewer arguments (e.g.
// tc_delete_nothrow being aliased to tc_delete).
//
// Benefit of aliasing is relatively moderate. It reduces instruction
// cache pressure a bit (not relevant for largely unused
// tc_delete_nothrow, but is potentially relevant for
// tc_delete_aligned (or sized)). It also used to be the case that gcc
// 5+ optimization for merging identical functions kicked in and
// "screwed" one of the otherwise identical functions with extra
// jump. I am not able to reproduce that anymore.
#if !defined(__i386__) && !defined(__x86_64__) && \
    !defined(__ppc__) && !defined(__PPC__) && \
    !defined(__aarch64__) && !defined(__mips__) && !defined(__arm__)
#undef TCMALLOC_NO_ALIASES
#define TCMALLOC_NO_ALIASES
#endif

#if defined(__GNUC__) && defined(__ELF__) && !defined(TCMALLOC_NO_ALIASES)
#define TC_ALIAS(name) __attribute__((alias(#name)))
#endif

// For windows, the printf we use to report large allocs is
// potentially dangerous: it could cause a malloc that would cause an
// infinite loop.  So by default we set the threshold to a huge number
// on windows, so this bad situation will never trigger.  You can
// always set TCMALLOC_LARGE_ALLOC_REPORT_THRESHOLD manually if you
// want this functionality.
#ifdef _WIN32
const int64 kDefaultLargeAllocReportThreshold = static_cast<int64>(1) << 62;
#else
const int64 kDefaultLargeAllocReportThreshold = static_cast<int64>(1) << 30;
#endif
DEFINE_int64(tcmalloc_large_alloc_report_threshold,
             EnvToInt64("TCMALLOC_LARGE_ALLOC_REPORT_THRESHOLD",
                        kDefaultLargeAllocReportThreshold),
             "Allocations larger than this value cause a stack "
             "trace to be dumped to stderr.  The threshold for "
             "dumping stack traces is increased by a factor of 1.125 "
             "every time we print a message so that the threshold "
             "automatically goes up by a factor of ~1000 every 60 "
             "messages.  This bounds the amount of extra logging "
             "generated by this flag.  Default value of this flag "
             "is very large and therefore you should see no extra "
             "logging unless the flag is overridden.  Set to 0 to "
             "disable reporting entirely.");


// We already declared these functions in tcmalloc.h, but we have to
// declare them again to give them an ATTRIBUTE_SECTION: we want to
// put all callers of MallocHook::Invoke* in this module into
// ATTRIBUTE_SECTION(google_malloc) section, so that
// MallocHook::GetCallerStackTrace can function accurately.
#ifndef _WIN32   // windows doesn't have attribute_section, so don't bother
extern "C" {
  void* tc_malloc(size_t size) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void tc_free(void* ptr) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void tc_free_sized(void* ptr, size_t size) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void* tc_realloc(void* ptr, size_t size) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void* tc_calloc(size_t nmemb, size_t size) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void tc_cfree(void* ptr) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);

  void* tc_memalign(size_t __alignment, size_t __size) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  int tc_posix_memalign(void** ptr, size_t align, size_t size) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void* tc_valloc(size_t __size) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void* tc_pvalloc(size_t __size) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);

  void tc_malloc_stats(void) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  int tc_mallopt(int cmd, int value) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
#ifdef HAVE_STRUCT_MALLINFO
  struct mallinfo tc_mallinfo(void) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
#endif

  void* tc_new(size_t size)
      ATTRIBUTE_SECTION(google_malloc);
  void tc_delete(void* p) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void tc_delete_sized(void* p, size_t size) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void* tc_newarray(size_t size)
      ATTRIBUTE_SECTION(google_malloc);
  void tc_deletearray(void* p) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void tc_deletearray_sized(void* p, size_t size) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);

  // And the nothrow variants of these:
  void* tc_new_nothrow(size_t size, const std::nothrow_t&) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void* tc_newarray_nothrow(size_t size, const std::nothrow_t&) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  // Surprisingly, standard C++ library implementations use a
  // nothrow-delete internally.  See, eg:
  // http://www.dinkumware.com/manuals/?manual=compleat&page=new.html
  void tc_delete_nothrow(void* ptr, const std::nothrow_t&) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void tc_deletearray_nothrow(void* ptr, const std::nothrow_t&) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);

#if defined(ENABLE_ALIGNED_NEW_DELETE)

  void* tc_new_aligned(size_t size, std::align_val_t al)
      ATTRIBUTE_SECTION(google_malloc);
  void tc_delete_aligned(void* p, std::align_val_t al) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void tc_delete_sized_aligned(void* p, size_t size, std::align_val_t al) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void* tc_newarray_aligned(size_t size, std::align_val_t al)
      ATTRIBUTE_SECTION(google_malloc);
  void tc_deletearray_aligned(void* p, std::align_val_t al) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void tc_deletearray_sized_aligned(void* p, size_t size, std::align_val_t al) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);

  // And the nothrow variants of these:
  void* tc_new_aligned_nothrow(size_t size, std::align_val_t al, const std::nothrow_t&) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void* tc_newarray_aligned_nothrow(size_t size, std::align_val_t al, const std::nothrow_t&) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void tc_delete_aligned_nothrow(void* ptr, std::align_val_t al, const std::nothrow_t&) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
  void tc_deletearray_aligned_nothrow(void* ptr, std::align_val_t al, const std::nothrow_t&) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);

#endif // defined(ENABLE_ALIGNED_NEW_DELETE)

  // Some non-standard extensions that we support.

  // This is equivalent to
  //    OS X: malloc_size()
  //    glibc: malloc_usable_size()
  //    Windows: _msize()
  size_t tc_malloc_size(void* p) PERFTOOLS_NOTHROW
      ATTRIBUTE_SECTION(google_malloc);
}  // extern "C"
#endif  // #ifndef _WIN32

// ----------------------- IMPLEMENTATION -------------------------------

static int tc_new_mode = 0;  // See tc_set_new_mode().

// Routines such as free() and realloc() catch some erroneous pointers
// passed to them, and invoke the below when they do.  (An erroneous pointer
// won't be caught if it's within a valid span or a stale span for which
// the pagemap cache has a non-zero sizeclass.) This is a cheap (source-editing
// required) kind of exception handling for these routines.
namespace {
ATTRIBUTE_NOINLINE void InvalidFree(void* ptr) {
  if (tcmalloc::IsEmergencyPtr(ptr)) {
    tcmalloc::EmergencyFree(ptr);
    return;
  }
  Log(kCrash, __FILE__, __LINE__, "Attempt to free invalid pointer", ptr);
}

size_t InvalidGetSizeForRealloc(const void* old_ptr) {
  Log(kCrash, __FILE__, __LINE__,
      "Attempt to realloc invalid pointer", old_ptr);
  return 0;
}

size_t InvalidGetAllocatedSize(const void* ptr) {
  Log(kCrash, __FILE__, __LINE__,
      "Attempt to get the size of an invalid pointer", ptr);
  return 0;
}
}  // unnamed namespace

// Extract interesting stats
struct TCMallocStats {
  uint64_t thread_bytes;      // Bytes in thread caches
  uint64_t central_bytes;     // Bytes in central cache
  uint64_t transfer_bytes;    // Bytes in central transfer cache
  uint64_t metadata_bytes;    // Bytes alloced for metadata
  PageHeap::Stats pageheap;   // Stats from page heap
};

// Get stats into "r".  Also, if class_count != NULL, class_count[k]
// will be set to the total number of objects of size class k in the
// central cache, transfer cache, and per-thread caches. If small_spans
// is non-NULL, it is filled.  Same for large_spans.
static void ExtractStats(TCMallocStats* r, uint64_t* class_count,
                         PageHeap::SmallSpanStats* small_spans,
                         PageHeap::LargeSpanStats* large_spans) {
  r->central_bytes = 0;
  r->transfer_bytes = 0;
  for (int cl = 0; cl < Static::num_size_classes(); ++cl) {
    const int length = Static::central_cache()[cl].length();
    const int tc_length = Static::central_cache()[cl].tc_length();
    const size_t cache_overhead = Static::central_cache()[cl].OverheadBytes();
    const size_t size = static_cast<uint64_t>(
        Static::sizemap()->ByteSizeForClass(cl));
    r->central_bytes += (size * length) + cache_overhead;
    r->transfer_bytes += (size * tc_length);
    if (class_count) {
      // Sum the lengths of all per-class freelists, except the per-thread
      // freelists, which get counted when we call GetThreadStats(), below.
      class_count[cl] = length + tc_length;
    }

  }

  // Add stats from per-thread heaps
  r->thread_bytes = 0;
  { // scope
    SpinLockHolder h(Static::pageheap_lock());
    ThreadCache::GetThreadStats(&r->thread_bytes, class_count);
    r->metadata_bytes = tcmalloc::metadata_system_bytes();
    r->pageheap = Static::pageheap()->stats();
    if (small_spans != NULL) {
      Static::pageheap()->GetSmallSpanStats(small_spans);
    }
    if (large_spans != NULL) {
      Static::pageheap()->GetLargeSpanStats(large_spans);
    }
  }
}

static double PagesToMiB(uint64_t pages) {
  return (pages << kPageShift) / 1048576.0;
}

// WRITE stats to "out"
static void DumpStats(TCMalloc_Printer* out, int level) {
  TCMallocStats stats;
  uint64_t class_count[kClassSizesMax];
  PageHeap::SmallSpanStats small;
  PageHeap::LargeSpanStats large;
  if (level >= 2) {
    ExtractStats(&stats, class_count, &small, &large);
  } else {
    ExtractStats(&stats, NULL, NULL, NULL);
  }

  static const double MiB = 1048576.0;

  const uint64_t virtual_memory_used = (stats.pageheap.system_bytes
                                        + stats.metadata_bytes);
  const uint64_t physical_memory_used = (virtual_memory_used
                                         - stats.pageheap.unmapped_bytes);
  const uint64_t bytes_in_use_by_app = (physical_memory_used
                                        - stats.metadata_bytes
                                        - stats.pageheap.free_bytes
                                        - stats.central_bytes
                                        - stats.transfer_bytes
                                        - stats.thread_bytes);

#ifdef TCMALLOC_SMALL_BUT_SLOW
  out->printf(
      "NOTE:  SMALL MEMORY MODEL IS IN USE, PERFORMANCE MAY SUFFER.\n");
#endif
  out->printf(
      "------------------------------------------------\n"
      "MALLOC:   %12" PRIu64 " (%7.1f MiB) Bytes in use by application\n"
      "MALLOC: + %12" PRIu64 " (%7.1f MiB) Bytes in page heap freelist\n"
      "MALLOC: + %12" PRIu64 " (%7.1f MiB) Bytes in central cache freelist\n"
      "MALLOC: + %12" PRIu64 " (%7.1f MiB) Bytes in transfer cache freelist\n"
      "MALLOC: + %12" PRIu64 " (%7.1f MiB) Bytes in thread cache freelists\n"
      "MALLOC: + %12" PRIu64 " (%7.1f MiB) Bytes in malloc metadata\n"
      "MALLOC:   ------------\n"
      "MALLOC: = %12" PRIu64 " (%7.1f MiB) Actual memory used (physical + swap)\n"
      "MALLOC: + %12" PRIu64 " (%7.1f MiB) Bytes released to OS (aka unmapped)\n"
      "MALLOC:   ------------\n"
      "MALLOC: = %12" PRIu64 " (%7.1f MiB) Virtual address space used\n"
      "MALLOC:\n"
      "MALLOC:   %12" PRIu64 "              Spans in use\n"
      "MALLOC:   %12" PRIu64 "              Thread heaps in use\n"
      "MALLOC:   %12" PRIu64 "              Tcmalloc page size\n"
      "------------------------------------------------\n"
      "Call ReleaseFreeMemory() to release freelist memory to the OS"
      " (via madvise()).\n"
      "Bytes released to the OS take up virtual address space"
      " but no physical memory.\n",
      bytes_in_use_by_app, bytes_in_use_by_app / MiB,
      stats.pageheap.free_bytes, stats.pageheap.free_bytes / MiB,
      stats.central_bytes, stats.central_bytes / MiB,
      stats.transfer_bytes, stats.transfer_bytes / MiB,
      stats.thread_bytes, stats.thread_bytes / MiB,
      stats.metadata_bytes, stats.metadata_bytes / MiB,
      physical_memory_used, physical_memory_used / MiB,
      stats.pageheap.unmapped_bytes, stats.pageheap.unmapped_bytes / MiB,
      virtual_memory_used, virtual_memory_used / MiB,
      uint64_t(Static::span_allocator()->inuse()),
      uint64_t(ThreadCache::HeapsInUse()),
      uint64_t(kPageSize));

  if (level >= 2) {
    out->printf("------------------------------------------------\n");
    out->printf("Total size of freelists for per-thread caches,\n");
    out->printf("transfer cache, and central cache, by size class\n");
    out->printf("------------------------------------------------\n");
    uint64_t cumulative_bytes = 0;
    uint64_t cumulative_overhead = 0;
    for (uint32 cl = 0; cl < Static::num_size_classes(); ++cl) {
      if (class_count[cl] > 0) {
        size_t cl_size = Static::sizemap()->ByteSizeForClass(cl);
        const uint64_t class_bytes = class_count[cl] * cl_size;
        cumulative_bytes += class_bytes;
        const uint64_t class_overhead =
            Static::central_cache()[cl].OverheadBytes();
        cumulative_overhead += class_overhead;
        out->printf("class %3d [ %8zu bytes ] : "
                "%8" PRIu64 " objs; %5.1f MiB; %5.1f cum MiB; "
                "%8.3f overhead MiB; %8.3f cum overhead MiB\n",
                cl, cl_size,
                class_count[cl],
                class_bytes / MiB,
                cumulative_bytes / MiB,
                class_overhead / MiB,
                cumulative_overhead / MiB);
      }
    }

    // append page heap info
    int nonempty_sizes = 0;
    for (int s = 0; s < kMaxPages; s++) {
      if (small.normal_length[s] + small.returned_length[s] > 0) {
        nonempty_sizes++;
      }
    }
    out->printf("------------------------------------------------\n");
    out->printf("PageHeap: %d sizes; %6.1f MiB free; %6.1f MiB unmapped\n",
                nonempty_sizes, stats.pageheap.free_bytes / MiB,
                stats.pageheap.unmapped_bytes / MiB);
    out->printf("------------------------------------------------\n");
    uint64_t total_normal = 0;
    uint64_t total_returned = 0;
    for (int s = 1; s <= kMaxPages; s++) {
      const int n_length = small.normal_length[s - 1];
      const int r_length = small.returned_length[s - 1];
      if (n_length + r_length > 0) {
        uint64_t n_pages = s * n_length;
        uint64_t r_pages = s * r_length;
        total_normal += n_pages;
        total_returned += r_pages;
        out->printf("%6u pages * %6u spans ~ %6.1f MiB; %6.1f MiB cum"
                    "; unmapped: %6.1f MiB; %6.1f MiB cum\n",
                    s,
                    (n_length + r_length),
                    PagesToMiB(n_pages + r_pages),
                    PagesToMiB(total_normal + total_returned),
                    PagesToMiB(r_pages),
                    PagesToMiB(total_returned));
      }
    }

    total_normal += large.normal_pages;
    total_returned += large.returned_pages;
    out->printf(">%-5u large * %6u spans ~ %6.1f MiB; %6.1f MiB cum"
                "; unmapped: %6.1f MiB; %6.1f MiB cum\n",
                static_cast<unsigned int>(kMaxPages),
                static_cast<unsigned int>(large.spans),
                PagesToMiB(large.normal_pages + large.returned_pages),
                PagesToMiB(total_normal + total_returned),
                PagesToMiB(large.returned_pages),
                PagesToMiB(total_returned));
  }
}

static void PrintStats(int level) {
  const int kBufferSize = 16 << 10;
  char* buffer = new char[kBufferSize];
  TCMalloc_Printer printer(buffer, kBufferSize);
  DumpStats(&printer, level);
  write(STDERR_FILENO, buffer, strlen(buffer));
  delete[] buffer;
}

static void** DumpHeapGrowthStackTraces() {
  // Count how much space we need
  int needed_slots = 0;
  {
    SpinLockHolder h(Static::pageheap_lock());
    for (StackTrace* t = Static::growth_stacks();
         t != NULL;
         t = reinterpret_cast<StackTrace*>(
             t->stack[tcmalloc::kMaxStackDepth-1])) {
      needed_slots += 3 + t->depth;
    }
    needed_slots += 100;            // Slop in case list grows
    needed_slots += needed_slots/8; // An extra 12.5% slop
  }

  void** result = new void*[needed_slots];
  if (result == NULL) {
    Log(kLog, __FILE__, __LINE__,
        "tcmalloc: allocation failed for stack trace slots",
        needed_slots * sizeof(*result));
    return NULL;
  }

  SpinLockHolder h(Static::pageheap_lock());
  int used_slots = 0;
  for (StackTrace* t = Static::growth_stacks();
       t != NULL;
       t = reinterpret_cast<StackTrace*>(
           t->stack[tcmalloc::kMaxStackDepth-1])) {
    ASSERT(used_slots < needed_slots);  // Need to leave room for terminator
    if (used_slots + 3 + t->depth >= needed_slots) {
      // No more room
      break;
    }

    result[used_slots+0] = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    result[used_slots+1] = reinterpret_cast<void*>(t->size);
    result[used_slots+2] = reinterpret_cast<void*>(t->depth);
    for (int d = 0; d < t->depth; d++) {
      result[used_slots+3+d] = t->stack[d];
    }
    used_slots += 3 + t->depth;
  }
  result[used_slots] = reinterpret_cast<void*>(static_cast<uintptr_t>(0));
  return result;
}

static void IterateOverRanges(void* arg, MallocExtension::RangeFunction func) {
  PageID page = 1;  // Some code may assume that page==0 is never used
  bool done = false;
  while (!done) {
    // Accumulate a small number of ranges in a local buffer
    static const int kNumRanges = 16;
    static base::MallocRange ranges[kNumRanges];
    int n = 0;
    {
      SpinLockHolder h(Static::pageheap_lock());
      while (n < kNumRanges) {
        if (!Static::pageheap()->GetNextRange(page, &ranges[n])) {
          done = true;
          break;
        } else {
          uintptr_t limit = ranges[n].address + ranges[n].length;
          page = (limit + kPageSize - 1) >> kPageShift;
          n++;
        }
      }
    }

    for (int i = 0; i < n; i++) {
      (*func)(arg, &ranges[i]);
    }
  }
}

// TCMalloc's support for extra malloc interfaces
class TCMallocImplementation : public MallocExtension {
 private:
  // ReleaseToSystem() might release more than the requested bytes because
  // the page heap releases at the span granularity, and spans are of wildly
  // different sizes.  This member keeps track of the extra bytes bytes
  // released so that the app can periodically call ReleaseToSystem() to
  // release memory at a constant rate.
  // NOTE: Protected by Static::pageheap_lock().
  size_t extra_bytes_released_;

 public:
  TCMallocImplementation()
      : extra_bytes_released_(0) {
  }

  virtual void GetStats(char* buffer, int buffer_length) {
    ASSERT(buffer_length > 0);
    TCMalloc_Printer printer(buffer, buffer_length);

    // Print level one stats unless lots of space is available
    if (buffer_length < 10000) {
      DumpStats(&printer, 1);
    } else {
      DumpStats(&printer, 2);
    }
  }

  // We may print an extra, tcmalloc-specific warning message here.
  virtual void GetHeapSample(MallocExtensionWriter* writer) {
    if (FLAGS_tcmalloc_sample_parameter == 0) {
      const char* const kWarningMsg =
          "%warn\n"
          "%warn This heap profile does not have any data in it, because\n"
          "%warn the application was run with heap sampling turned off.\n"
          "%warn To get useful data from GetHeapSample(), you must\n"
          "%warn set the environment variable TCMALLOC_SAMPLE_PARAMETER to\n"
          "%warn a positive sampling period, such as 524288.\n"
          "%warn\n";
      writer->append(kWarningMsg, strlen(kWarningMsg));
    }
    MallocExtension::GetHeapSample(writer);
  }

  virtual void** ReadStackTraces(int* sample_period) {
    tcmalloc::StackTraceTable table;
    {
      SpinLockHolder h(Static::pageheap_lock());
      Span* sampled = Static::sampled_objects();
      for (Span* s = sampled->next; s != sampled; s = s->next) {
        table.AddTrace(*reinterpret_cast<StackTrace*>(s->objects));
      }
    }
    *sample_period = ThreadCache::GetCache()->GetSamplePeriod();
    return table.ReadStackTracesAndClear(); // grabs and releases pageheap_lock
  }

  virtual void** ReadHeapGrowthStackTraces() {
    return DumpHeapGrowthStackTraces();
  }

  virtual size_t GetThreadCacheSize() {
    ThreadCache* tc = ThreadCache::GetCacheIfPresent();
    if (!tc)
      return 0;
    return tc->Size();
  }

  virtual void MarkThreadTemporarilyIdle() {
    ThreadCache::BecomeTemporarilyIdle();
  }

  virtual void Ranges(void* arg, RangeFunction func) {
    IterateOverRanges(arg, func);
  }

  virtual bool GetNumericProperty(const char* name, size_t* value) {
    ASSERT(name != NULL);

    if (strcmp(name, "generic.current_allocated_bytes") == 0) {
      TCMallocStats stats;
      ExtractStats(&stats, NULL, NULL, NULL);
      *value = stats.pageheap.system_bytes
               - stats.thread_bytes
               - stats.central_bytes
               - stats.transfer_bytes
               - stats.pageheap.free_bytes
               - stats.pageheap.unmapped_bytes;
      return true;
    }

    if (strcmp(name, "generic.heap_size") == 0) {
      TCMallocStats stats;
      ExtractStats(&stats, NULL, NULL, NULL);
      *value = stats.pageheap.system_bytes;
      return true;
    }

    if (strcmp(name, "generic.total_physical_bytes") == 0) {
      TCMallocStats stats;
      ExtractStats(&stats, NULL, NULL, NULL);
      *value = stats.pageheap.system_bytes + stats.metadata_bytes -
               stats.pageheap.unmapped_bytes;
      return true;
    }

    if (strcmp(name, "tcmalloc.slack_bytes") == 0) {
      // Kept for backwards compatibility.  Now defined externally as:
      //    pageheap_free_bytes + pageheap_unmapped_bytes.
      SpinLockHolder l(Static::pageheap_lock());
      PageHeap::Stats stats = Static::pageheap()->stats();
      *value = stats.free_bytes + stats.unmapped_bytes;
      return true;
    }

    if (strcmp(name, "tcmalloc.central_cache_free_bytes") == 0) {
      TCMallocStats stats;
      ExtractStats(&stats, NULL, NULL, NULL);
      *value = stats.central_bytes;
      return true;
    }

    if (strcmp(name, "tcmalloc.transfer_cache_free_bytes") == 0) {
      TCMallocStats stats;
      ExtractStats(&stats, NULL, NULL, NULL);
      *value = stats.transfer_bytes;
      return true;
    }

    if (strcmp(name, "tcmalloc.thread_cache_free_bytes") == 0) {
      TCMallocStats stats;
      ExtractStats(&stats, NULL, NULL, NULL);
      *value = stats.thread_bytes;
      return true;
    }

    if (strcmp(name, "tcmalloc.pageheap_free_bytes") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      *value = Static::pageheap()->stats().free_bytes;
      return true;
    }

    if (strcmp(name, "tcmalloc.pageheap_unmapped_bytes") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      *value = Static::pageheap()->stats().unmapped_bytes;
      return true;
    }

    if (strcmp(name, "tcmalloc.pageheap_committed_bytes") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      *value = Static::pageheap()->stats().committed_bytes;
      return true;
    }

    if (strcmp(name, "tcmalloc.pageheap_scavenge_count") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      *value = Static::pageheap()->stats().scavenge_count;
      return true;
    }

    if (strcmp(name, "tcmalloc.pageheap_commit_count") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      *value = Static::pageheap()->stats().commit_count;
      return true;
    }

    if (strcmp(name, "tcmalloc.pageheap_total_commit_bytes") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      *value = Static::pageheap()->stats().total_commit_bytes;
      return true;
    }

    if (strcmp(name, "tcmalloc.pageheap_decommit_count") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      *value = Static::pageheap()->stats().decommit_count;
      return true;
    }

    if (strcmp(name, "tcmalloc.pageheap_total_decommit_bytes") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      *value = Static::pageheap()->stats().total_decommit_bytes;
      return true;
    }

    if (strcmp(name, "tcmalloc.pageheap_reserve_count") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      *value = Static::pageheap()->stats().reserve_count;
      return true;
    }

    if (strcmp(name, "tcmalloc.pageheap_total_reserve_bytes") == 0) {
        SpinLockHolder l(Static::pageheap_lock());
        *value = Static::pageheap()->stats().total_reserve_bytes;
        return true;
    }

    if (strcmp(name, "tcmalloc.max_total_thread_cache_bytes") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      *value = ThreadCache::overall_thread_cache_size();
      return true;
    }

    if (strcmp(name, "tcmalloc.current_total_thread_cache_bytes") == 0) {
      TCMallocStats stats;
      ExtractStats(&stats, NULL, NULL, NULL);
      *value = stats.thread_bytes;
      return true;
    }

    if (strcmp(name, "tcmalloc.aggressive_memory_decommit") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      *value = size_t(Static::pageheap()->GetAggressiveDecommit());
      return true;
    }

    if (strcmp(name, "tcmalloc.heap_limit_mb") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      *value = FLAGS_tcmalloc_heap_limit_mb;
      return true;
    }

    return false;
  }

  virtual bool SetNumericProperty(const char* name, size_t value) {
    ASSERT(name != NULL);

    if (strcmp(name, "tcmalloc.max_total_thread_cache_bytes") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      ThreadCache::set_overall_thread_cache_size(value);
      return true;
    }

    if (strcmp(name, "tcmalloc.aggressive_memory_decommit") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      Static::pageheap()->SetAggressiveDecommit(value != 0);
      return true;
    }

    if (strcmp(name, "tcmalloc.heap_limit_mb") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      FLAGS_tcmalloc_heap_limit_mb = value;
      return true;
    }

    return false;
  }

  virtual void MarkThreadIdle() {
    ThreadCache::BecomeIdle();
  }

  virtual void MarkThreadBusy();  // Implemented below

  virtual SysAllocator* GetSystemAllocator() {
    SpinLockHolder h(Static::pageheap_lock());
    return tcmalloc_sys_alloc;
  }

  virtual void SetSystemAllocator(SysAllocator* alloc) {
    SpinLockHolder h(Static::pageheap_lock());
    tcmalloc_sys_alloc = alloc;
  }

  virtual void ReleaseToSystem(size_t num_bytes) {
    SpinLockHolder h(Static::pageheap_lock());
    if (num_bytes <= extra_bytes_released_) {
      // We released too much on a prior call, so don't release any
      // more this time.
      extra_bytes_released_ = extra_bytes_released_ - num_bytes;
      return;
    }
    num_bytes = num_bytes - extra_bytes_released_;
    // num_bytes might be less than one page.  If we pass zero to
    // ReleaseAtLeastNPages, it won't do anything, so we release a whole
    // page now and let extra_bytes_released_ smooth it out over time.
    Length num_pages = max<Length>(num_bytes >> kPageShift, 1);
    size_t bytes_released = Static::pageheap()->ReleaseAtLeastNPages(
        num_pages) << kPageShift;
    if (bytes_released > num_bytes) {
      extra_bytes_released_ = bytes_released - num_bytes;
    } else {
      // The PageHeap wasn't able to release num_bytes.  Don't try to
      // compensate with a big release next time.  Specifically,
      // ReleaseFreeMemory() calls ReleaseToSystem(LONG_MAX).
      extra_bytes_released_ = 0;
    }
  }

  virtual void SetMemoryReleaseRate(double rate) {
    FLAGS_tcmalloc_release_rate = rate;
  }

  virtual double GetMemoryReleaseRate() {
    return FLAGS_tcmalloc_release_rate;
  }
  virtual size_t GetEstimatedAllocatedSize(size_t size);

  // This just calls GetSizeWithCallback, but because that's in an
  // unnamed namespace, we need to move the definition below it in the
  // file.
  virtual size_t GetAllocatedSize(const void* ptr);

  // This duplicates some of the logic in GetSizeWithCallback, but is
  // faster.  This is important on OS X, where this function is called
  // on every allocation operation.
  virtual Ownership GetOwnership(const void* ptr) {
    const PageID p = reinterpret_cast<uintptr_t>(ptr) >> kPageShift;
    // The rest of tcmalloc assumes that all allocated pointers use at
    // most kAddressBits bits.  If ptr doesn't, then it definitely
    // wasn't alloacted by tcmalloc.
    if ((p >> (kAddressBits - kPageShift)) > 0) {
      return kNotOwned;
    }
    uint32 cl;
    if (Static::pageheap()->TryGetSizeClass(p, &cl)) {
      return kOwned;
    }
    const Span *span = Static::pageheap()->GetDescriptor(p);
    return span ? kOwned : kNotOwned;
  }

  virtual void GetFreeListSizes(vector<MallocExtension::FreeListInfo>* v) {
    static const char kCentralCacheType[] = "tcmalloc.central";
    static const char kTransferCacheType[] = "tcmalloc.transfer";
    static const char kThreadCacheType[] = "tcmalloc.thread";
    static const char kPageHeapType[] = "tcmalloc.page";
    static const char kPageHeapUnmappedType[] = "tcmalloc.page_unmapped";
    static const char kLargeSpanType[] = "tcmalloc.large";
    static const char kLargeUnmappedSpanType[] = "tcmalloc.large_unmapped";

    v->clear();

    // central class information
    int64 prev_class_size = 0;
    for (int cl = 1; cl < Static::num_size_classes(); ++cl) {
      size_t class_size = Static::sizemap()->ByteSizeForClass(cl);
      MallocExtension::FreeListInfo i;
      i.min_object_size = prev_class_size + 1;
      i.max_object_size = class_size;
      i.total_bytes_free =
          Static::central_cache()[cl].length() * class_size;
      i.type = kCentralCacheType;
      v->push_back(i);

      // transfer cache
      i.total_bytes_free =
          Static::central_cache()[cl].tc_length() * class_size;
      i.type = kTransferCacheType;
      v->push_back(i);

      prev_class_size = Static::sizemap()->ByteSizeForClass(cl);
    }

    // Add stats from per-thread heaps
    uint64_t class_count[kClassSizesMax];
    memset(class_count, 0, sizeof(class_count));
    {
      SpinLockHolder h(Static::pageheap_lock());
      uint64_t thread_bytes = 0;
      ThreadCache::GetThreadStats(&thread_bytes, class_count);
    }

    prev_class_size = 0;
    for (int cl = 1; cl < Static::num_size_classes(); ++cl) {
      MallocExtension::FreeListInfo i;
      i.min_object_size = prev_class_size + 1;
      i.max_object_size = Static::sizemap()->ByteSizeForClass(cl);
      i.total_bytes_free =
          class_count[cl] * Static::sizemap()->ByteSizeForClass(cl);
      i.type = kThreadCacheType;
      v->push_back(i);

      prev_class_size = Static::sizemap()->ByteSizeForClass(cl);
    }

    // append page heap info
    PageHeap::SmallSpanStats small;
    PageHeap::LargeSpanStats large;
    {
      SpinLockHolder h(Static::pageheap_lock());
      Static::pageheap()->GetSmallSpanStats(&small);
      Static::pageheap()->GetLargeSpanStats(&large);
    }

    // large spans: mapped
    MallocExtension::FreeListInfo span_info;
    span_info.type = kLargeSpanType;
    span_info.max_object_size = (numeric_limits<size_t>::max)();
    span_info.min_object_size = kMaxPages << kPageShift;
    span_info.total_bytes_free = large.normal_pages << kPageShift;
    v->push_back(span_info);

    // large spans: unmapped
    span_info.type = kLargeUnmappedSpanType;
    span_info.total_bytes_free = large.returned_pages << kPageShift;
    v->push_back(span_info);

    // small spans
    for (int s = 1; s <= kMaxPages; s++) {
      MallocExtension::FreeListInfo i;
      i.max_object_size = (s << kPageShift);
      i.min_object_size = ((s - 1) << kPageShift);

      i.type = kPageHeapType;
      i.total_bytes_free = (s << kPageShift) * small.normal_length[s - 1];
      v->push_back(i);

      i.type = kPageHeapUnmappedType;
      i.total_bytes_free = (s << kPageShift) * small.returned_length[s - 1];
      v->push_back(i);
    }
  }
};

static inline ATTRIBUTE_ALWAYS_INLINE
size_t align_size_up(size_t size, size_t align) {
  ASSERT(align <= kPageSize);
  size_t new_size = (size + align - 1) & ~(align - 1);
  if (PREDICT_FALSE(new_size == 0)) {
    // Note, new_size == 0 catches both integer overflow and size
    // being 0.
    if (size == 0) {
      new_size = align;
    } else {
      new_size = size;
    }
  }
  return new_size;
}

// Puts in *cl size class that is suitable for allocation of size bytes with
// align alignment. Returns true if such size class exists and false otherwise.
static bool size_class_with_alignment(size_t size, size_t align, uint32_t* cl) {
  if (PREDICT_FALSE(align > kPageSize)) {
    return false;
  }
  size = align_size_up(size, align);
  if (PREDICT_FALSE(!Static::sizemap()->GetSizeClass(size, cl))) {
    return false;
  }
  ASSERT((Static::sizemap()->class_to_size(*cl) & (align - 1)) == 0);
  return true;
}

// nallocx slow path. Moved to a separate function because
// ThreadCache::InitModule is not inlined which would cause nallocx to
// become non-leaf function with stack frame and stack spills.
static ATTRIBUTE_NOINLINE size_t nallocx_slow(size_t size, int flags) {
  if (PREDICT_FALSE(!Static::IsInited())) ThreadCache::InitModule();

  size_t align = static_cast<size_t>(1ull << (flags & 0x3f));
  uint32 cl;
  bool ok = size_class_with_alignment(size, align, &cl);
  if (ok) {
    return Static::sizemap()->ByteSizeForClass(cl);
  } else {
    return tcmalloc::pages(size) << kPageShift;
  }
}

// The nallocx function allocates no memory, but it performs the same size
// computation as the malloc function, and returns the real size of the
// allocation that would result from the equivalent malloc function call.
// nallocx is a malloc extension originally implemented by jemalloc:
// http://www.unix.com/man-page/freebsd/3/nallocx/
extern "C" PERFTOOLS_DLL_DECL
size_t tc_nallocx(size_t size, int flags) {
  if (PREDICT_FALSE(flags != 0)) {
    return nallocx_slow(size, flags);
  }
  uint32 cl;
  // size class 0 is only possible if malloc is not yet initialized
  if (Static::sizemap()->GetSizeClass(size, &cl) && cl != 0) {
    return Static::sizemap()->ByteSizeForClass(cl);
  } else {
    return nallocx_slow(size, 0);
  }
}

extern "C" PERFTOOLS_DLL_DECL
size_t nallocx(size_t size, int flags)
#ifdef TC_ALIAS
  TC_ALIAS(tc_nallocx);
#else
{
  return nallocx_slow(size, flags);
}
#endif


size_t TCMallocImplementation::GetEstimatedAllocatedSize(size_t size) {
  return tc_nallocx(size, 0);
}

// The constructor allocates an object to ensure that initialization
// runs before main(), and therefore we do not have a chance to become
// multi-threaded before initialization.  We also create the TSD key
// here.  Presumably by the time this constructor runs, glibc is in
// good enough shape to handle pthread_key_create().
//
// The constructor also takes the opportunity to tell STL to use
// tcmalloc.  We want to do this early, before construct time, so
// all user STL allocations go through tcmalloc (which works really
// well for STL).
//
// The destructor prints stats when the program exits.
static int tcmallocguard_refcount = 0;  // no lock needed: runs before main()
TCMallocGuard::TCMallocGuard() {
  if (tcmallocguard_refcount++ == 0) {
    ReplaceSystemAlloc();    // defined in libc_override_*.h
    tc_free(tc_malloc(1));
    ThreadCache::InitTSD();
    tc_free(tc_malloc(1));
    // Either we, or debugallocation.cc, or valgrind will control memory
    // management.  We register our extension if we're the winner.
#ifdef TCMALLOC_USING_DEBUGALLOCATION
    // Let debugallocation register its extension.
#else
    if (RunningOnValgrind()) {
      // Let Valgrind uses its own malloc (so don't register our extension).
    } else {
      MallocExtension::Register(new TCMallocImplementation);
    }
#endif
  }
}

TCMallocGuard::~TCMallocGuard() {
  if (--tcmallocguard_refcount == 0) {
    const char* env = NULL;
    if (!RunningOnValgrind()) {
      // Valgrind uses it's own malloc so we cannot do MALLOCSTATS
      env = getenv("MALLOCSTATS");
    }
    if (env != NULL) {
      int level = atoi(env);
      if (level < 1) level = 1;
      PrintStats(level);
    }
  }
}
#ifndef WIN32_OVERRIDE_ALLOCATORS
static TCMallocGuard module_enter_exit_hook;
#endif

//-------------------------------------------------------------------
// Helpers for the exported routines below
//-------------------------------------------------------------------

static inline bool CheckCachedSizeClass(void *ptr) {
  PageID p = reinterpret_cast<uintptr_t>(ptr) >> kPageShift;
  uint32 cached_value;
  if (!Static::pageheap()->TryGetSizeClass(p, &cached_value)) {
    return true;
  }
  return cached_value == Static::pageheap()->GetDescriptor(p)->sizeclass;
}

static inline ATTRIBUTE_ALWAYS_INLINE void* CheckedMallocResult(void *result) {
  ASSERT(result == NULL || CheckCachedSizeClass(result));
  return result;
}

static inline ATTRIBUTE_ALWAYS_INLINE void* SpanToMallocResult(Span *span) {
  Static::pageheap()->InvalidateCachedSizeClass(span->start);
  return
      CheckedMallocResult(reinterpret_cast<void*>(span->start << kPageShift));
}

static void* DoSampledAllocation(size_t size) {
#ifndef NO_TCMALLOC_SAMPLES
  // Grab the stack trace outside the heap lock
  StackTrace tmp;
  tmp.depth = GetStackTrace(tmp.stack, tcmalloc::kMaxStackDepth, 1);
  tmp.size = size;

  SpinLockHolder h(Static::pageheap_lock());
  // Allocate span
  Span *span = Static::pageheap()->New(tcmalloc::pages(size == 0 ? 1 : size));
  if (PREDICT_FALSE(span == NULL)) {
    return NULL;
  }

  // Allocate stack trace
  StackTrace *stack = Static::stacktrace_allocator()->New();
  if (PREDICT_FALSE(stack == NULL)) {
    // Sampling failed because of lack of memory
    return span;
  }
  *stack = tmp;
  span->sample = 1;
  span->objects = stack;
  tcmalloc::DLL_Prepend(Static::sampled_objects(), span);

  return SpanToMallocResult(span);
#else
  abort();
#endif
}

namespace {

typedef void* (*malloc_fn)(void *arg);

SpinLock set_new_handler_lock(SpinLock::LINKER_INITIALIZED);

void* handle_oom(malloc_fn retry_fn,
                 void* retry_arg,
                 bool from_operator,
                 bool nothrow) {
  // we hit out of memory condition, usually if it happens we've
  // called sbrk or mmap and failed, and thus errno is set. But there
  // is support for setting up custom system allocator or setting up
  // page heap size limit, in which cases errno may remain
  // untouched.
  //
  // So we set errno here. C++ operator new doesn't require ENOMEM to
  // be set, but doesn't forbid it too (and often C++ oom does happen
  // with ENOMEM set).
  errno = ENOMEM;
  if (!from_operator && !tc_new_mode) {
    // we're out of memory in C library function (malloc etc) and no
    // "new mode" forced on us. Just return NULL
    return NULL;
  }
  // we're OOM in operator new or "new mode" is set. We might have to
  // call new_handle and maybe retry allocation.

  for (;;) {
    // Get the current new handler.  NB: this function is not
    // thread-safe.  We make a feeble stab at making it so here, but
    // this lock only protects against tcmalloc interfering with
    // itself, not with other libraries calling set_new_handler.
    std::new_handler nh;
    {
      SpinLockHolder h(&set_new_handler_lock);
      nh = std::set_new_handler(0);
      (void) std::set_new_handler(nh);
    }
#if (defined(__GNUC__) && !defined(__EXCEPTIONS)) || (defined(_HAS_EXCEPTIONS) && !_HAS_EXCEPTIONS)
    if (!nh) {
      return NULL;
    }
    // Since exceptions are disabled, we don't really know if new_handler
    // failed.  Assume it will abort if it fails.
    (*nh)();
#else
    // If no new_handler is established, the allocation failed.
    if (!nh) {
      if (nothrow) {
        return NULL;
      }
      throw std::bad_alloc();
    }
    // Otherwise, try the new_handler.  If it returns, retry the
    // allocation.  If it throws std::bad_alloc, fail the allocation.
    // if it throws something else, don't interfere.
    try {
      (*nh)();
    } catch (const std::bad_alloc&) {
      if (!nothrow) throw;
      return NULL;
    }
#endif  // (defined(__GNUC__) && !defined(__EXCEPTIONS)) || (defined(_HAS_EXCEPTIONS) && !_HAS_EXCEPTIONS)

    // we get here if new_handler returns successfully. So we retry
    // allocation.
    void* rv = retry_fn(retry_arg);
    if (rv != NULL) {
      return rv;
    }

    // if allocation failed again we go to next loop iteration
  }
}

// Copy of FLAGS_tcmalloc_large_alloc_report_threshold with
// automatic increases factored in.
#ifdef ENABLE_LARGE_ALLOC_REPORT
static int64_t large_alloc_threshold =
  (kPageSize > FLAGS_tcmalloc_large_alloc_report_threshold
   ? kPageSize : FLAGS_tcmalloc_large_alloc_report_threshold);
#endif

static void ReportLargeAlloc(Length num_pages, void* result) {
  StackTrace stack;
  stack.depth = GetStackTrace(stack.stack, tcmalloc::kMaxStackDepth, 1);

  static const int N = 1000;
  char buffer[N];
  TCMalloc_Printer printer(buffer, N);
  printer.printf("tcmalloc: large alloc %" PRIu64 " bytes == %p @ ",
                 static_cast<uint64>(num_pages) << kPageShift,
                 result);
  for (int i = 0; i < stack.depth; i++) {
    printer.printf(" %p", stack.stack[i]);
  }
  printer.printf("\n");
  write(STDERR_FILENO, buffer, strlen(buffer));
}

// Must be called with the page lock held.
inline bool should_report_large(Length num_pages) {
#ifdef ENABLE_LARGE_ALLOC_REPORT
  const int64 threshold = large_alloc_threshold;
  if (threshold > 0 && num_pages >= (threshold >> kPageShift)) {
    // Increase the threshold by 1/8 every time we generate a report.
    // We cap the threshold at 8GiB to avoid overflow problems.
    large_alloc_threshold = (threshold + threshold/8 < 8ll<<30
                             ? threshold + threshold/8 : 8ll<<30);
    return true;
  }
#endif
  return false;
}

// Helper for do_malloc().
static void* do_malloc_pages(ThreadCache* heap, size_t size) {
  void* result;
  bool report_large;

  Length num_pages = tcmalloc::pages(size);

  // NOTE: we're passing original size here as opposed to rounded-up
  // size as we do in do_malloc_small. The difference is small here
  // (at most 4k out of at least 256k). And not rounding up saves us
  // from possibility of overflow, which rounding up could produce.
  //
  // See https://github.com/gperftools/gperftools/issues/723
  if (heap->SampleAllocation(size)) {
    result = DoSampledAllocation(size);

    SpinLockHolder h(Static::pageheap_lock());
    report_large = should_report_large(num_pages);
  } else {
    SpinLockHolder h(Static::pageheap_lock());
    Span* span = Static::pageheap()->New(num_pages);
    result = (PREDICT_FALSE(span == NULL) ? NULL : SpanToMallocResult(span));
    report_large = should_report_large(num_pages);
  }

  if (report_large) {
    ReportLargeAlloc(num_pages, result);
  }
  return result;
}

static void *nop_oom_handler(size_t size) {
  return NULL;
}

ATTRIBUTE_ALWAYS_INLINE inline void* do_malloc(size_t size) {
  if (PREDICT_FALSE(ThreadCache::IsUseEmergencyMalloc())) {
    return tcmalloc::EmergencyMalloc(size);
  }

  // note: it will force initialization of malloc if necessary
  ThreadCache* cache = ThreadCache::GetCache();
  uint32 cl;

  ASSERT(Static::IsInited());
  ASSERT(cache != NULL);

  if (PREDICT_FALSE(!Static::sizemap()->GetSizeClass(size, &cl))) {
    return do_malloc_pages(cache, size);
  }

  size_t allocated_size = Static::sizemap()->class_to_size(cl);
  if (PREDICT_FALSE(cache->SampleAllocation(allocated_size))) {
    return DoSampledAllocation(size);
  }

  // The common case, and also the simplest.  This just pops the
  // size-appropriate freelist, after replenishing it if it's empty.
  return CheckedMallocResult(cache->Allocate(allocated_size, cl, nop_oom_handler));
}

static void *retry_malloc(void* size) {
  return do_malloc(reinterpret_cast<size_t>(size));
}

ATTRIBUTE_ALWAYS_INLINE inline void* do_malloc_or_cpp_alloc(size_t size) {
  void *rv = do_malloc(size);
  if (PREDICT_TRUE(rv != NULL)) {
    return rv;
  }
  return handle_oom(retry_malloc, reinterpret_cast<void *>(size),
                    false, true);
}

ATTRIBUTE_ALWAYS_INLINE inline void* do_calloc(size_t n, size_t elem_size) {
  // Overflow check
  const size_t size = n * elem_size;
  if (elem_size != 0 && size / elem_size != n) return NULL;

  void* result = do_malloc_or_cpp_alloc(size);
  if (result != NULL) {
    memset(result, 0, tc_nallocx(size, 0));
  }
  return result;
}

// If ptr is NULL, do nothing.  Otherwise invoke the given function.
inline void free_null_or_invalid(void* ptr, void (*invalid_free_fn)(void*)) {
  if (ptr != NULL) {
    (*invalid_free_fn)(ptr);
  }
}

static ATTRIBUTE_NOINLINE void do_free_pages(Span* span, void* ptr) {
  SpinLockHolder h(Static::pageheap_lock());
  if (span->sample) {
    StackTrace* st = reinterpret_cast<StackTrace*>(span->objects);
    tcmalloc::DLL_Remove(span);
    Static::stacktrace_allocator()->Delete(st);
    span->objects = NULL;
  }
  Static::pageheap()->Delete(span);
}

#ifndef NDEBUG
// note, with sized deletions we have no means to support win32
// behavior where we detect "not ours" points and delegate them native
// memory management. This is because nature of sized deletes
// bypassing addr -> size class checks. So in this validation code we
// also assume that sized delete is always used with "our" pointers.
bool ValidateSizeHint(void* ptr, size_t size_hint) {
  const PageID p = reinterpret_cast<uintptr_t>(ptr) >> kPageShift;
  Span* span  = Static::pageheap()->GetDescriptor(p);
  uint32 cl = 0;
  Static::sizemap()->GetSizeClass(size_hint, &cl);
  return (span->sizeclass == cl);
}
#endif

// Helper for the object deletion (free, delete, etc.).  Inputs:
//   ptr is object to be freed
//   invalid_free_fn is a function that gets invoked on certain "bad frees"
//
// We can usually detect the case where ptr is not pointing to a page that
// tcmalloc is using, and in those cases we invoke invalid_free_fn.
ATTRIBUTE_ALWAYS_INLINE inline
void do_free_with_callback(void* ptr,
                           void (*invalid_free_fn)(void*),
                           bool use_hint, size_t size_hint) {
  ThreadCache* heap = ThreadCache::GetCacheIfPresent();

  const PageID p = reinterpret_cast<uintptr_t>(ptr) >> kPageShift;
  uint32 cl;

  ASSERT(!use_hint || ValidateSizeHint(ptr, size_hint));

  if (!use_hint || PREDICT_FALSE(!Static::sizemap()->GetSizeClass(size_hint, &cl))) {
    // if we're in sized delete, but size is too large, no need to
    // probe size cache
    bool cache_hit = !use_hint && Static::pageheap()->TryGetSizeClass(p, &cl);
    if (PREDICT_FALSE(!cache_hit)) {
      Span* span  = Static::pageheap()->GetDescriptor(p);
      if (PREDICT_FALSE(!span)) {
        // span can be NULL because the pointer passed in is NULL or invalid
        // (not something returned by malloc or friends), or because the
        // pointer was allocated with some other allocator besides
        // tcmalloc.  The latter can happen if tcmalloc is linked in via
        // a dynamic library, but is not listed last on the link line.
        // In that case, libraries after it on the link line will
        // allocate with libc malloc, but free with tcmalloc's free.
        free_null_or_invalid(ptr, invalid_free_fn);
        return;
      }
      cl = span->sizeclass;
      if (PREDICT_FALSE(cl == 0)) {
        ASSERT(reinterpret_cast<uintptr_t>(ptr) % kPageSize == 0);
        ASSERT(span != NULL && span->start == p);
        do_free_pages(span, ptr);
        return;
      }
      if (!use_hint) {
        Static::pageheap()->SetCachedSizeClass(p, cl);
      }
    }
  }

  if (PREDICT_TRUE(heap != NULL)) {
    ASSERT(Static::IsInited());
    // If we've hit initialized thread cache, so we're done.
    heap->Deallocate(ptr, cl);
    return;
  }

  if (PREDICT_FALSE(!Static::IsInited())) {
    // if free was called very early we've could have missed the case
    // of invalid or nullptr free. I.e. because probing size classes
    // cache could return bogus result (cl = 0 as of this
    // writing). But since there is no way we could be dealing with
    // ptr we've allocated, since successfull malloc implies IsInited,
    // we can just call "invalid free" handling code.
    free_null_or_invalid(ptr, invalid_free_fn);
    return;
  }

  // Otherwise, delete directly into central cache
  tcmalloc::SLL_SetNext(ptr, NULL);
  Static::central_cache()[cl].InsertRange(ptr, ptr, 1);
}

// The default "do_free" that uses the default callback.
ATTRIBUTE_ALWAYS_INLINE inline void do_free(void* ptr) {
  return do_free_with_callback(ptr, &InvalidFree, false, 0);
}

// NOTE: some logic here is duplicated in GetOwnership (above), for
// speed.  If you change this function, look at that one too.
inline size_t GetSizeWithCallback(const void* ptr,
                                  size_t (*invalid_getsize_fn)(const void*)) {
  if (ptr == NULL)
    return 0;
  const PageID p = reinterpret_cast<uintptr_t>(ptr) >> kPageShift;
  uint32 cl;
  if (Static::pageheap()->TryGetSizeClass(p, &cl)) {
    return Static::sizemap()->ByteSizeForClass(cl);
  }

  const Span *span = Static::pageheap()->GetDescriptor(p);
  if (PREDICT_FALSE(span == NULL)) {  // means we do not own this memory
    return (*invalid_getsize_fn)(ptr);
  }

  if (span->sizeclass != 0) {
    return Static::sizemap()->ByteSizeForClass(span->sizeclass);
  }

  if (span->sample) {
    size_t orig_size = reinterpret_cast<StackTrace*>(span->objects)->size;
    return tc_nallocx(orig_size, 0);
  }

  return span->length << kPageShift;
}

// This lets you call back to a given function pointer if ptr is invalid.
// It is used primarily by windows code which wants a specialized callback.
ATTRIBUTE_ALWAYS_INLINE inline void* do_realloc_with_callback(
    void* old_ptr, size_t new_size,
    void (*invalid_free_fn)(void*),
    size_t (*invalid_get_size_fn)(const void*)) {
  // Get the size of the old entry
  const size_t old_size = GetSizeWithCallback(old_ptr, invalid_get_size_fn);

  // Reallocate if the new size is larger than the old size,
  // or if the new size is significantly smaller than the old size.
  // We do hysteresis to avoid resizing ping-pongs:
  //    . If we need to grow, grow to max(new_size, old_size * 1.X)
  //    . Don't shrink unless new_size < old_size * 0.Y
  // X and Y trade-off time for wasted space.  For now we do 1.25 and 0.5.
  const size_t min_growth = min(old_size / 4,
      (std::numeric_limits<size_t>::max)() - old_size);  // Avoid overflow.
  const size_t lower_bound_to_grow = old_size + min_growth;
  const size_t upper_bound_to_shrink = old_size / 2ul;
  if ((new_size > old_size) || (new_size < upper_bound_to_shrink)) {
    // Need to reallocate.
    void* new_ptr = NULL;

    if (new_size > old_size && new_size < lower_bound_to_grow) {
      new_ptr = do_malloc_or_cpp_alloc(lower_bound_to_grow);
    }
    if (new_ptr == NULL) {
      // Either new_size is not a tiny increment, or last do_malloc failed.
      new_ptr = do_malloc_or_cpp_alloc(new_size);
    }
    if (PREDICT_FALSE(new_ptr == NULL)) {
      return NULL;
    }
    MallocHook::InvokeNewHook(new_ptr, new_size);
    memcpy(new_ptr, old_ptr, ((old_size < new_size) ? old_size : new_size));
    MallocHook::InvokeDeleteHook(old_ptr);
    // We could use a variant of do_free() that leverages the fact
    // that we already know the sizeclass of old_ptr.  The benefit
    // would be small, so don't bother.
    do_free_with_callback(old_ptr, invalid_free_fn, false, 0);
    return new_ptr;
  } else {
    // We still need to call hooks to report the updated size:
    MallocHook::InvokeDeleteHook(old_ptr);
    MallocHook::InvokeNewHook(old_ptr, new_size);
    return old_ptr;
  }
}

ATTRIBUTE_ALWAYS_INLINE inline void* do_realloc(void* old_ptr, size_t new_size) {
  return do_realloc_with_callback(old_ptr, new_size,
                                  &InvalidFree, &InvalidGetSizeForRealloc);
}

static ATTRIBUTE_ALWAYS_INLINE inline
void* do_memalign_pages(size_t align, size_t size) {
  ASSERT((align & (align - 1)) == 0);
  ASSERT(align > kPageSize);
  if (size + align < size) return NULL;         // Overflow

  if (PREDICT_FALSE(Static::pageheap() == NULL)) ThreadCache::InitModule();

  // Allocate at least one byte to avoid boundary conditions below
  if (size == 0) size = 1;

  // We will allocate directly from the page heap
  SpinLockHolder h(Static::pageheap_lock());

  // Allocate extra pages and carve off an aligned portion
  const Length alloc = tcmalloc::pages(size + align);
  Span* span = Static::pageheap()->New(alloc);
  if (PREDICT_FALSE(span == NULL)) return NULL;

  // Skip starting portion so that we end up aligned
  Length skip = 0;
  while ((((span->start+skip) << kPageShift) & (align - 1)) != 0) {
    skip++;
  }
  ASSERT(skip < alloc);
  if (skip > 0) {
    Span* rest = Static::pageheap()->Split(span, skip);
    Static::pageheap()->Delete(span);
    span = rest;
  }

  // Skip trailing portion that we do not need to return
  const Length needed = tcmalloc::pages(size);
  ASSERT(span->length >= needed);
  if (span->length > needed) {
    Span* trailer = Static::pageheap()->Split(span, needed);
    Static::pageheap()->Delete(trailer);
  }
  return SpanToMallocResult(span);
}

// Helpers for use by exported routines below:

inline void do_malloc_stats() {
  PrintStats(1);
}

inline int do_mallopt(int cmd, int value) {
  return 1;     // Indicates error
}

#ifdef HAVE_STRUCT_MALLINFO
inline struct mallinfo do_mallinfo() {
  TCMallocStats stats;
  ExtractStats(&stats, NULL, NULL, NULL);

  // Just some of the fields are filled in.
  struct mallinfo info;
  memset(&info, 0, sizeof(info));

  // Unfortunately, the struct contains "int" field, so some of the
  // size values will be truncated.
  info.arena     = static_cast<int>(stats.pageheap.system_bytes);
  info.fsmblks   = static_cast<int>(stats.thread_bytes
                                    + stats.central_bytes
                                    + stats.transfer_bytes);
  info.fordblks  = static_cast<int>(stats.pageheap.free_bytes +
                                    stats.pageheap.unmapped_bytes);
  info.uordblks  = static_cast<int>(stats.pageheap.system_bytes
                                    - stats.thread_bytes
                                    - stats.central_bytes
                                    - stats.transfer_bytes
                                    - stats.pageheap.free_bytes
                                    - stats.pageheap.unmapped_bytes);

  return info;
}
#endif  // HAVE_STRUCT_MALLINFO

}  // end unnamed namespace

// As promised, the definition of this function, declared above.
size_t TCMallocImplementation::GetAllocatedSize(const void* ptr) {
  if (ptr == NULL)
    return 0;
  ASSERT(TCMallocImplementation::GetOwnership(ptr)
         != TCMallocImplementation::kNotOwned);
  return GetSizeWithCallback(ptr, &InvalidGetAllocatedSize);
}

void TCMallocImplementation::MarkThreadBusy() {
  // Allocate to force the creation of a thread cache, but avoid
  // invoking any hooks.
  do_free(do_malloc(0));
}

//-------------------------------------------------------------------
// Exported routines
//-------------------------------------------------------------------

extern "C" PERFTOOLS_DLL_DECL const char* tc_version(
    int* major, int* minor, const char** patch) PERFTOOLS_NOTHROW {
  if (major) *major = TC_VERSION_MAJOR;
  if (minor) *minor = TC_VERSION_MINOR;
  if (patch) *patch = TC_VERSION_PATCH;
  return TC_VERSION_STRING;
}

// This function behaves similarly to MSVC's _set_new_mode.
// If flag is 0 (default), calls to malloc will behave normally.
// If flag is 1, calls to malloc will behave like calls to new,
// and the std_new_handler will be invoked on failure.
// Returns the previous mode.
extern "C" PERFTOOLS_DLL_DECL int tc_set_new_mode(int flag) PERFTOOLS_NOTHROW {
  int old_mode = tc_new_mode;
  tc_new_mode = flag;
  return old_mode;
}

extern "C" PERFTOOLS_DLL_DECL int tc_query_new_mode() PERFTOOLS_NOTHROW {
  return tc_new_mode;
}

#ifndef TCMALLOC_USING_DEBUGALLOCATION  // debugallocation.cc defines its own

// CAVEAT: The code structure below ensures that MallocHook methods are always
//         called from the stack frame of the invoked allocation function.
//         heap-checker.cc depends on this to start a stack trace from
//         the call to the (de)allocation function.

namespace tcmalloc {


static ATTRIBUTE_SECTION(google_malloc)
void invoke_hooks_and_free(void *ptr) {
  MallocHook::InvokeDeleteHook(ptr);
  do_free(ptr);
}

ATTRIBUTE_SECTION(google_malloc)
void* cpp_throw_oom(size_t size) {
  return handle_oom(retry_malloc, reinterpret_cast<void *>(size),
                    true, false);
}

ATTRIBUTE_SECTION(google_malloc)
void* cpp_nothrow_oom(size_t size) {
  return handle_oom(retry_malloc, reinterpret_cast<void *>(size),
                    true, true);
}

ATTRIBUTE_SECTION(google_malloc)
void* malloc_oom(size_t size) {
  return handle_oom(retry_malloc, reinterpret_cast<void *>(size),
                    false, true);
}

// tcmalloc::allocate_full_XXX is called by fast-path malloc when some
// complex handling is needed (such as fetching object from central
// freelist or malloc sampling). It contains all 'operator new' logic,
// as opposed to malloc_fast_path which only deals with important
// subset of cases.
//
// Note that this is under tcmalloc namespace so that pprof
// can automatically filter it out of growthz/heapz profiles.
//
// We have slightly fancy setup because we need to call hooks from
// function in 'google_malloc' section and we cannot place template
// into this section. Thus 3 separate functions 'built' by macros.
//
// Also note that we're carefully orchestrating for
// MallocHook::GetCallerStackTrace to work even if compiler isn't
// optimizing tail calls (e.g. -O0 is given). We still require
// ATTRIBUTE_ALWAYS_INLINE to work for that case, but it was seen to
// work for -O0 -fno-inline across both GCC and clang. I.e. in this
// case we'll get stack frame for tc_new, followed by stack frame for
// allocate_full_cpp_throw_oom, followed by hooks machinery and user
// code's stack frames. So GetCallerStackTrace will find 2
// subsequent stack frames in google_malloc section and correctly
// 'cut' stack trace just before tc_new.
template <void* OOMHandler(size_t)>
ATTRIBUTE_ALWAYS_INLINE inline
static void* do_allocate_full(size_t size) {
  void* p = do_malloc(size);
  if (PREDICT_FALSE(p == NULL)) {
    p = OOMHandler(size);
  }
  MallocHook::InvokeNewHook(p, size);
  return CheckedMallocResult(p);
}

#define AF(oom) \
  ATTRIBUTE_SECTION(google_malloc)   \
  void* allocate_full_##oom(size_t size) {   \
    return do_allocate_full<oom>(size);     \
  }

AF(cpp_throw_oom)
AF(cpp_nothrow_oom)
AF(malloc_oom)

#undef AF

template <void* OOMHandler(size_t)>
static ATTRIBUTE_ALWAYS_INLINE inline void* dispatch_allocate_full(size_t size) {
  if (OOMHandler == cpp_throw_oom) {
    return allocate_full_cpp_throw_oom(size);
  }
  if (OOMHandler == cpp_nothrow_oom) {
    return allocate_full_cpp_nothrow_oom(size);
  }
  ASSERT(OOMHandler == malloc_oom);
  return allocate_full_malloc_oom(size);
}

struct retry_memalign_data {
  size_t align;
  size_t size;
};

static void *retry_do_memalign(void *arg) {
  retry_memalign_data *data = static_cast<retry_memalign_data *>(arg);
  return do_memalign_pages(data->align, data->size);
}

static ATTRIBUTE_SECTION(google_malloc)
void* memalign_pages(size_t align, size_t size,
                     bool from_operator, bool nothrow) {
  void *rv = do_memalign_pages(align, size);
  if (PREDICT_FALSE(rv == NULL)) {
    retry_memalign_data data;
    data.align = align;
    data.size = size;
    rv = handle_oom(retry_do_memalign, &data,
                    from_operator, nothrow);
  }
  MallocHook::InvokeNewHook(rv, size);
  return CheckedMallocResult(rv);
}

} // namespace tcmalloc

// This is quick, fast-path-only implementation of malloc/new. It is
// designed to only have support for fast-path. It checks if more
// complex handling is needed (such as a pageheap allocation or
// sampling) and only performs allocation if none of those uncommon
// conditions hold. When we have one of those odd cases it simply
// tail-calls to one of tcmalloc::allocate_full_XXX defined above.
//
// Such approach was found to be quite effective. Generated code for
// tc_{new,malloc} either succeeds quickly or tail-calls to
// allocate_full. Terseness of the source and lack of
// non-tail calls enables compiler to produce better code. Also
// produced code is short enough to enable effort-less human
// comprehension. Which itself led to elimination of various checks
// that were not necessary for fast-path.
template <void* OOMHandler(size_t)>
ATTRIBUTE_ALWAYS_INLINE inline
static void * malloc_fast_path(size_t size) {
  if (PREDICT_FALSE(!base::internal::new_hooks_.empty())) {
    return tcmalloc::dispatch_allocate_full<OOMHandler>(size);
  }

  ThreadCache *cache = ThreadCache::GetFastPathCache();

  if (PREDICT_FALSE(cache == NULL)) {
    return tcmalloc::dispatch_allocate_full<OOMHandler>(size);
  }

  uint32 cl;
  if (PREDICT_FALSE(!Static::sizemap()->GetSizeClass(size, &cl))) {
    return tcmalloc::dispatch_allocate_full<OOMHandler>(size);
  }

  size_t allocated_size = Static::sizemap()->ByteSizeForClass(cl);

  if (PREDICT_FALSE(!cache->TryRecordAllocationFast(allocated_size))) {
    return tcmalloc::dispatch_allocate_full<OOMHandler>(size);
  }

  return CheckedMallocResult(cache->Allocate(allocated_size, cl, OOMHandler));
}

template <void* OOMHandler(size_t)>
ATTRIBUTE_ALWAYS_INLINE inline
static void* memalign_fast_path(size_t align, size_t size) {
  if (PREDICT_FALSE(align > kPageSize)) {
    if (OOMHandler == tcmalloc::cpp_throw_oom) {
      return tcmalloc::memalign_pages(align, size, true, false);
    } else if (OOMHandler == tcmalloc::cpp_nothrow_oom) {
      return tcmalloc::memalign_pages(align, size, true, true);
    } else {
      ASSERT(OOMHandler == tcmalloc::malloc_oom);
      return tcmalloc::memalign_pages(align, size, false, true);
    }
  }

  // Everything with alignment <= kPageSize we can easily delegate to
  // regular malloc

  return malloc_fast_path<OOMHandler>(align_size_up(size, align));
}

extern "C" PERFTOOLS_DLL_DECL CACHELINE_ALIGNED_FN
void* tc_malloc(size_t size) PERFTOOLS_NOTHROW {
  return malloc_fast_path<tcmalloc::malloc_oom>(size);
}

static ATTRIBUTE_ALWAYS_INLINE inline
void free_fast_path(void *ptr) {
  if (PREDICT_FALSE(!base::internal::delete_hooks_.empty())) {
    tcmalloc::invoke_hooks_and_free(ptr);
    return;
  }
  do_free(ptr);
}

extern "C" PERFTOOLS_DLL_DECL CACHELINE_ALIGNED_FN
void tc_free(void* ptr) PERFTOOLS_NOTHROW {
  free_fast_path(ptr);
}

extern "C" PERFTOOLS_DLL_DECL CACHELINE_ALIGNED_FN
void tc_free_sized(void *ptr, size_t size) PERFTOOLS_NOTHROW {
  if (PREDICT_FALSE(!base::internal::delete_hooks_.empty())) {
    tcmalloc::invoke_hooks_and_free(ptr);
    return;
  }
#ifndef NO_TCMALLOC_SAMPLES
  // if ptr is kPageSize-aligned, then it could be sampled allocation,
  // thus we don't trust hint and just do plain free. It also handles
  // nullptr for us.
  if (PREDICT_FALSE((reinterpret_cast<uintptr_t>(ptr) & (kPageSize-1)) == 0)) {
    tc_free(ptr);
    return;
  }
#else
  if (!ptr) {
    return;
  }
#endif
  do_free_with_callback(ptr, &InvalidFree, true, size);
}

#ifdef TC_ALIAS

extern "C" PERFTOOLS_DLL_DECL void tc_delete_sized(void *p, size_t size) PERFTOOLS_NOTHROW
  TC_ALIAS(tc_free_sized);
extern "C" PERFTOOLS_DLL_DECL void tc_deletearray_sized(void *p, size_t size) PERFTOOLS_NOTHROW
  TC_ALIAS(tc_free_sized);

#else

extern "C" PERFTOOLS_DLL_DECL void tc_delete_sized(void *p, size_t size) PERFTOOLS_NOTHROW {
  tc_free_sized(p, size);
}
extern "C" PERFTOOLS_DLL_DECL void tc_deletearray_sized(void *p, size_t size) PERFTOOLS_NOTHROW {
  tc_free_sized(p, size);
}

#endif

extern "C" PERFTOOLS_DLL_DECL void* tc_calloc(size_t n,
                                              size_t elem_size) PERFTOOLS_NOTHROW {
  if (ThreadCache::IsUseEmergencyMalloc()) {
    return tcmalloc::EmergencyCalloc(n, elem_size);
  }
  void* result = do_calloc(n, elem_size);
  MallocHook::InvokeNewHook(result, n * elem_size);
  return result;
}

extern "C" PERFTOOLS_DLL_DECL void tc_cfree(void* ptr) PERFTOOLS_NOTHROW
#ifdef TC_ALIAS
TC_ALIAS(tc_free);
#else
{
  free_fast_path(ptr);
}
#endif

extern "C" PERFTOOLS_DLL_DECL void* tc_realloc(void* old_ptr,
                                               size_t new_size) PERFTOOLS_NOTHROW {
  if (old_ptr == NULL) {
    void* result = do_malloc_or_cpp_alloc(new_size);
    MallocHook::InvokeNewHook(result, new_size);
    return result;
  }
  if (new_size == 0) {
    MallocHook::InvokeDeleteHook(old_ptr);
    do_free(old_ptr);
    return NULL;
  }
  if (PREDICT_FALSE(tcmalloc::IsEmergencyPtr(old_ptr))) {
    return tcmalloc::EmergencyRealloc(old_ptr, new_size);
  }
  return do_realloc(old_ptr, new_size);
}

extern "C" PERFTOOLS_DLL_DECL CACHELINE_ALIGNED_FN
void* tc_new(size_t size) {
  return malloc_fast_path<tcmalloc::cpp_throw_oom>(size);
}

extern "C" PERFTOOLS_DLL_DECL CACHELINE_ALIGNED_FN
void* tc_new_nothrow(size_t size, const std::nothrow_t&) PERFTOOLS_NOTHROW {
  return malloc_fast_path<tcmalloc::cpp_nothrow_oom>(size);
}

extern "C" PERFTOOLS_DLL_DECL void tc_delete(void* p) PERFTOOLS_NOTHROW
#ifdef TC_ALIAS
TC_ALIAS(tc_free);
#else
{
  free_fast_path(p);
}
#endif

// Standard C++ library implementations define and use this
// (via ::operator delete(ptr, nothrow)).
// But it's really the same as normal delete, so we just do the same thing.
extern "C" PERFTOOLS_DLL_DECL void tc_delete_nothrow(void* p, const std::nothrow_t&) PERFTOOLS_NOTHROW
{
  if (PREDICT_FALSE(!base::internal::delete_hooks_.empty())) {
    tcmalloc::invoke_hooks_and_free(p);
    return;
  }
  do_free(p);
}

extern "C" PERFTOOLS_DLL_DECL void* tc_newarray(size_t size)
#ifdef TC_ALIAS
TC_ALIAS(tc_new);
#else
{
  return malloc_fast_path<tcmalloc::cpp_throw_oom>(size);
}
#endif

extern "C" PERFTOOLS_DLL_DECL void* tc_newarray_nothrow(size_t size, const std::nothrow_t&)
    PERFTOOLS_NOTHROW
#ifdef TC_ALIAS
TC_ALIAS(tc_new_nothrow);
#else
{
  return malloc_fast_path<tcmalloc::cpp_nothrow_oom>(size);
}
#endif

extern "C" PERFTOOLS_DLL_DECL void tc_deletearray(void* p) PERFTOOLS_NOTHROW
#ifdef TC_ALIAS
TC_ALIAS(tc_free);
#else
{
  free_fast_path(p);
}
#endif

extern "C" PERFTOOLS_DLL_DECL void tc_deletearray_nothrow(void* p, const std::nothrow_t&) PERFTOOLS_NOTHROW
#ifdef TC_ALIAS
TC_ALIAS(tc_delete_nothrow);
#else
{
  free_fast_path(p);
}
#endif

extern "C" PERFTOOLS_DLL_DECL CACHELINE_ALIGNED_FN
void* tc_memalign(size_t align, size_t size) PERFTOOLS_NOTHROW {
  return memalign_fast_path<tcmalloc::malloc_oom>(align, size);
}

extern "C" PERFTOOLS_DLL_DECL int tc_posix_memalign(
    void** result_ptr, size_t align, size_t size) PERFTOOLS_NOTHROW {
  if (((align % sizeof(void*)) != 0) ||
      ((align & (align - 1)) != 0) ||
      (align == 0)) {
    return EINVAL;
  }

  void* result = tc_memalign(align, size);
  if (PREDICT_FALSE(result == NULL)) {
    return ENOMEM;
  } else {
    *result_ptr = result;
    return 0;
  }
}

#if defined(ENABLE_ALIGNED_NEW_DELETE)

extern "C" PERFTOOLS_DLL_DECL void* tc_new_aligned(size_t size, std::align_val_t align) {
  return memalign_fast_path<tcmalloc::cpp_throw_oom>(static_cast<size_t>(align), size);
}

extern "C" PERFTOOLS_DLL_DECL void* tc_new_aligned_nothrow(size_t size, std::align_val_t align, const std::nothrow_t&) PERFTOOLS_NOTHROW {
  return memalign_fast_path<tcmalloc::cpp_nothrow_oom>(static_cast<size_t>(align), size);
}

extern "C" PERFTOOLS_DLL_DECL void tc_delete_aligned(void* p, std::align_val_t) PERFTOOLS_NOTHROW
{
  free_fast_path(p);
}

// There is no easy way to obtain the actual size used by do_memalign to allocate aligned storage, so for now
// just ignore the size. It might get useful in the future.
extern "C" PERFTOOLS_DLL_DECL void tc_delete_sized_aligned(void* p, size_t size, std::align_val_t align) PERFTOOLS_NOTHROW
{
  free_fast_path(p);
}

extern "C" PERFTOOLS_DLL_DECL void tc_delete_aligned_nothrow(void* p, std::align_val_t, const std::nothrow_t&) PERFTOOLS_NOTHROW
{
  free_fast_path(p);
}

extern "C" PERFTOOLS_DLL_DECL void* tc_newarray_aligned(size_t size, std::align_val_t align)
#ifdef TC_ALIAS
TC_ALIAS(tc_new_aligned);
#else
{
  return memalign_fast_path<tcmalloc::cpp_throw_oom>(static_cast<size_t>(align), size);
}
#endif

extern "C" PERFTOOLS_DLL_DECL void* tc_newarray_aligned_nothrow(size_t size, std::align_val_t align, const std::nothrow_t& nt) PERFTOOLS_NOTHROW
#ifdef TC_ALIAS
TC_ALIAS(tc_new_aligned_nothrow);
#else
{
  return memalign_fast_path<tcmalloc::cpp_nothrow_oom>(static_cast<size_t>(align), size);
}
#endif

extern "C" PERFTOOLS_DLL_DECL void tc_deletearray_aligned(void* p, std::align_val_t) PERFTOOLS_NOTHROW
#ifdef TC_ALIAS
TC_ALIAS(tc_delete_aligned);
#else
{
  free_fast_path(p);
}
#endif

// There is no easy way to obtain the actual size used by do_memalign to allocate aligned storage, so for now
// just ignore the size. It might get useful in the future.
extern "C" PERFTOOLS_DLL_DECL void tc_deletearray_sized_aligned(void* p, size_t size, std::align_val_t align) PERFTOOLS_NOTHROW
#ifdef TC_ALIAS
TC_ALIAS(tc_delete_sized_aligned);
#else
{
  free_fast_path(p);
}
#endif

extern "C" PERFTOOLS_DLL_DECL void tc_deletearray_aligned_nothrow(void* p, std::align_val_t, const std::nothrow_t&) PERFTOOLS_NOTHROW
#ifdef TC_ALIAS
TC_ALIAS(tc_delete_aligned_nothrow);
#else
{
  free_fast_path(p);
}
#endif

#endif // defined(ENABLE_ALIGNED_NEW_DELETE)

static size_t pagesize = 0;

extern "C" PERFTOOLS_DLL_DECL void* tc_valloc(size_t size) PERFTOOLS_NOTHROW {
  // Allocate page-aligned object of length >= size bytes
  if (pagesize == 0) pagesize = getpagesize();
  return tc_memalign(pagesize, size);
}

extern "C" PERFTOOLS_DLL_DECL void* tc_pvalloc(size_t size) PERFTOOLS_NOTHROW {
  // Round up size to a multiple of pagesize
  if (pagesize == 0) pagesize = getpagesize();
  if (size == 0) {     // pvalloc(0) should allocate one page, according to
    size = pagesize;   // http://man.free4web.biz/man3/libmpatrol.3.html
  }
  size = (size + pagesize - 1) & ~(pagesize - 1);
  return tc_memalign(pagesize, size);
}

extern "C" PERFTOOLS_DLL_DECL void tc_malloc_stats(void) PERFTOOLS_NOTHROW {
  do_malloc_stats();
}

extern "C" PERFTOOLS_DLL_DECL int tc_mallopt(int cmd, int value) PERFTOOLS_NOTHROW {
  return do_mallopt(cmd, value);
}

#ifdef HAVE_STRUCT_MALLINFO
extern "C" PERFTOOLS_DLL_DECL struct mallinfo tc_mallinfo(void) PERFTOOLS_NOTHROW {
  return do_mallinfo();
}
#endif

extern "C" PERFTOOLS_DLL_DECL size_t tc_malloc_size(void* ptr) PERFTOOLS_NOTHROW {
  return MallocExtension::instance()->GetAllocatedSize(ptr);
}

extern "C" PERFTOOLS_DLL_DECL void* tc_malloc_skip_new_handler(size_t size)  PERFTOOLS_NOTHROW {
  void* result = do_malloc(size);
  MallocHook::InvokeNewHook(result, size);
  return result;
}

#endif  // TCMALLOC_USING_DEBUGALLOCATION
