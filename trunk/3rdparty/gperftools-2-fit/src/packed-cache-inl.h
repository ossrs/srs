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
// Author: Geoff Pike
//
// This file provides a minimal cache that can hold a <key, value> pair
// with little if any wasted space.  The types of the key and value
// must be unsigned integral types or at least have unsigned semantics
// for >>, casting, and similar operations.
//
// Synchronization is not provided.  However, the cache is implemented
// as an array of cache entries whose type is chosen at compile time.
// If a[i] is atomic on your hardware for the chosen array type then
// raciness will not necessarily lead to bugginess.  The cache entries
// must be large enough to hold a partial key and a value packed
// together.  The partial keys are bit strings of length
// kKeybits - kHashbits, and the values are bit strings of length kValuebits.
//
// In an effort to use minimal space, every cache entry represents
// some <key, value> pair; the class provides no way to mark a cache
// entry as empty or uninitialized.  In practice, you may want to have
// reserved keys or values to get around this limitation.  For example, in
// tcmalloc's PageID-to-sizeclass cache, a value of 0 is used as
// "unknown sizeclass."
//
// Usage Considerations
// --------------------
//
// kHashbits controls the size of the cache.  The best value for
// kHashbits will of course depend on the application.  Perhaps try
// tuning the value of kHashbits by measuring different values on your
// favorite benchmark.  Also remember not to be a pig; other
// programs that need resources may suffer if you are.
//
// The main uses for this class will be when performance is
// critical and there's a convenient type to hold the cache's
// entries.  As described above, the number of bits required
// for a cache entry is (kKeybits - kHashbits) + kValuebits.  Suppose
// kKeybits + kValuebits is 43.  Then it probably makes sense to
// chose kHashbits >= 11 so that cache entries fit in a uint32.
//
// On the other hand, suppose kKeybits = kValuebits = 64.  Then
// using this class may be less worthwhile.  You'll probably
// be using 128 bits for each entry anyway, so maybe just pick
// a hash function, H, and use an array indexed by H(key):
//    void Put(K key, V value) { a_[H(key)] = pair<K, V>(key, value); }
//    V GetOrDefault(K key, V default) { const pair<K, V> &p = a_[H(key)]; ... }
//    etc.
//
// Further Details
// ---------------
//
// For caches used only by one thread, the following is true:
// 1. For a cache c,
//      (c.Put(key, value), c.GetOrDefault(key, 0)) == value
//    and
//      (c.Put(key, value), <...>, c.GetOrDefault(key, 0)) == value
//    if the elided code contains no c.Put calls.
//
// 2. Has(key) will return false if no <key, value> pair with that key
//    has ever been Put.  However, a newly initialized cache will have
//    some <key, value> pairs already present.  When you create a new
//    cache, you must specify an "initial value."  The initialization
//    procedure is equivalent to Clear(initial_value), which is
//    equivalent to Put(k, initial_value) for all keys k from 0 to
//    2^kHashbits - 1.
//
// 3. If key and key' differ then the only way Put(key, value) may
//    cause Has(key') to change is that Has(key') may change from true to
//    false. Furthermore, a Put() call that doesn't change Has(key')
//    doesn't change GetOrDefault(key', ...) either.
//
// Implementation details:
//
// This is a direct-mapped cache with 2^kHashbits entries; the hash
// function simply takes the low bits of the key.  We store whole keys
// if a whole key plus a whole value fits in an entry.  Otherwise, an
// entry is the high bits of a key and a value, packed together.
// E.g., a 20 bit key and a 7 bit value only require a uint16 for each
// entry if kHashbits >= 11.
//
// Alternatives to this scheme will be added as needed.

#ifndef TCMALLOC_PACKED_CACHE_INL_H_
#define TCMALLOC_PACKED_CACHE_INL_H_

#include "config.h"
#include <stddef.h>                     // for size_t
#ifdef HAVE_STDINT_H
#include <stdint.h>                     // for uintptr_t
#endif
#include "base/basictypes.h"
#include "common.h"
#include "internal_logging.h"

// A safe way of doing "(1 << n) - 1" -- without worrying about overflow
// Note this will all be resolved to a constant expression at compile-time
#define N_ONES_(IntType, N)                                     \
  ( (N) == 0 ? 0 : ((static_cast<IntType>(1) << ((N)-1))-1 +    \
                    (static_cast<IntType>(1) << ((N)-1))) )

// The types K and V provide upper bounds on the number of valid keys
// and values, but we explicitly require the keys to be less than
// 2^kKeybits and the values to be less than 2^kValuebits.  The size
// of the table is controlled by kHashbits, and the type of each entry
// in the cache is uintptr_t (native machine word).  See also the big
// comment at the top of the file.
template <int kKeybits>
class PackedCache {
 public:
  typedef uintptr_t T;
  typedef uintptr_t K;
  typedef uint32 V;
#ifdef TCMALLOC_SMALL_BUT_SLOW
  // Decrease the size map cache if running in the small memory mode.
  static const int kHashbits = 12;
#else
  static const int kHashbits = 16;
#endif
  static const int kValuebits = 7;
  // one bit after value bits
  static const int kInvalidMask = 0x80;

  explicit PackedCache() {
    COMPILE_ASSERT(kKeybits + kValuebits + 1 <= 8 * sizeof(T), use_whole_keys);
    COMPILE_ASSERT(kHashbits <= kKeybits, hash_function);
    COMPILE_ASSERT(kHashbits >= kValuebits + 1, small_values_space);
    Clear();
  }

  bool TryGet(K key, V* out) const {
    // As with other code in this class, we touch array_ as few times
    // as we can.  Assuming entries are read atomically then certain
    // races are harmless.
    ASSERT(key == (key & kKeyMask));
    T hash = Hash(key);
    T expected_entry = key;
    expected_entry &= ~N_ONES_(T, kHashbits);
    T entry = array_[hash];
    entry ^= expected_entry;
    if (PREDICT_FALSE(entry >= (1 << kValuebits))) {
      return false;
    }
    *out = static_cast<V>(entry);
    return true;
  }

  void Clear() {
    // sets 'invalid' bit in every byte, include value byte
    memset(const_cast<T* >(array_), kInvalidMask, sizeof(array_));
  }

  void Put(K key, V value) {
    ASSERT(key == (key & kKeyMask));
    ASSERT(value == (value & kValueMask));
    array_[Hash(key)] = KeyToUpper(key) | value;
  }

  void Invalidate(K key) {
    ASSERT(key == (key & kKeyMask));
    array_[Hash(key)] = KeyToUpper(key) | kInvalidMask;
  }

 private:
  // we just wipe all hash bits out of key. I.e. clear lower
  // kHashbits. We rely on compiler knowing value of Hash(k).
  static T KeyToUpper(K k) {
    return static_cast<T>(k) ^ Hash(k);
  }

  static T Hash(K key) {
    return static_cast<T>(key) & N_ONES_(size_t, kHashbits);
  }

  // For masking a K.
  static const K kKeyMask = N_ONES_(K, kKeybits);

  // For masking a V or a T.
  static const V kValueMask = N_ONES_(V, kValuebits);

  // array_ is the cache.  Its elements are volatile because any
  // thread can write any array element at any time.
  volatile T array_[1 << kHashbits];
};

#undef N_ONES_

#endif  // TCMALLOC_PACKED_CACHE_INL_H_
