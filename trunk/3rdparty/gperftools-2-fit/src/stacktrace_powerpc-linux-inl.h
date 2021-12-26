// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright (c) 2007, Google Inc.
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
// Author: Craig Silverstein
//
// Produce stack trace.  ABI documentation reference can be found at:
// * PowerPC32 ABI: https://www.power.org/documentation/
// power-architecture-32-bit-abi-supplement-1-0-embeddedlinuxunified/
// * PowerPC64 ABI:
// http://www.linux-foundation.org/spec/ELF/ppc64/PPC-elf64abi-1.9.html#STACK

#ifndef BASE_STACKTRACE_POWERPC_INL_H_
#define BASE_STACKTRACE_POWERPC_INL_H_
// Note: this file is included into stacktrace.cc more than once.
// Anything that should only be defined once should be here:

#include <stdint.h>   // for uintptr_t
#include <stdlib.h>   // for NULL
#include <signal.h>  // for siginfo_t
#include <gperftools/stacktrace.h>
#include <base/vdso_support.h>

#if defined(HAVE_SYS_UCONTEXT_H)
#include <sys/ucontext.h>
#elif defined(HAVE_UCONTEXT_H)
#include <ucontext.h>  // for ucontext_t
#endif

// PowerPC64 Little Endian follows BE wrt. backchain, condition register,
// and LR save area, so no need to adjust the reading struct.
struct layout_ppc {
  struct layout_ppc *next;
#ifdef __PPC64__
  long condition_register;
#endif
  void *return_addr;
};

// Signal callbacks are handled by the vDSO symbol:
//
// * PowerPC64 Linux (arch/powerpc/kernel/vdso64/sigtramp.S):
//   __kernel_sigtramp_rt64
// * PowerPC32 Linux (arch/powerpc/kernel/vdso32/sigtramp.S):
//   __kernel_sigtramp32
//   __kernel_sigtramp_rt32
//
// So a backtrace may need to specially handling if the symbol readed is
// the signal trampoline.

// Given a pointer to a stack frame, locate and return the calling
// stackframe, or return NULL if no stackframe can be found. Perform sanity
// checks (the strictness of which is controlled by the boolean parameter
// "STRICT_UNWINDING") to reduce the chance that a bad pointer is returned.
template<bool STRICT_UNWINDING>
static layout_ppc *NextStackFrame(layout_ppc *current) {
  uintptr_t old_sp = (uintptr_t)(current);
  uintptr_t new_sp = (uintptr_t)(current->next);

  // Check that the transition from frame pointer old_sp to frame
  // pointer new_sp isn't clearly bogus
  if (STRICT_UNWINDING) {
    // With the stack growing downwards, older stack frame must be
    // at a greater address that the current one.
    if (new_sp <= old_sp)
      return NULL;
    // Assume stack frames larger than 100,000 bytes are bogus.
    if (new_sp - old_sp > 100000)
      return NULL;
  } else {
    // In the non-strict mode, allow discontiguous stack frames.
    // (alternate-signal-stacks for example).
    if (new_sp == old_sp)
      return NULL;
    // And allow frames upto about 1MB.
    if ((new_sp > old_sp) && (new_sp - old_sp > 1000000))
      return NULL;
  }
  if (new_sp & (sizeof(void *) - 1))
    return NULL;
  return current->next;
}

// This ensures that GetStackTrace stes up the Link Register properly.
void StacktracePowerPCDummyFunction() __attribute__((noinline));
void StacktracePowerPCDummyFunction() { __asm__ volatile(""); }
#endif  // BASE_STACKTRACE_POWERPC_INL_H_

// Note: this part of the file is included several times.
// Do not put globals below.

// Load instruction used on top-of-stack get.
#if defined(__PPC64__) || defined(__LP64__)
# define LOAD "ld"
#else
# define LOAD "lwz"
#endif

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
  layout_ppc *current;
  int n;

  // Get the address on top-of-stack
  current = reinterpret_cast<layout_ppc*> (__builtin_frame_address (0));
  // And ignore the current symbol
  current = current->next;

  StacktracePowerPCDummyFunction();

  n = 0;
  skip_count++; // skip parent's frame due to indirection in
                // stacktrace.cc

  base::VDSOSupport vdso;
  base::ElfMemImage::SymbolInfo rt_sigreturn_symbol_info;
#ifdef __PPC64__
  const void *sigtramp64_vdso = 0;
  if (vdso.LookupSymbol("__kernel_sigtramp_rt64", "LINUX_2.6.15", STT_NOTYPE,
                        &rt_sigreturn_symbol_info))
    sigtramp64_vdso = rt_sigreturn_symbol_info.address;
#else
  const void *sigtramp32_vdso = 0;
  if (vdso.LookupSymbol("__kernel_sigtramp32", "LINUX_2.6.15", STT_NOTYPE,
                        &rt_sigreturn_symbol_info))
    sigtramp32_vdso = rt_sigreturn_symbol_info.address;
  const void *sigtramp32_rt_vdso = 0;
  if (vdso.LookupSymbol("__kernel_sigtramp_rt32", "LINUX_2.6.15", STT_NOTYPE,
                        &rt_sigreturn_symbol_info))
    sigtramp32_rt_vdso = rt_sigreturn_symbol_info.address;
#endif

  while (current && n < max_depth) {

    // The GetStackFrames routine is called when we are in some
    // informational context (the failure signal handler for example).
    // Use the non-strict unwinding rules to produce a stack trace
    // that is as complete as possible (even if it contains a few
    // bogus entries in some rare cases).
    layout_ppc *next = NextStackFrame<!IS_STACK_FRAMES>(current);
    if (skip_count > 0) {
      skip_count--;
    } else {
      result[n] = current->return_addr;
#ifdef __PPC64__
      if (sigtramp64_vdso && (sigtramp64_vdso == current->return_addr)) {
        struct signal_frame_64 {
          char dummy[128];
          ucontext_t uc;
        // We don't care about the rest, since the IP value is at 'uc' field.
        } *sigframe = reinterpret_cast<signal_frame_64*>(current);
        result[n] = (void*) sigframe->uc.uc_mcontext.gp_regs[PT_NIP];
      }
#else
      if (sigtramp32_vdso && (sigtramp32_vdso == current->return_addr)) {
        struct signal_frame_32 {
          char dummy[64];
          struct sigcontext sctx;
          mcontext_t mctx;
          // We don't care about the rest, since IP value is at 'mctx' field.
        } *sigframe = reinterpret_cast<signal_frame_32*>(current);
        result[n] = (void*) sigframe->mctx.gregs[PT_NIP];
      } else if (sigtramp32_rt_vdso && (sigtramp32_rt_vdso == current->return_addr)) {
        struct rt_signal_frame_32 {
          char dummy[64 + 16];
          siginfo_t info;
          ucontext_t uc;
          // We don't care about the rest, since IP value is at 'uc' field.A
        } *sigframe = reinterpret_cast<rt_signal_frame_32*>(current);
        result[n] = (void*) sigframe->uc.uc_mcontext.uc_regs->gregs[PT_NIP];
      }
#endif

#if IS_STACK_FRAMES
      if (next > current) {
        sizes[n] = (uintptr_t)next - (uintptr_t)current;
      } else {
        // A frame-size of 0 is used to indicate unknown frame size.
        sizes[n] = 0;
      }
#endif
      n++;
    }
    current = next;
  }

  // It's possible the second-last stack frame can't return
  // (that is, it's __libc_start_main), in which case
  // the CRT startup code will have set its LR to 'NULL'.
  if (n > 0 && result[n-1] == NULL)
    n--;

  return n;
}
