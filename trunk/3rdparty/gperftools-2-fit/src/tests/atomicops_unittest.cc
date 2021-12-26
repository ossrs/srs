// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
/* Copyright (c) 2006, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ---
 * Author: Sanjay Ghemawat
 */

#include <stdio.h>
#include "base/logging.h"
#include "base/atomicops.h"

#define GG_ULONGLONG(x)  static_cast<uint64>(x)


#define NUM_BITS(T) (sizeof(T) * 8)


template <class AtomicType>
static void TestCompareAndSwap(AtomicType (*compare_and_swap_func)
                               (volatile AtomicType*, AtomicType, AtomicType)) {
  AtomicType value = 0;
  AtomicType prev = (*compare_and_swap_func)(&value, 0, 1);
  ASSERT_EQ(1, value);
  ASSERT_EQ(0, prev);

  // Use test value that has non-zero bits in both halves, more for testing
  // 64-bit implementation on 32-bit platforms.
  const AtomicType k_test_val = (GG_ULONGLONG(1) <<
                                 (NUM_BITS(AtomicType) - 2)) + 11;
  value = k_test_val;
  prev = (*compare_and_swap_func)(&value, 0, 5);
  ASSERT_EQ(k_test_val, value);
  ASSERT_EQ(k_test_val, prev);

  value = k_test_val;
  prev = (*compare_and_swap_func)(&value, k_test_val, 5);
  ASSERT_EQ(5, value);
  ASSERT_EQ(k_test_val, prev);
}


template <class AtomicType>
static void TestAtomicExchange(AtomicType (*atomic_exchange_func)
                               (volatile AtomicType*, AtomicType)) {
  AtomicType value = 0;
  AtomicType new_value = (*atomic_exchange_func)(&value, 1);
  ASSERT_EQ(1, value);
  ASSERT_EQ(0, new_value);

  // Use test value that has non-zero bits in both halves, more for testing
  // 64-bit implementation on 32-bit platforms.
  const AtomicType k_test_val = (GG_ULONGLONG(1) <<
                                 (NUM_BITS(AtomicType) - 2)) + 11;
  value = k_test_val;
  new_value = (*atomic_exchange_func)(&value, k_test_val);
  ASSERT_EQ(k_test_val, value);
  ASSERT_EQ(k_test_val, new_value);

  value = k_test_val;
  new_value = (*atomic_exchange_func)(&value, 5);
  ASSERT_EQ(5, value);
  ASSERT_EQ(k_test_val, new_value);
}


// This is a simple sanity check that values are correct. Not testing
// atomicity
template <class AtomicType>
static void TestStore() {
  const AtomicType kVal1 = static_cast<AtomicType>(0xa5a5a5a5a5a5a5a5LL);
  const AtomicType kVal2 = static_cast<AtomicType>(-1);

  AtomicType value;

  base::subtle::NoBarrier_Store(&value, kVal1);
  ASSERT_EQ(kVal1, value);
  base::subtle::NoBarrier_Store(&value, kVal2);
  ASSERT_EQ(kVal2, value);

  base::subtle::Release_Store(&value, kVal1);
  ASSERT_EQ(kVal1, value);
  base::subtle::Release_Store(&value, kVal2);
  ASSERT_EQ(kVal2, value);
}

// This is a simple sanity check that values are correct. Not testing
// atomicity
template <class AtomicType>
static void TestLoad() {
  const AtomicType kVal1 = static_cast<AtomicType>(0xa5a5a5a5a5a5a5a5LL);
  const AtomicType kVal2 = static_cast<AtomicType>(-1);

  AtomicType value;

  value = kVal1;
  ASSERT_EQ(kVal1, base::subtle::NoBarrier_Load(&value));
  value = kVal2;
  ASSERT_EQ(kVal2, base::subtle::NoBarrier_Load(&value));

  value = kVal1;
  ASSERT_EQ(kVal1, base::subtle::Acquire_Load(&value));
  value = kVal2;
  ASSERT_EQ(kVal2, base::subtle::Acquire_Load(&value));
}

template <class AtomicType>
static void TestAtomicOps() {
  TestCompareAndSwap<AtomicType>(base::subtle::NoBarrier_CompareAndSwap);
  TestCompareAndSwap<AtomicType>(base::subtle::Acquire_CompareAndSwap);
  TestCompareAndSwap<AtomicType>(base::subtle::Release_CompareAndSwap);

  TestAtomicExchange<AtomicType>(base::subtle::NoBarrier_AtomicExchange);
  TestAtomicExchange<AtomicType>(base::subtle::Acquire_AtomicExchange);
  TestAtomicExchange<AtomicType>(base::subtle::Release_AtomicExchange);

  TestStore<AtomicType>();
  TestLoad<AtomicType>();
}

int main(int argc, char** argv) {
  TestAtomicOps<AtomicWord>();
  TestAtomicOps<Atomic32>();
  printf("PASS\n");
  return 0;
}
