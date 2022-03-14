// -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil -*-
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

#include "run_benchmark.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

struct internal_bench {
  bench_body body;
  uintptr_t param;
};

static void run_body(struct internal_bench *b, long iterations)
{
  b->body(iterations, b->param);
}

static double measure_once(struct internal_bench *b, long iterations)
{
  struct timeval tv_before, tv_after;
  int rv;
  double time;

  rv = gettimeofday(&tv_before, NULL);
  if (rv) {
    perror("gettimeofday");
    abort();
  }

  run_body(b, iterations);

  rv = gettimeofday(&tv_after, NULL);
  if (rv) {
    perror("gettimeofday");
    abort();
  }
  tv_after.tv_sec -= tv_before.tv_sec;
  time = tv_after.tv_sec * 1E6 + tv_after.tv_usec;
  time -= tv_before.tv_usec;
  time *= 1000;
  return time;
}

#define TRIAL_NSEC 0.3E9
#define TARGET_NSEC 3E9

static double run_benchmark(struct internal_bench *b)
{
  long iterations = 128;
  double nsec;
  while (1) {
    nsec = measure_once(b, iterations);
    if (nsec > TRIAL_NSEC) {
      break;
    }
    iterations <<= 1;
  }
  while (nsec < TARGET_NSEC) {
    iterations = (long)(iterations * TARGET_NSEC * 1.1 / nsec);
    nsec = measure_once(b, iterations);
  }
  return nsec / iterations;
}

void report_benchmark(const char *name, bench_body body, uintptr_t param)
{
  int i;
  struct internal_bench b = {.body = body, .param = param};
  for (i = 0; i < 3; i++) {
    double nsec = run_benchmark(&b);
    int slen;
    int padding_size;

    slen = printf("Benchmark: %s", name);
    if (param && name[strlen(name)-1] != ')') {
      slen += printf("(%lld)", (long long)param);
    }
    padding_size = 60 - slen;
    if (padding_size < 1) {
      padding_size = 1;
    }
    printf("%*c%f nsec\n", padding_size, ' ', nsec);
    fflush(stdout);
  }
}
