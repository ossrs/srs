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
// Produce stack trace.  I'm guessing (hoping!) the code is much like
// for x86.  For apple machines, at least, it seems to be; see
//    http://developer.apple.com/documentation/mac/runtimehtml/RTArch-59.html
//    http://www.linux-foundation.org/spec/ELF/ppc64/PPC-elf64abi-1.9.html#STACK
// Linux has similar code: http://patchwork.ozlabs.org/linuxppc/patch?id=8882

#ifndef BASE_STACKTRACE_POWERPC_INL_H_
#define BASE_STACKTRACE_POWERPC_INL_H_
// Note: this file is included into stacktrace.cc more than once.
// Anything that should only be defined once should be here:

#include <stdint.h>   // for uintptr_t
#include <stdlib.h>   // for NULL
#include <gperftools/stacktrace.h>

struct layout_ppc {
  struct layout_ppc *next;
#if defined(__APPLE__) || (defined(__linux) && defined(__PPC64__))
  long condition_register;
#endif
  void *return_addr;
};

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

#if defined(__linux__) && defined(__PPC__)
# define TOP_STACK "%0,0(1)"
#elif defined(__MACH__) && defined(__APPLE__)
// Apple OS X uses an old version of gnu as -- both Darwin 7.9.0 (Panther)
// and Darwin 8.8.1 (Tiger) use as 1.38.  This means we have to use a
// different asm syntax.  I don't know quite the best way to discriminate
// systems using the old as from the new one; I've gone with __APPLE__.
// TODO(csilvers): use autoconf instead, to look for 'as --version' == 1 or 2
# define TOP_STACK "%0,0(r1)"
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

  // Force GCC to spill LR.
  asm volatile ("" : "=l"(current));

  // Get the address on top-of-stack
  asm volatile (LOAD " " TOP_STACK : "=r"(current));

  StacktracePowerPCDummyFunction();

  n = 0;
  skip_count++; // skip parent's frame due to indirection in
                // stacktrace.cc
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
