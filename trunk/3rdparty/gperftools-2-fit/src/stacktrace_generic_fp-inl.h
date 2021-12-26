// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright (c) 2021, gperftools Contributors
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

// This file contains "generic" stack frame pointer backtracing
// code. Attempt is made to minimize amount of arch- or os-specific
// code and keep everything as generic as possible. Currently
// supported are x86-64, aarch64 and riscv.
#ifndef BASE_STACKTRACE_GENERIC_FP_INL_H_
#define BASE_STACKTRACE_GENERIC_FP_INL_H_

#if defined(HAVE_SYS_UCONTEXT_H)
#include <sys/ucontext.h>
#elif defined(HAVE_UCONTEXT_H)
#include <ucontext.h>
#endif

// This is only used on OS-es with mmap support.
#include <sys/mman.h>

// Set this to true to disable "probing" of addresses that are read to
// make backtracing less-safe, but faster.
#ifndef TCMALLOC_UNSAFE_GENERIC_FP_STACKTRACE
#define TCMALLOC_UNSAFE_GENERIC_FP_STACKTRACE 0
#endif

namespace {
namespace stacktrace_generic_fp {

struct frame {
  uintptr_t parent;
  void* pc;
};

frame* adjust_fp(frame* f) {
#ifdef __riscv
  return f - 1;
#else
  return f;
#endif
}

static bool CheckPageIsReadable(void* ptr, void* checked_ptr) {
  static uintptr_t pagesize;
  if (pagesize == 0) {
    pagesize = getpagesize();
  }

  uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t parent_frame = reinterpret_cast<uintptr_t>(checked_ptr);

  parent_frame &= ~(pagesize - 1);
  addr &= ~(pagesize - 1);

  if (parent_frame != 0 && addr == parent_frame) {
    return true;
  }

  return (msync(reinterpret_cast<void*>(addr), pagesize, MS_ASYNC) == 0);
}

ATTRIBUTE_NOINLINE // forces architectures with link register to save it
int capture(void **result, int max_depth, int skip_count,
            void* initial_frame, void* const * initial_pc) {
  int i = 0;

  if (initial_pc != nullptr) {
    // This is 'with ucontext' case. We take first pc from ucontext
    // and then skip_count is ignored as we assume that caller only
    // needed stack trace up to signal handler frame.
    skip_count = 0;
    if (max_depth == 0) {
      return 0;
    }
    result[0] = *initial_pc;
    i++;
  }

  constexpr uintptr_t kTooSmallAddr = 16 << 10;
  constexpr uintptr_t kFrameSizeThreshold = 128 << 10;

  // This is simplistic yet. Here we're targeting x86-64, aarch64 and
  // riscv. All have 16 bytes stack alignment (even 32 bit
  // riscv). This can be made more elaborate as we consider more
  // architectures. Note, it allows us to only readability of check
  // f->parent address.
  constexpr uintptr_t kAlignment = 16;

  uintptr_t initial_frame_addr = reinterpret_cast<uintptr_t>(initial_frame);
  if ((initial_frame_addr & (kAlignment - 1)) != 0) {
    return i;
  }
  if (initial_frame_addr < kTooSmallAddr) {
    return i;
  }

  frame* prev_f = nullptr;
  frame *f = adjust_fp(reinterpret_cast<frame*>(initial_frame));

  while (i < max_depth) {
    if (!TCMALLOC_UNSAFE_GENERIC_FP_STACKTRACE
        && !CheckPageIsReadable(&f->parent, prev_f)) {
      break;
    }

    void* pc = f->pc;
    if (pc == nullptr) {
      break;
    }

    if (i >= skip_count) {
      result[i - skip_count] = pc;
    }

    i++;

    uintptr_t parent_frame_addr = f->parent;
    uintptr_t child_frame_addr = reinterpret_cast<uintptr_t>(f);

    if (parent_frame_addr < kTooSmallAddr) {
      break;
    }
    // stack grows towards smaller addresses, so if we didn't see
    // frame address increased (going from child to parent), it is bad
    // frame. We also test if frame is too big since that is another
    // sign of bad stack frame.
    if (parent_frame_addr - child_frame_addr > kFrameSizeThreshold) {
      break;
    }

    if ((parent_frame_addr & (kAlignment - 1)) != 0) {
      // not aligned, so we keep it safe and assume frame is bogus
      break;
    }

    prev_f = f;

    f = adjust_fp(reinterpret_cast<frame*>(parent_frame_addr));
  }
  return i;
}

}  // namespace stacktrace_generic_fp
}  // namespace

#endif  // BASE_STACKTRACE_GENERIC_FP_INL_H_

// Note: this part of the file is included several times.
// Do not put globals below.

// The following 4 functions are generated from the code below:
//   GetStack{Trace,Frames}()
//   GetStack{Trace,Frames}WithContext()
//
// These functions take the following args:
//   void** result: the stack-trace, as an array
//   int* sizes: the size of each stack frame, as an array
//               (GetStackFrames* only)
//   int max_depth: the size of the result (and sizes) array(s)
//   int skip_count: how many stack pointers to skip before storing in result
//   void* ucp: a ucontext_t* (GetStack{Trace,Frames}WithContext only)

static int GET_STACK_TRACE_OR_FRAMES {
#if IS_STACK_FRAMES
  memset(sizes, 0, sizeof(*sizes) * max_depth);
#endif

  // one for this function
  skip_count += 1;

  void* const * initial_pc = nullptr;
  void* initial_frame = __builtin_frame_address(0);

#if IS_WITH_CONTEXT
  if (ucp) {
    auto uc = static_cast<const ucontext_t*>(ucp);
#ifdef __riscv
    initial_pc = reinterpret_cast<void* const *>(&uc->uc_mcontext.__gregs[REG_PC]);
    initial_frame = reinterpret_cast<void*>(uc->uc_mcontext.__gregs[REG_S0]);
#elif __aarch64__
    initial_pc = reinterpret_cast<void* const *>(&uc->uc_mcontext.pc);
    initial_frame = reinterpret_cast<void*>(uc->uc_mcontext.regs[29]);
#else
    initial_pc = reinterpret_cast<void* const *>(&uc->uc_mcontext.gregs[REG_RIP]);
    initial_frame = reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_RBP]);
#endif
  }
#endif  // IS_WITH_CONTEXT

  int n = stacktrace_generic_fp::capture(result, max_depth, skip_count,
                                         initial_frame, initial_pc);

  // make sure we don't tail-call capture
  (void)*(const_cast<void * volatile *>(result));
  return n;
}
