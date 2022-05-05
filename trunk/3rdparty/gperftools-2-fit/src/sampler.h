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
// All Rights Reserved.
//
// Author: Daniel Ford

#ifndef TCMALLOC_SAMPLER_H_
#define TCMALLOC_SAMPLER_H_

#include "config.h"
#include <stddef.h>                     // for size_t
#ifdef HAVE_STDINT_H
#include <stdint.h>                     // for uint64_t, uint32_t, int32_t
#endif
#include <string.h>                     // for memcpy
#include "base/basictypes.h"  // for ASSERT
#include "internal_logging.h"  // for ASSERT
#include "static_vars.h"

namespace tcmalloc {

//-------------------------------------------------------------------
// Sampler to decide when to create a sample trace for an allocation
// Not thread safe: Each thread should have it's own sampler object.
// Caller must use external synchronization if used
// from multiple threads.
//
// With 512K average sample step (the default):
//  the probability of sampling a 4K allocation is about 0.00778
//  the probability of sampling a 1MB allocation is about 0.865
//  the probability of sampling a 1GB allocation is about 1.00000
// In general, the probablity of sampling is an allocation of size X
// given a flag value of Y (default 1M) is:
//  1 - e^(-X/Y)
//
// With 128K average sample step:
//  the probability of sampling a 1MB allocation is about 0.99966
//  the probability of sampling a 1GB allocation is about 1.0
//  (about 1 - 2**(-26))
// With 1M average sample step:
//  the probability of sampling a 4K allocation is about 0.00390
//  the probability of sampling a 1MB allocation is about 0.632
//  the probability of sampling a 1GB allocation is about 1.0
//
// The sampler works by representing memory as a long stream from
// which allocations are taken. Some of the bytes in this stream are
// marked and if an allocation includes a marked byte then it is
// sampled. Bytes are marked according to a Poisson point process
// with each byte being marked independently with probability
// p = 1/tcmalloc_sample_parameter.  This makes the probability
// of sampling an allocation of X bytes equal to the CDF of
// a geometric with mean tcmalloc_sample_parameter. (ie. the
// probability that at least one byte in the range is marked). This
// is accurately given by the CDF of the corresponding exponential
// distribution : 1 - e^(-X/tcmalloc_sample_parameter_)
// Independence of the byte marking ensures independence of
// the sampling of each allocation.
//
// This scheme is implemented by noting that, starting from any
// fixed place, the number of bytes until the next marked byte
// is geometrically distributed. This number is recorded as
// bytes_until_sample_.  Every allocation subtracts from this
// number until it is less than 0. When this happens the current
// allocation is sampled.
//
// When an allocation occurs, bytes_until_sample_ is reset to
// a new independtly sampled geometric number of bytes. The
// memoryless property of the point process means that this may
// be taken as the number of bytes after the end of the current
// allocation until the next marked byte. This ensures that
// very large allocations which would intersect many marked bytes
// only result in a single call to PickNextSamplingPoint.
//-------------------------------------------------------------------

class SamplerTest;

class PERFTOOLS_DLL_DECL Sampler {
 public:
  constexpr Sampler() {}

  // Initialize this sampler.
  void Init(uint64_t seed);

  // Record allocation of "k" bytes.  Return true if no further work
  // is need, and false if allocation needed to be sampled.
  bool RecordAllocation(size_t k);

  // Same as above (but faster), except:
  // a) REQUIRES(k < std::numeric_limits<ssize_t>::max())
  // b) if this returns false, you must call RecordAllocation
  //    to confirm if sampling truly needed.
  //
  // The point of this function is to only deal with common case of no
  // sampling and let caller (which is in malloc fast-path) to
  // "escalate" to fuller and slower logic only if necessary.
  bool TryRecordAllocationFast(size_t k);

  // Generate a geometric with mean 512K (or FLAG_tcmalloc_sample_parameter)
  ssize_t PickNextSamplingPoint();

  // Returns the current sample period
  static int GetSamplePeriod();

  // The following are public for the purposes of testing
  static uint64_t NextRandom(uint64_t rnd_);  // Returns the next prng value

  // C++03 requires that types stored in TLS be POD.  As a result, you must
  // initialize these members to {0, 0, false} before using this class!
  //
  // TODO(ahh): C++11 support will let us make these private.

  // Bytes until we sample next.
  //
  // More specifically when bytes_until_sample_ is X, we can allocate
  // X bytes without triggering sampling; on the (X+1)th allocated
  // byte, the containing allocation will be sampled.
  //
  // Always non-negative with only very brief exceptions (see
  // DecrementFast{,Finish}, so casting to size_t is ok.
 private:
  friend class SamplerTest;
  bool RecordAllocationSlow(size_t k);

  ssize_t bytes_until_sample_{};
  uint64_t rnd_{};  // Cheap random number generator
  bool initialized_{};
};

inline bool Sampler::RecordAllocation(size_t k) {
  // The first time we enter this function we expect bytes_until_sample_
  // to be zero, and we must call SampleAllocationSlow() to ensure
  // proper initialization of static vars.
  ASSERT(Static::IsInited() || bytes_until_sample_ == 0);

  // Note that we have to deal with arbitrarily large values of k
  // here. Thus we're upcasting bytes_until_sample_ to unsigned rather
  // than the other way around. And this is why this code cannot be
  // merged with DecrementFast code below.
  if (static_cast<size_t>(bytes_until_sample_) < k) {
    bool result = RecordAllocationSlow(k);
    ASSERT(Static::IsInited());
    return result;
  } else {
    bytes_until_sample_ -= k;
    ASSERT(Static::IsInited());
    return true;
  }
}

inline bool Sampler::TryRecordAllocationFast(size_t k) {
  // For efficiency reason, we're testing bytes_until_sample_ after
  // decrementing it by k. This allows compiler to do sub <reg>, <mem>
  // followed by conditional jump on sign. But it is correct only if k
  // is actually smaller than largest ssize_t value. Otherwise
  // converting k to signed value overflows.
  //
  // It would be great for generated code to be sub <reg>, <mem>
  // followed by conditional jump on 'carry', which would work for
  // arbitrary values of k, but there seem to be no way to express
  // that in C++.
  //
  // Our API contract explicitly states that only small values of k
  // are permitted. And thus it makes sense to assert on that.
  ASSERT(static_cast<ssize_t>(k) >= 0);

  bytes_until_sample_ -= static_cast<ssize_t>(k);
  if (PREDICT_FALSE(bytes_until_sample_ < 0)) {
    // Note, we undo sampling counter update, since we're not actually
    // handling slow path in the "needs sampling" case (calling
    // RecordAllocationSlow to reset counter). And we do that in order
    // to avoid non-tail calls in malloc fast-path. See also comments
    // on declaration inside Sampler class.
    //
    // volatile is used here to improve compiler's choice of
    // instuctions. We know that this path is very rare and that there
    // is no need to keep previous value of bytes_until_sample_ in
    // register. This helps compiler generate slightly more efficient
    // sub <reg>, <mem> instruction for subtraction above.
    volatile ssize_t *ptr =
        const_cast<volatile ssize_t *>(&bytes_until_sample_);
    *ptr += k;
    return false;
  }
  return true;
}

// Inline functions which are public for testing purposes

// Returns the next prng value.
// pRNG is: aX+b mod c with a = 0x5DEECE66D, b =  0xB, c = 1<<48
// This is the lrand64 generator.
inline uint64_t Sampler::NextRandom(uint64_t rnd) {
  const uint64_t prng_mult = 0x5DEECE66DULL;
  const uint64_t prng_add = 0xB;
  const uint64_t prng_mod_power = 48;
  const uint64_t prng_mod_mask =
      ~((~static_cast<uint64_t>(0)) << prng_mod_power);
  return (prng_mult * rnd + prng_add) & prng_mod_mask;
}

}  // namespace tcmalloc

#endif  // TCMALLOC_SAMPLER_H_
