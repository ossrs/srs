// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <algorithm>

#include "run_benchmark.h"

static void bench_fastpath_throughput(long iterations,
                                      uintptr_t param)
{
  size_t sz = 32;
  for (; iterations>0; iterations--) {
    void *p = malloc(sz);
    if (!p) {
      abort();
    }
    free(p);
    // this makes next iteration use different free list. So
    // subsequent iterations may actually overlap in time.
    sz = ((sz * 8191) & 511) + 16;
  }
}

static void bench_fastpath_dependent(long iterations,
                                     uintptr_t param)
{
  size_t sz = 32;
  for (; iterations>0; iterations--) {
    void *p = malloc(sz);
    if (!p) {
      abort();
    }
    free(p);
    // this makes next iteration depend on current iteration. But this
    // iteration's free may still overlap with next iteration's malloc
    sz = ((sz | reinterpret_cast<size_t>(p)) & 511) + 16;
  }
}

static void bench_fastpath_simple(long iterations,
                                  uintptr_t param)
{
  size_t sz = static_cast<size_t>(param);
  for (; iterations>0; iterations--) {
    void *p = malloc(sz);
    if (!p) {
      abort();
    }
    free(p);
    // next iteration will use same free list as this iteration. So it
    // should be prevent next iterations malloc to go too far before
    // free done. But using same size will make free "too fast" since
    // we'll hit size class cache.
  }
}

#ifdef __GNUC__
#define HAVE_SIZED_FREE_OPTION

extern "C" void tc_free_sized(void *ptr, size_t size) __attribute__((weak));
extern "C" void *tc_memalign(size_t align, size_t size) __attribute__((weak));

static bool is_sized_free_available(void)
{
  return tc_free_sized != NULL;
}

static bool is_memalign_available(void)
{
  return tc_memalign != NULL;
}

static void bench_fastpath_simple_sized(long iterations,
                                        uintptr_t param)
{
  size_t sz = static_cast<size_t>(param);
  for (; iterations>0; iterations--) {
    void *p = malloc(sz);
    if (!p) {
      abort();
    }
    tc_free_sized(p, sz);
    // next iteration will use same free list as this iteration. So it
    // should be prevent next iterations malloc to go too far before
    // free done. But using same size will make free "too fast" since
    // we'll hit size class cache.
  }
}

static void bench_fastpath_memalign(long iterations,
                                    uintptr_t param)
{
  size_t sz = static_cast<size_t>(param);
  for (; iterations>0; iterations--) {
    void *p = tc_memalign(32, sz);
    if (!p) {
      abort();
    }
    free(p);
    // next iteration will use same free list as this iteration. So it
    // should be prevent next iterations malloc to go too far before
    // free done. But using same size will make free "too fast" since
    // we'll hit size class cache.
  }
}

#endif // __GNUC__

#define STACKSZ (1 << 16)

static void bench_fastpath_stack(long iterations,
                                 uintptr_t _param)
{

  void *stack[STACKSZ];
  size_t sz = 64;
  long param = static_cast<long>(_param);
  param &= STACKSZ - 1;
  param = param ? param : 1;
  for (; iterations>0; iterations -= param) {
    for (long k = param-1; k >= 0; k--) {
      void *p = malloc(sz);
      if (!p) {
        abort();
      }
      stack[k] = p;
      // this makes next iteration depend on result of this iteration
      sz = ((sz | reinterpret_cast<size_t>(p)) & 511) + 16;
    }
    for (long k = 0; k < param; k++) {
      free(stack[k]);
    }
  }
}

static void bench_fastpath_stack_simple(long iterations,
                                        uintptr_t _param)
{

  void *stack[STACKSZ];
  size_t sz = 128;
  long param = static_cast<long>(_param);
  param &= STACKSZ - 1;
  param = param ? param : 1;
  for (; iterations>0; iterations -= param) {
    for (long k = param-1; k >= 0; k--) {
      void *p = malloc(sz);
      if (!p) {
        abort();
      }
      stack[k] = p;
    }
    for (long k = 0; k < param; k++) {
      free(stack[k]);
    }
  }
}

static void bench_fastpath_rnd_dependent(long iterations,
                                         uintptr_t _param)
{
  static const uintptr_t rnd_c = 1013904223;
  static const uintptr_t rnd_a = 1664525;

  void *ptrs[STACKSZ];
  size_t sz = 128;
  if ((_param & (_param - 1))) {
    abort();
  }
  if (_param > STACKSZ) {
    abort();
  }
  int param = static_cast<int>(_param);

  for (; iterations>0; iterations -= param) {
    for (int k = param-1; k >= 0; k--) {
      void *p = malloc(sz);
      if (!p) {
        abort();
      }
      ptrs[k] = p;
      sz = ((sz | reinterpret_cast<size_t>(p)) & 511) + 16;
    }

    // this will iterate through all objects in order that is
    // unpredictable to processor's prefetchers
    uint32_t rnd = 0;
    uint32_t free_idx = 0;
    do {
      free(ptrs[free_idx]);
      rnd = rnd * rnd_a + rnd_c;
      free_idx = rnd & (param - 1);
    } while (free_idx != 0);
  }
}

static void *randomize_buffer[13<<20];


void randomize_one_size_class(size_t size) {
  int count = (100<<20) / size;
  if (count * sizeof(randomize_buffer[0]) > sizeof(randomize_buffer)) {
    abort();
  }
  for (int i = 0; i < count; i++) {
    randomize_buffer[i] = malloc(size);
  }
  std::random_shuffle(randomize_buffer, randomize_buffer + count);
  for (int i = 0; i < count; i++) {
    free(randomize_buffer[i]);
  }
}

void randomize_size_classes() {
  randomize_one_size_class(8);
  int i;
  for (i = 16; i < 256; i += 16) {
    randomize_one_size_class(i);
  }
  for (; i < 512; i += 32) {
    randomize_one_size_class(i);
  }
  for (; i < 1024; i += 64) {
    randomize_one_size_class(i);
  }
  for (; i < (4 << 10); i += 128) {
    randomize_one_size_class(i);
  }
  for (; i < (32 << 10); i += 1024) {
    randomize_one_size_class(i);
  }
}

int main(void)
{
  randomize_size_classes();

  report_benchmark("bench_fastpath_throughput", bench_fastpath_throughput, 0);
  report_benchmark("bench_fastpath_dependent", bench_fastpath_dependent, 0);
  report_benchmark("bench_fastpath_simple", bench_fastpath_simple, 64);
  report_benchmark("bench_fastpath_simple", bench_fastpath_simple, 2048);
  report_benchmark("bench_fastpath_simple", bench_fastpath_simple, 16384);

#ifdef HAVE_SIZED_FREE_OPTION
  if (is_sized_free_available()) {
    report_benchmark("bench_fastpath_simple_sized", bench_fastpath_simple_sized, 64);
    report_benchmark("bench_fastpath_simple_sized", bench_fastpath_simple_sized, 2048);
  }

  if (is_memalign_available()) {
    report_benchmark("bench_fastpath_memalign", bench_fastpath_memalign, 64);
    report_benchmark("bench_fastpath_memalign", bench_fastpath_memalign, 2048);
  }

#endif

  for (int i = 8; i <= 512; i <<= 1) {
    report_benchmark("bench_fastpath_stack", bench_fastpath_stack, i);
  }
  report_benchmark("bench_fastpath_stack_simple", bench_fastpath_stack_simple, 32);
  report_benchmark("bench_fastpath_stack_simple", bench_fastpath_stack_simple, 8192);
  report_benchmark("bench_fastpath_rnd_dependent", bench_fastpath_rnd_dependent, 32);
  report_benchmark("bench_fastpath_rnd_dependent", bench_fastpath_rnd_dependent, 8192);
  return 0;
}
