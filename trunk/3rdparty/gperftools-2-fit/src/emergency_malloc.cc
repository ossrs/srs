// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright (c) 2014, gperftools Contributors
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
//

#include "config.h"

#include "emergency_malloc.h"

#include <errno.h>                      // for ENOMEM, errno
#include <string.h>                     // for memset

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/low_level_alloc.h"
#include "base/spinlock.h"
#include "internal_logging.h"


namespace tcmalloc {
  __attribute__ ((visibility("internal"))) char *emergency_arena_start;
  __attribute__ ((visibility("internal"))) uintptr_t emergency_arena_start_shifted;

  static CACHELINE_ALIGNED SpinLock emergency_malloc_lock(base::LINKER_INITIALIZED);
  static char *emergency_arena_end;
  static LowLevelAlloc::Arena *emergency_arena;

  class EmergencyArenaPagesAllocator : public LowLevelAlloc::PagesAllocator {
    ~EmergencyArenaPagesAllocator() {}
    void *MapPages(int32 flags, size_t size) {
      char *new_end = emergency_arena_end + size;
      if (new_end > emergency_arena_start + kEmergencyArenaSize) {
        RAW_LOG(FATAL, "Unable to allocate %zu bytes in emergency zone.", size);
      }
      char *rv = emergency_arena_end;
      emergency_arena_end = new_end;
      return static_cast<void *>(rv);
    }
    void UnMapPages(int32 flags, void *addr, size_t size) {
      RAW_LOG(FATAL, "UnMapPages is not implemented for emergency arena");
    }
  };

  static union {
    char bytes[sizeof(EmergencyArenaPagesAllocator)];
    void *ptr;
  } pages_allocator_place;

  static void InitEmergencyMalloc(void) {
    const int32 flags = LowLevelAlloc::kAsyncSignalSafe;

    void *arena = LowLevelAlloc::GetDefaultPagesAllocator()->MapPages(flags, kEmergencyArenaSize * 2);

    uintptr_t arena_ptr = reinterpret_cast<uintptr_t>(arena);
    uintptr_t ptr = (arena_ptr + kEmergencyArenaSize - 1) & ~(kEmergencyArenaSize-1);

    emergency_arena_end = emergency_arena_start = reinterpret_cast<char *>(ptr);
    EmergencyArenaPagesAllocator *allocator = new (pages_allocator_place.bytes) EmergencyArenaPagesAllocator();
    emergency_arena = LowLevelAlloc::NewArenaWithCustomAlloc(0, LowLevelAlloc::DefaultArena(), allocator);

    emergency_arena_start_shifted = reinterpret_cast<uintptr_t>(emergency_arena_start) >> kEmergencyArenaShift;

    uintptr_t head_unmap_size = ptr - arena_ptr;
    CHECK_CONDITION(head_unmap_size < kEmergencyArenaSize);
    if (head_unmap_size != 0) {
      LowLevelAlloc::GetDefaultPagesAllocator()->UnMapPages(flags, arena, ptr - arena_ptr);
    }

    uintptr_t tail_unmap_size = kEmergencyArenaSize - head_unmap_size;
    void *tail_start = reinterpret_cast<void *>(arena_ptr + head_unmap_size + kEmergencyArenaSize);
    LowLevelAlloc::GetDefaultPagesAllocator()->UnMapPages(flags, tail_start, tail_unmap_size);
  }

  PERFTOOLS_DLL_DECL void *EmergencyMalloc(size_t size) {
    SpinLockHolder l(&emergency_malloc_lock);

    if (emergency_arena_start == NULL) {
      InitEmergencyMalloc();
      CHECK_CONDITION(emergency_arena_start != NULL);
    }

    void *rv = LowLevelAlloc::AllocWithArena(size, emergency_arena);
    if (rv == NULL) {
      errno = ENOMEM;
    }
    return rv;
  }

  PERFTOOLS_DLL_DECL void EmergencyFree(void *p) {
    SpinLockHolder l(&emergency_malloc_lock);
    if (emergency_arena_start == NULL) {
      InitEmergencyMalloc();
      CHECK_CONDITION(emergency_arena_start != NULL);
      free(p);
      return;
    }
    CHECK_CONDITION(emergency_arena_start);
    LowLevelAlloc::Free(p);
  }

  PERFTOOLS_DLL_DECL void *EmergencyRealloc(void *_old_ptr, size_t new_size) {
    if (_old_ptr == NULL) {
      return EmergencyMalloc(new_size);
    }
    if (new_size == 0) {
      EmergencyFree(_old_ptr);
      return NULL;
    }
    SpinLockHolder l(&emergency_malloc_lock);
    CHECK_CONDITION(emergency_arena_start);

    char *old_ptr = static_cast<char *>(_old_ptr);
    CHECK_CONDITION(old_ptr <= emergency_arena_end);
    CHECK_CONDITION(emergency_arena_start <= old_ptr);

    // NOTE: we don't know previous size of old_ptr chunk. So instead
    // of trying to figure out right size of copied memory, we just
    // copy largest possible size. We don't care about being slow.
    size_t old_ptr_size = emergency_arena_end - old_ptr;
    size_t copy_size = (new_size < old_ptr_size) ? new_size : old_ptr_size;

    void *new_ptr = LowLevelAlloc::AllocWithArena(new_size, emergency_arena);
    if (new_ptr == NULL) {
      errno = ENOMEM;
      return NULL;
    }
    memcpy(new_ptr, old_ptr, copy_size);

    LowLevelAlloc::Free(old_ptr);
    return new_ptr;
  }

  PERFTOOLS_DLL_DECL void *EmergencyCalloc(size_t n, size_t elem_size) {
    // Overflow check
    const size_t size = n * elem_size;
    if (elem_size != 0 && size / elem_size != n) return NULL;
    void *rv = EmergencyMalloc(size);
    if (rv != NULL) {
      memset(rv, 0, size);
    }
    return rv;
  }
};
