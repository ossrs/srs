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

#ifndef EMERGENCY_MALLOC_H
#define EMERGENCY_MALLOC_H
#include "config.h"

#include <stddef.h>

#include "base/basictypes.h"
#include "common.h"

namespace tcmalloc {
  static const uintptr_t kEmergencyArenaShift = 20+4; // 16 megs
  static const uintptr_t kEmergencyArenaSize = 1 << kEmergencyArenaShift;

  extern __attribute__ ((visibility("internal"))) char *emergency_arena_start;
  extern __attribute__ ((visibility("internal"))) uintptr_t emergency_arena_start_shifted;;

  PERFTOOLS_DLL_DECL void *EmergencyMalloc(size_t size);
  PERFTOOLS_DLL_DECL void EmergencyFree(void *p);
  PERFTOOLS_DLL_DECL void *EmergencyCalloc(size_t n, size_t elem_size);
  PERFTOOLS_DLL_DECL void *EmergencyRealloc(void *old_ptr, size_t new_size);

  static inline bool IsEmergencyPtr(const void *_ptr) {
    uintptr_t ptr = reinterpret_cast<uintptr_t>(_ptr);
    return PREDICT_FALSE((ptr >> kEmergencyArenaShift) == emergency_arena_start_shifted)
      && emergency_arena_start_shifted;
  }

} // namespace tcmalloc

#endif
